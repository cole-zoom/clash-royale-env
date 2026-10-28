#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "ClashEnv.h"
#include "CardRegistry.h"
#include "RangedTroop.h"
#include "Projectile.h"
#include "Building.h"
#include "Tower.h"
#include "AreaSpell.h"
#include <vector>

// Six card mechanics re-pinned against the installed client's logic tables
// (bot-77 scripts/audit_client_stats.py, level 11), on Cole's go-ahead of
// 2026-10-28: Earthquake's building and Crown Tower damage, Three Musketeers'
// bayonet, Monk's combo, Hunter's pellets, Firecracker's sparks and Battle
// Healer's heals.

namespace {

// GameManager::step()'s board half, in its order.
void tickBoard(Board& board, int tick) {
    board.currentTick = tick;
    board.commitPendingEntities(tick);
    for (const auto& e : board.getEntities()) if (e->isAlive()) e->update(board);
    board.commitPendingEntities(tick);
    board.resolveCollisions();
    board.cleanDeadEntities(tick);
}

std::shared_ptr<CombatEntity> spawnCard(Board& board, int cardId, float x, float y, int team) {
    const CardDefinition* def = CardRegistry::getInstance().getCard(cardId);
    REQUIRE(def != nullptr);
    const size_t before = board.getEntities().size();
    def->spawnEntity(x, y, team, board);
    board.commitPendingEntities();
    REQUIRE(board.getEntities().size() > before);
    return std::dynamic_pointer_cast<CombatEntity>(board.getEntities()[before]);
}

// A target that never moves on its own and deals nothing back.
std::shared_ptr<StationaryCombatant> dummyTarget(Board& board, float x, float y, int team, int hp = 100000) {
    auto t = std::make_shared<StationaryCombatant>(board.allocateId(), x, y, hp, team, 0.5f, 0, 10);
    spawn(board, t);
    return t;
}

int countOf(Board& board, bool (*pred)(const Entity&)) {
    int n = 0;
    for (const auto& e : board.getEntities()) if (e->isAlive() && pred(*e)) ++n;
    return n;
}

} // namespace

// ---------------- Hunter ----------------

TEST_CASE("Hunter point-blank: all 10 pellets of 84 land, 840", "[client_mechanics][hunter]") {
    Board board;
    auto target = dummyTarget(board, 9.0f, 6.0f, 1);
    spawnCard(board, 62, 9.0f, 5.0f, 0);
    for (int t = 0; t < 40; ++t) tickBoard(board, t);  // one volley: deploy 10 + load 15, next at +22
    REQUIRE(100000 - target->hp == 840);
}

TEST_CASE("Hunter at full range: the 20-degree fan spreads past a lone troop, 8 of 10 land", "[client_mechanics][hunter]") {
    Board board;
    // 4.7 from the Hunter, inside his 4.0 + 0.4 + 0.4 reach. The edge pellets
    // (+-10 degrees) pass 4.7 sin 10 = 0.82 from its centre, beyond 0.3 + 0.4.
    auto target = dummyTarget(board, 9.0f, 9.7f, 1);
    spawnCard(board, 62, 9.0f, 5.0f, 0);
    for (int t = 0; t < 40; ++t) tickBoard(board, t);
    REQUIRE(100000 - target->hp == 8 * 84);
}

