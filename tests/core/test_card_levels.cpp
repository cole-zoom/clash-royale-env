#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "CardLevels.h"
#include "ClashEnv.h"
#include "GameManager.h"
#include "CardRegistry.h"
#include "CombatEntity.h"
#include "Tower.h"
#include "StatsEventBus.h"
#include <memory>
#include <stdexcept>
#include <vector>

// Per-card levels (CardLevels.h): a card at level L has its level-11 hp and damage times
// PowerLevelMultiplier(L) / 256, truncated; what it spawns takes the same level. Level 7 is 176 / 256.

namespace {

constexpr int KNIGHT = 0;
constexpr int ARROWS = 3;
constexpr int GOBLINS = 4;
constexpr int MUSKETEER = 6;
constexpr int FIREBALL = 7;
constexpr int GOLEM = 19;
constexpr int WITCH = 71;
constexpr int GOBLIN_HUT = 95;

int at7(int level11) { return level11 * 176 / 256; }

const std::vector<int>& deck() {
    static const std::vector<int> d = { KNIGHT, FIREBALL, GOLEM, WITCH, MUSKETEER, GOBLINS, ARROWS, GOBLIN_HUT };
    return d;
}

struct Recorder : IStatsObserver {
    std::vector<DamageDealtEvent> damage;
    void onDamageDealt(const DamageDealtEvent& e) override { damage.push_back(e); }
};

struct Match {
    GameManager game;
    std::shared_ptr<Recorder> rec = std::make_shared<Recorder>();

    Match() : game(deck(), deck()) { start(); }
    void start() {
        game.seed(1);
        game.reset();
        rec = std::make_shared<Recorder>();
        game.getBoard().statsEvents.subscribe(rec);
    }
    Board& board() { return game.getBoard(); }
    void step(int n = 1) { for (int i = 0; i < n; ++i) game.step(); }

    // Plays `cardId` for `team` through the real play path and returns the bodies it put on the board.
    std::vector<std::shared_ptr<Entity>> play(int team, int cardId, float x, float y) {
        std::vector<int> hand = { cardId };
        for (int c : deck()) if (c != cardId && hand.size() < 4) hand.push_back(c);
        REQUIRE(game.setHand(team, hand));
        game.setElixir(team, 10.0f);
        const size_t before = board().pendingEntityCount();
        REQUIRE(game.playCard(team, cardId, x, y));
        std::vector<std::shared_ptr<Entity>> out;
        for (size_t i = before; i < board().pendingEntityCount(); ++i) out.push_back(board().getPendingEntity(i));
        board().commitPendingEntities();
        return out;
    }
    std::shared_ptr<StationaryCombatant> dummy(float x, float y, int team, int hp) {
        auto d = std::make_shared<StationaryCombatant>(board().allocateId(), x, y, hp, team, 0.5f, 0, 1000);
        spawn(board(), d);
        return d;
    }
    int firstHitBy(int attackerId, int maxSteps) {
        for (int i = 0; i < maxSteps; ++i) {
            for (const auto& d : rec->damage) if (d.attackerId == attackerId) return d.amount;
            step();
        }
        return -1;
    }
};

std::shared_ptr<CombatEntity> combat(const std::shared_ptr<Entity>& e) {
    return std::dynamic_pointer_cast<CombatEntity>(e);
}

} // namespace

TEST_CASE("The level table is rarities.csv's PowerLevelMultiplier, level 11 the identity", "[card_levels]") {
    REQUIRE(cardLevelMultiplier(1) == 100);
    REQUIRE(cardLevelMultiplier(7) == 176);
    REQUIRE(cardLevelMultiplier(11) == CARD_LEVEL_BASE_MULT);
    REQUIRE(CARD_LEVEL_BASE_MULT == 256);
    REQUIRE(cardLevelMultiplier(20) == 600);
    REQUIRE_THROWS_AS(cardLevelMultiplier(0), std::invalid_argument);
    REQUIRE_THROWS_AS(cardLevelMultiplier(21), std::invalid_argument);
    REQUIRE(scaleByLevel(1766, 256) == 1766);
    REQUIRE(scaleByLevel(1766, 176) == 1214);   // floor(1214.125)
}

