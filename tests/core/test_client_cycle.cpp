#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "ClientMechanics.h"
#include "LoadTimes.h"
#include "CardRegistry.h"
#include "GameManager.h"
#include "Projectile.h"
#include "Tower.h"
#include <vector>

// The attack cycle and targeting as the client's code runs them (2026-10-29, second batch; FidelitySwitches.h).
// The rules come from the game's battle library (bot-77 docs/client-code-notes.md: combat update 0x71fff599e4,
// timer advance 0x71fff5ac3c, set target 0x71fff558e0, target removed 0x71fff591f8, find target 0x71fff56878);
// the numbers from its logic tables through bot-77's generators (ClientMechanics.h, LoadTimes.h). Each case pins
// the client's rule and, with its switch off, the engine's earlier behaviour.
//
// The Knight (client LoadTime 700, HitSpeed 1200) is the probe: reach 1.2 + 0.5 + 0.4 = 2.1 to a test dummy.

namespace {

constexpr int KNIGHT = 0;
constexpr int GIANT = 2;
constexpr int FISHERMAN = 53;
constexpr int SPARKY = 60;

template <bool* Flag>
struct SwitchTo {
    bool was = *Flag;
    explicit SwitchTo(bool on) { *Flag = on; }
    ~SwitchTo() { *Flag = was; }
};

std::shared_ptr<CombatEntity> spawnReady(Board& board, int cardId, float x, float y, int team) {
    CardRegistry::getInstance().getCard(cardId)->spawnEntity(x, y, team, board);
    board.commitPendingEntities();
    auto e = std::dynamic_pointer_cast<CombatEntity>(board.getEntities().back());
    REQUIRE(e != nullptr);
    e->deployTicksRemaining = 0;  // already on the board
    return e;
}

std::shared_ptr<DummyEntity> dummy(Board& board, int id, float x, float y, int hp, int team = 1) {
    auto d = std::make_shared<DummyEntity>(id, x, y, hp, team);
    spawn(board, d);
    return d;
}

// One 100 ms step of the board (TICKS_PER_STEP engine ticks, Tick.h) without collisions (positions stay where the
// test puts them). `t` counts steps, so the tick numbers below are tenths of a second, as they were written.
void tick(Board& board, int t) {
    for (int k = 0; k < TICKS_PER_STEP; ++k) {
        const int engineTick = stepsToTicks(t - 1) + k + 1;
        board.currentTick = engineTick;
        for (const auto& e : board.getEntities()) if (e->isAlive()) e->update(board);
        board.commitPendingEntities(engineTick);
        board.cleanDeadEntities(engineTick);
    }
}

}  // namespace

TEST_CASE("client cycle values come from the client tables", "[client_cycle]") {
    REQUIRE(loadMsForCard(KNIGHT) == 700);
    REQUIRE(loadMsForCard(28) == -1);                       // Inferno Tower: load >= hit speed, not on the cycle
    REQUIRE(CLIENT_ATTACK_FINISH_TIME_MS == 250);
    REQUIRE(CLIENT_PENDING_DAMAGE_WINDOW_MS == 600);
    REQUIRE(CLIENT_RANGE_EXTENSION_TO_KEEP_TARGET == 25);
    REQUIRE(CLIENT_CANCEL_HIT_DISTANCE == 1500);
    REQUIRE(CLIENT_KING_TOWER_LOAD_TIME_MS == 500);
    REQUIRE(CLIENT_KING_TOWER_HIT_SPEED_MS == 1000);
    REQUIRE(CLIENT_PRINCESS_TOWER_LOAD_TIME_MS == 0);
    REQUIRE(CLIENT_PRINCESS_TOWER_HIT_SPEED_MS == 800);
    REQUIRE(clientSpecialLoadTimeForCard(FISHERMAN) == 1300);
    REQUIRE(clientSpecialLoadTimeForCard(KNIGHT) == 0);
    REQUIRE(CLIENT_ELIXIR_FULL_BAR_MS_1X == 28000);
    REQUIRE(Fidelity::reloadFromAttackStart);
    REQUIRE_FALSE(Fidelity::princessFirstShotDelay);       // waits for lab L18
    REQUIRE_FALSE(Fidelity::clientElixirRate);             // for Cole to decide
}