TEST_CASE("A pellet stops on the first unit it touches; a piercing shot flies on", "[client_mechanics][pellets]") {
    Board board;
    auto nearUnit = std::make_shared<DummyEntity>(1, 5.0f, 7.0f, 1000, 1);
    auto farUnit = std::make_shared<DummyEntity>(2, 5.0f, 9.0f, 1000, 1);
    spawn(board, nearUnit);
    spawn(board, farUnit);

    SECTION("stop on first collision (Hunter)") {
        PelletSpread one = HUNTER_PELLETS;
        one.count = 1;
        firePelletFan(board, Vector2D{ 5.0f, 5.0f }, farUnit->position, 0.0f, 0.0f, one, 84, 0, -1, 62);
        for (int t = 0; t < 10; ++t) tickBoard(board, t);
        REQUIRE(nearUnit->hp == 1000 - 84);
        REQUIRE(farUnit->hp == 1000);  // shielded by the nearer body
    }
    SECTION("piercing (Firecracker spark)") {
        PelletSpread one = FIRECRACKER_SPARKS;
        one.count = 1;
        firePelletFan(board, Vector2D{ 5.0f, 5.0f }, farUnit->position, 0.0f, 0.0f, one, 64, 0, -1, 64);
        for (int t = 0; t < 10; ++t) tickBoard(board, t);
        REQUIRE(nearUnit->hp == 1000 - 64);
        REQUIRE(farUnit->hp == 1000 - 64);  // once each
    }
    SECTION("a shot vanishes after its range") {
        PelletSpread one = HUNTER_PELLETS;
        one.count = 1;
        one.range = 1.0f;  // never reaches the near unit 2 tiles out
        firePelletFan(board, Vector2D{ 5.0f, 5.0f }, farUnit->position, 0.0f, 0.0f, one, 84, 0, -1, 62);
        for (int t = 0; t < 10; ++t) tickBoard(board, t);
        REQUIRE(nearUnit->hp == 1000);
        REQUIRE(countOf(board, [](const Entity& e) { return dynamic_cast<const PelletProjectile*>(&e) != nullptr; }) == 0);
    }
}

// ---------------- Firecracker ----------------

TEST_CASE("Firecracker: the shell bursts into 5 sparks of 64 that also hit a unit behind the target",
          "[client_mechanics][firecracker]") {
    Board board;
    auto target = dummyTarget(board, 9.0f, 8.0f, 1);
    auto behind = dummyTarget(board, 9.0f, 10.0f, 1);   // 2 tiles past the target, on the shot's line
    auto aside = dummyTarget(board, 14.0f, 10.0f, 1);   // well off the fan
    spawnCard(board, 64, 9.0f, 3.0f, 0);
    for (int t = 0; t < 60; ++t) tickBoard(board, t);  // one shot: deploy 10 + load 24, next at +30
    REQUIRE(100000 - target->hp == 5 * 64);
    REQUIRE(100000 - behind->hp == 5 * 64);
    REQUIRE(aside->hp == 100000);
}