TEST_CASE("Card levels default to 11 and leave a card's stats unchanged", "[card_levels]") {
    Match m;
    REQUIRE(m.game.getCardLevel(0, KNIGHT) == 11);
    REQUIRE(m.game.getCardLevel(1, FIREBALL) == 11);
    auto bodies = m.play(0, KNIGHT, 8.5f, 10.0f);
    REQUIRE(bodies.size() == 1);
    auto knight = combat(bodies[0]);
    REQUIRE(knight->hp == 1766);
    REQUIRE(knight->getBaseDamage() == 202);
    REQUIRE(knight->levelMult == CARD_LEVEL_BASE_MULT);
}

TEST_CASE("setCardLevel refuses a level, team or card id out of range", "[card_levels]") {
    Match m;
    REQUIRE_THROWS_AS(m.game.setCardLevel(0, KNIGHT, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(m.game.setCardLevel(0, KNIGHT, 21), std::invalid_argument);
    REQUIRE_THROWS_AS(m.game.setCardLevel(2, KNIGHT, 7), std::invalid_argument);
    REQUIRE_THROWS_AS(m.game.setCardLevel(0, -1, 7), std::invalid_argument);
    REQUIRE_THROWS_AS(m.game.setCardLevel(0, CARD_ID_COUNT, 7), std::invalid_argument);
    REQUIRE(m.game.getCardLevel(0, KNIGHT) == 11);
}

TEST_CASE("A level-7 troop spawns with scaled hp and hits for scaled damage", "[card_levels]") {
    Match m;
    m.game.setCardLevel(0, KNIGHT, 7);
    REQUIRE(m.game.getCardLevel(0, KNIGHT) == 7);
    // Mid-board, out of every tower's range.
    auto target = m.dummy(8.5f, 13.0f, 1, 100000);
    auto bodies = m.play(0, KNIGHT, 8.5f, 12.0f);
    REQUIRE(bodies.size() == 1);
    auto knight = combat(bodies[0]);
    REQUIRE(knight->hp == at7(1766));   // 1214
    REQUIRE(knight->getBaseDamage() == at7(202));   // 138

    const int hit = m.firstHitBy(knight->id, 60);
    REQUIRE(hit == at7(202));
    REQUIRE(target->hp == 100000 - at7(202));
}

TEST_CASE("A level-7 spell deals scaled damage to a troop and its scaled crown-tower part to a tower",
          "[card_levels][spell][tower]") {
    Match m;
    m.game.setCardLevel(0, FIREBALL, 7);
    // Next to team 1's left princess tower (3.0, 27.0), inside Fireball's 2.5 radius.
    auto target = m.dummy(3.0f, 25.5f, 1, 100000);
    auto spellBodies = m.play(0, FIREBALL, 3.0f, 26.5f);
    REQUIRE(spellBodies.size() == 1);
    const int spellId = spellBodies[0]->id;
    m.step(40);

    int troopHit = -1, towerHit = -1;
    for (const auto& d : m.rec->damage) {
        if (d.attackerId != spellId) continue;
        if (d.targetId == target->id) troopHit = d.amount;
        if (d.targetIsTower) towerHit = d.amount;
    }
    const int scaled = at7(689);   // 473
    REQUIRE(troopHit == scaled);
    // CrownTowerDamagePercent -77% on top of the scaled damage, rounded as the registry does.
    REQUIRE(towerHit == static_cast<int>(scaled * 0.23f + 0.5f));   // 109; level 11 gives 158
}

TEST_CASE("A level-11 spell keeps its crown-tower damage", "[card_levels][spell][tower]") {
    Match m;
    auto spellBodies = m.play(0, FIREBALL, 3.0f, 26.5f);
    const int spellId = spellBodies[0]->id;
    m.step(40);
    int towerHit = -1;
    for (const auto& d : m.rec->damage) if (d.attackerId == spellId && d.targetIsTower) towerHit = d.amount;
    REQUIRE(towerHit == static_cast<int>(689 * 0.23f + 0.5f));
}

TEST_CASE("A death spawn and death damage take the dying card's level", "[card_levels][spawn]") {
    Match m;
    m.game.setCardLevel(0, GOLEM, 7);
    auto bodies = m.play(0, GOLEM, 8.5f, 12.0f);
    REQUIRE(bodies.size() == 1);
    auto golem = combat(bodies[0]);
    REQUIRE(golem->hp == at7(5120));
    // An enemy inside the 2.5-tile death explosion, out of every tower's range.
    auto target = m.dummy(8.5f, 13.0f, 1, 100000);
    golem->hp = 0;
    m.step(2);   // dies, then the Golemites commit

    int golemites = 0;
    for (const auto& e : m.board().getEntities()) {
        if (e->team != 0 || e->name != "Golemite") continue;
        ++golemites;
        REQUIRE(e->hp == at7(1039));   // 714
        REQUIRE(combat(e)->getBaseDamage() == at7(84));
        REQUIRE(e->levelMult == 176);
    }
    REQUIRE(golemites == 2);
    REQUIRE(target->hp == 100000 - at7(200));   // AreaDamageOnDeath 200 -> 137
}

TEST_CASE("A unit spawned during its parent's update takes the parent's level", "[card_levels][spawn]") {
    Match m;
    m.game.setCardLevel(0, WITCH, 7);
    auto bodies = m.play(0, WITCH, 8.5f, 10.0f);
    REQUIRE(bodies.size() == 1);
    REQUIRE(bodies[0]->hp == at7(839));
    int skeletons = 0;
    for (int i = 0; i < 100 && skeletons == 0; ++i) {
        m.step();
        for (const auto& e : m.board().getEntities()) {
            if (e->team != 0 || e->name != "Skeletons") continue;
            ++skeletons;
            REQUIRE(e->hp == at7(81));   // 55
        }
    }
    REQUIRE(skeletons == 4);
}

TEST_CASE("Card levels are per team", "[card_levels]") {
    Match m;
    m.game.setCardLevel(0, KNIGHT, 7);
    REQUIRE(m.game.getCardLevel(1, KNIGHT) == 11);
    auto ours = m.play(0, KNIGHT, 8.5f, 10.0f);
    auto theirs = m.play(1, KNIGHT, 8.5f, 23.0f);
    REQUIRE(ours[0]->hp == at7(1766));
    REQUIRE(theirs[0]->hp == 1766);
    REQUIRE(combat(theirs[0])->getBaseDamage() == 202);
}

TEST_CASE("Card levels survive reset and are copied by snapshot", "[card_levels]") {
    Match m;
    m.game.setCardLevel(0, KNIGHT, 7);
    m.game.setCardLevel(1, FIREBALL, 14);
    m.start();   // reset()
    REQUIRE(m.game.getCardLevel(0, KNIGHT) == 7);
    REQUIRE(m.game.getCardLevel(1, FIREBALL) == 14);

    auto bodies = m.play(0, KNIGHT, 8.5f, 10.0f);
    GameManager copy = m.game.snapshot();
    REQUIRE(copy.getCardLevel(0, KNIGHT) == 7);
    REQUIRE(copy.getCardLevel(1, FIREBALL) == 14);
    // The copied body keeps its level and is not scaled again.
    bool found = false;
    for (const auto& e : copy.getBoard().getEntities()) {
        if (e->id != bodies[0]->id) continue;
        found = true;
        REQUIRE(e.get() != bodies[0].get());
        REQUIRE(e->hp == at7(1766));
        REQUIRE(e->levelMult == 176);
    }
    REQUIRE(found);
    // A play in the copy is levelled too.
    std::vector<int> hand = { KNIGHT, FIREBALL, GOLEM, WITCH };
    REQUIRE(copy.setHand(0, hand));
    copy.setElixir(0, 10.0f);
    const size_t before = copy.getBoard().pendingEntityCount();
    REQUIRE(copy.playCard(0, KNIGHT, 5.0f, 10.0f));
    REQUIRE(copy.getBoard().getPendingEntity(before)->hp == at7(1766));
}

TEST_CASE("ClashEnv exposes card levels, and inject spawns at the team's level", "[card_levels][clash_env]") {
    ClashEnv env(deck(), deck());
    env.reset();
    REQUIRE(env.getCardLevel(0, KNIGHT) == 11);
    env.setCardLevel(0, KNIGHT, 7);
    env.reset();
    REQUIRE(env.getCardLevel(0, KNIGHT) == 7);
    REQUIRE_THROWS_AS(env.setCardLevel(0, KNIGHT, 42), std::invalid_argument);
    env.inject(KNIGHT, 8.5f, 10.0f, 0);
    env.inject(KNIGHT, 8.5f, 23.0f, 1);
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);   // commits the injected bodies
    int ours = -1, theirs = -1;
    for (const auto& u : env.getUnits()) {
        if (u.cardId != KNIGHT) continue;
        (u.team == 0 ? ours : theirs) = u.hp;
    }
    REQUIRE(ours == at7(1766));
    REQUIRE(theirs == 1766);
    ClashEnv copy = env.snapshot();
    REQUIRE(copy.getCardLevel(0, KNIGHT) == 7);
}