// ---------------- 1. reload from the attack start ----------------

TEST_CASE("the load resets when an attack starts and counts down every tick", "[client_cycle][reload]") {
    Board board;
    auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
    dummy(board, 100, 9.0f, 6.5f, 100000);
    REQUIRE(knight->clientCycleOn());
    tick(board, 1);                                         // in reach: the attack starts 700 into its 1200 cycle on the step's first tick
    REQUIRE(knight->attackTimerMs == 700 + (TICKS_PER_STEP - 1) * TICK_MS);   // and runs the rest of the step
    REQUIRE(knight->loadMissingMs == 700 - (TICKS_PER_STEP - 1) * TICK_MS);   // reloading from the start
    for (int t = 2; t <= 8; ++t) tick(board, t);
    REQUIRE(knight->loadMissingMs == 0);                    // loaded again 0.7 s after the start, mid-cycle
}

TEST_CASE("after a kill, a unit whose attack started long enough ago strikes again 0.5 s after reaching a new target",
          "[client_cycle][reload]") {
    // Hit 1 at tick 6 kills; a new target appears in reach at tick 10 (after the 250 ms window). The client: the load
    // was full again 0.7 s after the start (tick 8), so the new attack lands 0.5 s after it begins: tick 15. The
    // preload model reloaded 0.7 s from the HIT (ready at tick 13), then wound up 0.5 s: tick 18.
    auto run = [](bool clientCycle) {
        SwitchTo<&Fidelity::reloadFromAttackStart> sw(clientCycle);
        Board board;
        auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
        auto first = dummy(board, 100, 9.0f, 6.5f, 1);
        std::shared_ptr<DummyEntity> second;
        int hit1 = -1;
        for (int t = 1; t <= 30; ++t) {
            if (t == 10) second = dummy(board, 101, 9.0f, 6.6f, 100000);
            tick(board, t);
            if (hit1 < 0 && !first->isAlive()) hit1 = t;
            if (second && second->hp < 100000) return std::make_pair(hit1, t);
        }
        return std::make_pair(hit1, -1);
    };
    REQUIRE(run(true) == std::make_pair(6, 15));
    REQUIRE(run(false) == std::make_pair(6, 18));
}

// ---------------- 3. the 250 ms attack-finish window ----------------

TEST_CASE("a kill keeps the cycle for a new target in reach within 250 ms; otherwise the swing is lost",
          "[client_cycle][finish_window]") {
    // Kill at tick 6 (attack timer 1200). A target in reach at tick 8 (inside the window): the cycle carries on,
    // the timer paused while it waited, so the next hit lands one HitSpeed of running later: tick 19. Without the
    // window the swing is lost when nothing is in reach on the tick after the kill, and the new attack starts loaded
    // at tick 8: hit at tick 13.
    auto run = [](bool window) {
        SwitchTo<&Fidelity::attackFinishWindow> sw(window);
        Board board;
        auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
        auto first = dummy(board, 100, 9.0f, 6.5f, 1);
        std::shared_ptr<DummyEntity> second;
        for (int t = 1; t <= 30; ++t) {
            if (t == 8) second = dummy(board, 101, 9.0f, 6.6f, 100000);
            tick(board, t);
            if (t == 7) REQUIRE(knight->attacking == window);   // the swing waits, or is gone
            if (second && second->hp < 100000) return t;
        }
        return -1;
    };
    REQUIRE(run(true) == 19);
    REQUIRE(run(false) == 13);
}

TEST_CASE("a target in reach right after the kill keeps the cycle: hits stay one HitSpeed apart",
          "[client_cycle][finish_window]") {
    Board board;
    auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
    auto first = dummy(board, 100, 9.0f, 6.5f, 1);
    auto second = dummy(board, 101, 9.0f, 6.9f, 100000);  // farther: the Knight takes `first` first
    int hit2 = -1;
    for (int t = 1; t <= 30 && hit2 < 0; ++t) {
        tick(board, t);
        if (second->hp < 100000) hit2 = t;
    }
    REQUIRE_FALSE(first->isAlive());
    REQUIRE(hit2 == 6 + 12);
}