TEST_CASE("Firecracker's Evolution keeps its own shot (untouched by the spark rework)", "[client_mechanics][firecracker]") {
    const CardDefinition* evo = CardRegistry::getInstance().getCard(130);
    REQUIRE(evo != nullptr);
    for (bool evolved : { false, true }) {
        Board board;
        if (evolved) evo->spawnEvolvedEntity(5.0f, 5.0f, 0, board);
        else evo->spawnEntity(5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto fc = std::dynamic_pointer_cast<RangedTroop>(board.getEntities()[0]);
        REQUIRE(fc != nullptr);
        REQUIRE(fc->burstPellets.count == 0);
        REQUIRE(fc->splashRadius == Catch::Approx(1.5f));
    }
    REQUIRE(CardRegistry::getInstance().getStats(64)->burstPellets.count == 5);
    REQUIRE(CardRegistry::getInstance().getStats(64)->splashRadius == 0.0f);
}

// ---------------- Monk ----------------

TEST_CASE("Monk: combo 140 / 140 / 422, the third hit knocking back 1.8 tiles", "[client_mechanics][monk]") {
    Board board;
    auto target = dummyTarget(board, 9.0f, 6.5f, 1);
    auto monk = spawnCard(board, 119, 9.0f, 5.0f, 0);
    std::vector<int> hits;
    float pushed = 0.0f;
    for (int t = 0; t < 120 && hits.size() < 4; ++t) {
        const int hpBefore = target->hp;
        const float yBefore = target->position.y;
        tickBoard(board, t);
        if (target->hp < hpBefore) {
            hits.push_back(hpBefore - target->hp);
            if (hits.size() == 3) pushed = target->position.y - yBefore;
        }
    }
    REQUIRE(hits == std::vector<int>{ 140, 140, 422, 140 });
    REQUIRE(pushed == Catch::Approx(1.8f).margin(0.05f));
}

TEST_CASE("Monk's combo counter carries across targets", "[client_mechanics][monk]") {
    NoLoadTimes noLoad;
    Board board;
    auto first = std::make_shared<DummyEntity>(1, 0.0f, 1.0f, 100, 1);   // dies to hit 1
    auto second = std::make_shared<DummyEntity>(2, 0.0f, 2.0f, 100000, 1);
    spawn(board, first);
    spawn(board, second);
    auto monk = std::make_shared<StationaryCombatant>(3, 0.0f, 0.0f, 2214, 0, 3.0f, 140, 1);
    monk->attackSequenceDamage = { 140, 140, 422 };
    spawn(board, monk);

    monk->update(board);
    REQUIRE_FALSE(first->isAlive());
    monk->update(board);
    REQUIRE(second->hp == 100000 - 140);         // hit 2 of the combo
    monk->update(board);
    REQUIRE(second->hp == 100000 - 140 - 422);   // hit 3, not a reset to hit 1
}

// ---------------- Earthquake ----------------

TEST_CASE("Earthquake: 81 a second to troops, 286 to buildings, 49 to Crown Towers", "[client_mechanics][earthquake]") {
    Board board;
    auto troop = dummyTarget(board, 9.0f, 20.0f, 1, 10000);
    auto building = std::make_shared<Building>(board.allocateId(), 10.5f, 21.0f, 10000, 1, 'C', 5.5f, 0, 10, 100000);
    spawn(board, building);
    auto tower = std::make_shared<Tower>(board.allocateId(), 7.0f, 19.0f, 10000, 1, 7.5f, 0, 8, 'P');
    spawn(board, tower);
    const CardDefinition* def = CardRegistry::getInstance().getCard(103);
    def->spawnEntity(9.0f, 20.0f, 0, board);
    board.commitPendingEntities();
    std::shared_ptr<AreaSpell> quake;
    for (const auto& e : board.getEntities()) if (auto s = std::dynamic_pointer_cast<AreaSpell>(e)) quake = s;
    REQUIRE(quake != nullptr);
    for (int t = 0; t < 60 && quake->isAlive(); ++t) quake->update(board);

    REQUIRE(10000 - troop->hp == 3 * 81);
    REQUIRE(10000 - building->hp == 3 * 286);
    REQUIRE(10000 - tower->hp == 3 * 49);
}

TEST_CASE("Earthquake on a real Princess Tower: 147, no further spell reduction", "[client_mechanics][earthquake]") {
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    ClashEnv env(deck, deck);
    env.reset();
    const int before = env.getTowerHp(1, 1);
    env.inject(103, 3.0f, 27.0f, 0);  // centred on team 1's left princess tower
    for (int t = 0; t < 50; ++t) env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
    REQUIRE(before - env.getTowerHp(1, 1) == 147);
}

// ---------------- Three Musketeers ----------------

TEST_CASE("Three Musketeers are three Elite Musketeers: 906 hp, 204 shot, 314 bayonet, 1.3 s", "[client_mechanics][three_musketeers]") {
    const CardStats* s = CardRegistry::getInstance().getStats(80);
    REQUIRE(s != nullptr);
    REQUIRE(s->hp == 906);
    REQUIRE(s->damage == 204);
    REQUIRE(s->attackCooldown == 13);
    REQUIRE(s->attackRange == Catch::Approx(6.0f));
    REQUIRE(s->meleeAltRange == Catch::Approx(1.6f));
    REQUIRE(s->meleeAltDamage == 314);
    REQUIRE(s->spawnOffsets.size() == 3);
    REQUIRE(CardRegistry::getInstance().getCard(80)->cost == Catch::Approx(9.0f));
}

TEST_CASE("Three Musketeers: bayonet against ground in 1.6, the shot beyond it and always against air",
          "[client_mechanics][three_musketeers]") {
    NoLoadTimes noLoad;
    Board board;
    const CardStats* s = CardRegistry::getInstance().getStats(80);
    auto musketeer = std::make_shared<RangedTroop>(board.allocateId(), 9.0f, 5.0f, s->hp, 0,
        s->speed, s->attackRange, s->damage, s->attackCooldown);
    musketeer->targetsAir = true;
    musketeer->sightRange = 6.0f;
    musketeer->meleeAltRange = s->meleeAltRange;
    musketeer->meleeAltDamage = s->meleeAltDamage;
    auto isShot = [](const Entity& e) { return dynamic_cast<const Projectile*>(&e) != nullptr; };

    SECTION("ground target within 1.6: a 314 bayonet hit, no shot") {
        auto enemy = dummyTarget(board, 9.0f, 7.0f, 1);   // 2.0 centre to centre: 1.2 surface to surface
        spawn(board, musketeer);
        musketeer->update(board);
        board.commitPendingEntities();
        REQUIRE(100000 - enemy->hp == 314);
        REQUIRE(countOf(board, isShot) == 0);
    }
    SECTION("ground target beyond 1.6: the 204 shot only") {
        auto enemy = dummyTarget(board, 9.0f, 9.0f, 1);
        spawn(board, musketeer);
        for (int t = 0; t < 8; ++t) tickBoard(board, t);   // one attack (cooldown 13), shot lands
        REQUIRE(100000 - enemy->hp == 204);
    }
    SECTION("air target point-blank: the shot, never the bayonet") {
        auto enemy = dummyTarget(board, 9.0f, 6.0f, 1);
        enemy->isFlying = true;
        spawn(board, musketeer);
        musketeer->update(board);
        board.commitPendingEntities();
        REQUIRE(countOf(board, isShot) == 1);
        for (int t = 0; t < 5; ++t) tickBoard(board, t);
        REQUIRE(100000 - enemy->hp == 204);
    }
}

// ---------------- Battle Healer ----------------

TEST_CASE("Battle Healer: 202 on deploy and 102 per attack to allied troops; never itself, another Battle Healer, or a building",
          "[client_mechanics][battle_healer]") {
    Board board;
    auto ally = dummyTarget(board, 9.0f, 4.0f, 0, 500);
    auto allyBuilding = std::make_shared<Building>(board.allocateId(), 9.0f, 2.5f, 1000, 0, 'C', 5.5f, 0, 10, 100000);
    spawn(board, allyBuilding);
    auto enemy = dummyTarget(board, 9.0f, 6.0f, 1);

    auto healerA = spawnCard(board, 49, 8.0f, 5.0f, 0);
    REQUIRE(ally->hp == 500 + 202);              // deploy heal
    REQUIRE(allyBuilding->hp == 1000);           // buildings are not healed
    healerA->hp = 1000;                          // wounded: nothing may heal it
    auto healerB = spawnCard(board, 49, 10.0f, 5.0f, 0);
    REQUIRE(ally->hp == 500 + 2 * 202);
    REQUIRE(healerA->hp == 1000);                // B's deploy heal skips A

    for (int t = 0; t < 60; ++t) tickBoard(board, t);
    const int attacks = (100000 - enemy->hp) / 268;
    REQUIRE(attacks >= 2);
    REQUIRE((100000 - enemy->hp) % 268 == 0);
    REQUIRE(ally->hp == 500 + 2 * 202 + attacks * 102);
    REQUIRE(healerA->hp == 1000);                // no self-heal, no heal from B
    REQUIRE(healerB->hp == 1920);
    REQUIRE(allyBuilding->hp <= 1000);           // only its own decay moves it
}