// ---------------- 2. stun pauses the swing ----------------

TEST_CASE("a stun pauses the swing and resumes it; the preload model rewound it", "[client_cycle][stun]") {
    // Attack starts tick 1 (700 of 1200), tick 2 at 800; a 5-tick stun (ticks 3-7) holds it; it resumes at tick 8
    // and lands at tick 11: the first hit (tick 6) pushed back by exactly the stun. Off: the stun cancels the swing
    // and the full 0.5 s windup starts again after it: tick 13.
    auto run = [](bool pauses) {
        SwitchTo<&Fidelity::stunPausesSwing> sw(pauses);
        Board board;
        auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
        auto target = dummy(board, 100, 9.0f, 6.5f, 100000);
        for (int t = 1; t <= 30; ++t) {
            if (t == 3) knight->applyFreeze(secs(0.5), 0.0f);
            tick(board, t);
            if (target->hp < 100000) return t;
        }
        return -1;
    };
    REQUIRE(run(true) == 11);
    REQUIRE(run(false) == 13);
}

TEST_CASE("a stunned unit drops its target and re-acquires the nearest each tick; a locked one does not switch",
          "[client_cycle][stun]") {
    Board board;
    auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
    auto far = dummy(board, 100, 9.0f, 6.9f, 100000);      // the only one at first
    tick(board, 1);
    REQUIRE(knight->getCurrentTargetId() == far->id);
    auto nearer = dummy(board, 101, 9.0f, 6.0f, 100000);   // nearer, also in reach
    tick(board, 2);
    REQUIRE(knight->getCurrentTargetId() == far->id);       // locked while in reach: no rescan
    knight->applyFreeze(secs(0.3), 0.0f);
    tick(board, 3);
    REQUIRE(knight->getCurrentTargetId() == nearer->id);    // dropped and re-acquired the nearest
    REQUIRE(knight->attacking);                             // in reach: the swing is kept, paused
    REQUIRE(knight->attackTimerMs == 700 + (2 * TICKS_PER_STEP - 1) * TICK_MS);   // where steps 1-2 left it
}

TEST_CASE("a pure slow keeps the target; Sparky's stun empties her load", "[client_cycle][stun]") {
    {
        Board board;
        auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
        auto far = dummy(board, 100, 9.0f, 6.9f, 100000);
        tick(board, 1);
        auto nearer = dummy(board, 101, 9.0f, 6.0f, 100000);
        knight->applyFreeze(secs(0.5), 0.5f);                       // a slow, not a stun
        tick(board, 2);
        REQUIRE(knight->getCurrentTargetId() == far->id);
        REQUIRE(knight->attackTimerMs == 700 + (TICKS_PER_STEP - 1) * TICK_MS + 50);   // step 2: 100 ms x 0.5
        (void)nearer;
    }
    {
        Board board;
        auto sparky = spawnReady(board, SPARKY, 9.0f, 5.0f, 0);
        dummy(board, 100, 9.0f, 9.0f, 100000);
        REQUIRE(sparky->loadFirstHit);
        for (int t = 1; t <= 20; ++t) tick(board, t);
        REQUIRE(sparky->attacking);
        REQUIRE(sparky->loadMissingMs < sparky->clientLoadMs);
        sparky->applyFreeze(secs(0.2), 0.0f);
        tick(board, 21);
        REQUIRE_FALSE(sparky->attacking);                   // reset
        REQUIRE(sparky->loadMissingMs == sparky->clientLoadMs);  // and unloaded (LOAD_FIRST_HIT ..._WHEN_ZAPPED)
    }
}

// ---------------- 4. pushback ----------------

TEST_CASE("a started swing runs to its hit wherever the target goes; an unstarted one is dropped", "[client_cycle][pushback]") {
    // Started (tick 2, 800 of 1200): the target is moved 1.0 past reach: the hit still lands at tick 6.
    {
        Board board;
        auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
        auto target = dummy(board, 100, 9.0f, 6.5f, 100000);
        tick(board, 1);
        tick(board, 2);
        REQUIRE(knight->swingStarted);
        target->position.y = 5.0f + 2.1f + 1.0f;
        int hit = -1;
        for (int t = 3; t <= 8 && hit < 0; ++t) { tick(board, t); if (target->hp < 100000) hit = t; }
        REQUIRE(hit == 6);
    }
    // Moved 2.0 past reach (beyond LOGIC_CANCEL_HIT_FROM_LONG_DISTANCE_RANGE 1.5): the Knight stands until the swing
    // would land (tick 6, its first 50 ms), the hit is cancelled, then it walks (the rest of tick 6). Off: the preload model's rule drops the swing at once
    // (kept only within reach + 1.5) and it walks from tick 3.
    auto run = [](bool finishes) {
        SwitchTo<&Fidelity::startedSwingFinishes> sw(finishes);
        Board board;
        auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
        auto target = dummy(board, 100, 9.0f, 6.5f, 100000);
        tick(board, 1);
        tick(board, 2);
        target->position.y = 5.0f + 2.1f + 2.0f;
        int moved = -1;
        for (int t = 3; t <= 10 && moved < 0; ++t) { tick(board, t); if (knight->position.y > 5.0f) moved = t; }
        REQUIRE(target->hp == 100000);
        return moved;
    };
    REQUIRE(run(true) == 6);   // 7 at the 100 ms tick: the walk began on the next whole tick
    REQUIRE(run(false) == 3);
    // Unstarted: right after a hit (tick 6) the swing is at a cycle boundary; the target leaves reach: no hit at the
    // next cycle, the Knight walks at once.
    {
        Board board;
        auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
        auto target = dummy(board, 100, 9.0f, 6.5f, 100000);
        for (int t = 1; t <= 6; ++t) tick(board, t);
        REQUIRE(target->hp == 100000 - 202);
        REQUIRE_FALSE(knight->swingStarted);
        target->position.y = 5.0f + 2.1f + 0.5f;
        tick(board, 7);
        REQUIRE_FALSE(knight->attacking);
        REQUIRE(knight->position.y > 5.0f);
    }
}

TEST_CASE("the current target is kept 0.025 past reach (LOGIC_RANGE_EXTENSION_TO_KEEP_TARGET)", "[client_cycle][pushback]") {
    // Locked on `a`, which drifts 0.02 past reach while a nearer `b` stands in reach: no rescan, `a` stays.
    Board board;
    auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
    auto a = dummy(board, 100, 9.0f, 7.0f, 100000);
    tick(board, 1);
    auto b = dummy(board, 101, 10.0f, 5.0f, 100000);
    a->position.y = 5.0f + 2.1f + 0.02f;
    tick(board, 2);
    REQUIRE(knight->getCurrentTargetId() == a->id);
    (void)b;
}

// ---------------- 5. SpecialLoadTime ----------------

TEST_CASE("Fisherman's hook winds up SpecialLoadTime 1.3 s before it pulls", "[client_cycle][special]") {
    auto run = [](bool windup) {
        SwitchTo<&Fidelity::specialLoadTime> sw(windup);
        Board board;
        auto fisherman = spawnReady(board, FISHERMAN, 9.0f, 5.0f, 0);
        auto target = std::make_shared<StationaryCombatant>(100, 9.0f, 10.0f, 100000, 1, 0.5f, 0, 10);
        spawn(board, target);
        for (int t = 1; t <= 30; ++t) {
            tick(board, t);
            if (target->position.y < 9.0f) return t;    // pulled in
        }
        return -1;
    };
    REQUIRE(run(false) == 1);
    REQUIRE(run(true) == 13);   // 13 ticks of 100 ms: 1300 ms
}

// ---------------- 6. targeting ----------------

TEST_CASE("sight is edge to edge: Knight vs Knight 5.5 + 0.5 + 0.5 = 6.5; the fitted rule saw 7.25",
          "[client_cycle][targeting]") {
    auto sees = [](float gap, bool client) {
        SwitchTo<&Fidelity::clientTargeting> sw(client);
        Board board;
        auto knight = spawnReady(board, KNIGHT, 9.0f, 5.0f, 0);
        auto enemy = spawnReady(board, KNIGHT, 9.0f, 5.0f + gap, 1);
        enemy->deployTicksRemaining = 50;                 // stands still
        tick(board, 1);
        return knight->getCurrentTargetId() == enemy->id;
    };
    REQUIRE(sees(6.4f, true));
    REQUIRE_FALSE(sees(6.6f, true));
    REQUIRE(sees(7.2f, false));
    REQUIRE_FALSE(sees(7.3f, false));
}

TEST_CASE("candidates are ranked by centre distance; the fitted rule subtracted the tower's 1.5 targeting radius",
          "[client_cycle][targeting]") {
    // A Giant 6.3 from a Princess Tower's centre and 6.0 from a building's: the client takes the building (nearer
    // by centre); the fitted rule ranks the tower at 6.3 - 1.5 = 4.8 against the building's 6.0 - 1.0 = 5.0.
    auto pick = [](bool client) {
        SwitchTo<&Fidelity::clientTargeting> sw(client);
        Board board;
        auto giant = spawnReady(board, GIANT, 9.0f, 20.0f, 0);
        auto tower = std::make_shared<Tower>(200, 9.0f, 26.3f, 3052, 1, 7.5f, 109, 8, 'P');
        auto hut = std::make_shared<Building>(201, 3.0f, 20.0f, 1000, 1, 'H', 0.0f, 0, 10, -1);
        spawn(board, tower);
        spawn(board, hut);
        tick(board, 1);
        return giant->getCurrentTargetId();
    };
    REQUIRE(pick(true) == 201);
    REQUIRE(pick(false) == 200);
}

// ---------------- 7. pending damage ----------------

TEST_CASE("pending damage: only a projectile attacker skips, and only for damage landing within 600 ms",
          "[client_cycle][pending_damage]") {
    // `doomed` (80 hp, nearer) has an ally's 100-damage shot on the way; `other` is farther.
    auto pick = [](bool projectileUnit, float shotSpeed, bool clientRule) {
        SwitchTo<&Fidelity::pendingDamageClientRule> sw(clientRule);
        Board board;
        auto shooter = std::make_shared<StationaryCombatant>(1, 5.0f, 5.0f, 1000, 0, 6.0f, 50, 10);
        if (projectileUnit) shooter->clientShotSpeed = 0.5f;
        auto doomed = std::make_shared<DummyEntity>(2, 5.0f, 8.0f, 80, 1);
        auto other = std::make_shared<DummyEntity>(3, 5.0f, 9.0f, 500, 1);
        spawn(board, shooter);
        spawn(board, doomed);
        spawn(board, other);
        // the shot is 2.0 tiles from `doomed`: 5 tiles a second lands in 400 ms, 2.5 in 800 ms
        spawn(board, std::make_shared<Projectile>(10, 5.0f, 10.0f, 0, doomed, shotSpeed, 100));
        shooter->update(board);
        return shooter->lastTargetId;
    };
    REQUIRE(pick(true, perTick(5.0f), true) == 3);    // a projectile unit, the shot lands in 400 ms: skipped
    REQUIRE(pick(true, perTick(2.5f), true) == 2);    // lands in 800 ms: not skipped
    REQUIRE(pick(false, perTick(5.0f), true) == 2);   // a melee attacker never skips
    REQUIRE(pick(false, perTick(2.5f), false) == 3);  // the 41fe8ad rule: everyone, whenever it lands
}

// ---------------- 8. King first shot ----------------

TEST_CASE("the King's first shot comes 0.5 s after it acquires, then every 1.0 s", "[client_cycle][king]") {
    auto shots = [](bool delay, char symbol, bool princessDelay) {
        SwitchTo<&Fidelity::kingFirstShotDelay> sw(delay);
        SwitchTo<&Fidelity::princessFirstShotDelay> sw2(princessDelay);
        Board board;
        const int cooldown = (symbol == 'R') ? secs(1.0) : secs(0.8);
        auto tower = std::make_shared<Tower>(200, 9.0f, 10.0f, 4824, 1, 7.0f, 109, cooldown, symbol);
        tower->clientLoadMs = (symbol == 'R') ? CLIENT_KING_TOWER_LOAD_TIME_MS : CLIENT_PRINCESS_TOWER_LOAD_TIME_MS;
        spawn(board, tower);
        dummy(board, 100, 9.0f, 5.0f, 100000, 0);
        std::vector<int> out;
        for (int t = 1; t <= 25; ++t) {   // 100 ms steps
            const size_t before = board.getEntities().size();
            for (int k = 0; k < TICKS_PER_STEP; ++k) {
                tower->update(board);
                board.commitPendingEntities(t);
            }
            if (board.getEntities().size() > before) out.push_back(t);
        }
        return out;
    };
    REQUIRE(shots(true, 'R', false) == std::vector<int>{6, 16});
    REQUIRE(shots(false, 'R', false) == std::vector<int>{1, 11, 21});
    // The Princess Tower (off by default): fires on acquiring; with the switch, one HitSpeed (0.8 s) later.
    REQUIRE(shots(true, 'P', false) == std::vector<int>{1, 9, 17, 25});
    REQUIRE(shots(true, 'P', true) == std::vector<int>{9, 17, 25});
}

TEST_CASE("GameManager gives the crown towers their client load", "[client_cycle][king]") {
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    GameManager game(deck, deck);
    int kings = 0, princesses = 0;
    for (const auto& e : game.getBoard().getEntities()) {
        auto t = std::dynamic_pointer_cast<Tower>(e);
        if (!t) continue;
        if (t->symbol == 'R') { ++kings; REQUIRE(t->clientLoadMs == 500); REQUIRE(t->clientCycleOn()); }
        else { ++princesses; REQUIRE(t->clientLoadMs == 0); REQUIRE_FALSE(t->clientCycleOn()); }
    }
    REQUIRE(kings == 2);
    REQUIRE(princesses == 4);
}

// ---------------- elixir (off by default) ----------------

TEST_CASE("client elixir rate: 2.8 s an elixir at 1x (off by default: 2.857)", "[client_cycle][elixir]") {
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    GameManager game(deck, deck);
    const float ticksPerSecond = 1000.0f / TICK_MS;
    REQUIRE(game.elixirRegenPerTick(0) * TICKS_PER_STEP == Catch::Approx(0.035f));   // 0.035 per 100 ms
    SwitchTo<&Fidelity::clientElixirRate> sw(true);
    REQUIRE(game.elixirRegenPerTick(0) * ticksPerSecond * 2.8f == Catch::Approx(1.0f));
    REQUIRE(game.elixirRegenPerTick(GameManager::DOUBLE_ELIXIR_TICK) * ticksPerSecond * 1.4f == Catch::Approx(1.0f));
    REQUIRE(game.elixirRegenPerTick(GameManager::TRIPLE_ELIXIR_TICK) * ticksPerSecond * 0.93f == Catch::Approx(1.0f));
}

TEST_CASE("crown towers are seen 2 tiles further, edge to edge: Knight 5.5 + 0.5 + 1.0 + 2 = 9.0", "[client_cycle][targeting]") {
    struct Probe : StationaryCombatant {
        using StationaryCombatant::StationaryCombatant;
        float sightTo(const Entity& e) const { return effectiveSightWith(ownEffectiveRadius(), e); }
    };
    auto probe = std::make_shared<Probe>(1, 9.0f, 5.0f, 100, 0, 1.2f, 0, 12);
    probe->bodyRadius = 0.5f;  // a Knight's
    auto tower = std::make_shared<Tower>(200, 9.0f, 14.0f, 3052, 1, 7.5f, 109, 8, 'P');  // body 1.0
    {
        SwitchTo<&Fidelity::crownTowerSightBonus> on(true);
        REQUIRE(probe->sightTo(*tower) == Catch::Approx(9.0f));
    }
    {
        SwitchTo<&Fidelity::crownTowerSightBonus> off(false);
        REQUIRE(probe->sightTo(*tower) == Catch::Approx(7.0f));
    }
}
