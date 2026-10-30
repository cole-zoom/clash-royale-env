#include <catch_amalgamated.hpp>
#include "ClashEnv.h"
#include "LoadTimes.h"

// Load times (LoadTimes.h, from the game client's logic tables) and how the
// engine uses them (CombatEntity::loadModel). Measured through the env, one
// tick at a time.
namespace {
struct LoadSwitch {  // restores the process-wide switches after a case
    bool on = CombatEntity::loadTimesEnabled;
    int model = CombatEntity::loadModel;
    ~LoadSwitch() { CombatEntity::loadTimesEnabled = on; CombatEntity::loadModel = model; }
};

// Ticks at which the defender (a Cannon, team 1) loses more than its own decay,
// over `ticks` ticks. The attacker is injected already deployed and in range of
// the Cannon, which it targets.
std::vector<int> hitTicks(int attackerCard, int ticks = 60) {
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    ClashEnv env(deck, deck);
    env.reset();
    env.inject(25, 9.0f, 18.0f, 1, -1.0f, 0);          // a Cannon (team 1), the target
    env.inject(attackerCard, 9.0f, 16.6f, 0, -1.0f, 0);  // the attacker, just in front of it
    std::vector<int> out;
    int prev = -1;
    for (int t = 1; t <= ticks; ++t) {
        env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
        bool alive = false;
        for (const auto& u : env.getUnits()) {
            if (u.cardId != 25) continue;
            alive = true;
            if (prev >= 0 && prev - u.hp > 60) out.push_back(t);
            prev = u.hp;
        }
        if (!alive) {  // killed by this tick's hit
            if (prev > 0) out.push_back(t);
            break;
        }
    }
    return out;
}

int firstHit(int card, bool on, int model) {
    CombatEntity::loadTimesEnabled = on;
    CombatEntity::loadModel = model;
    auto h = hitTicks(card);
    return h.empty() ? -1 : h.front();
}
}  // namespace

TEST_CASE("load times come from the client: Knight 700 ms of 1200, Valkyrie 1400 of 1500", "[load_time]") {
    REQUIRE(CombatEntity::loadTimesEnabled);                                  // on by default
    REQUIRE(CombatEntity::loadModel == CombatEntity::LOAD_MODEL_PRELOAD);     // the game's model by default
    REQUIRE(loadTicksForCard(0) == msToTicks(700));
    REQUIRE(firstHitTicksForCard(0) == msToTicks(1200 - 700));                                    // 1200 - 700
    REQUIRE(loadTicksForCard(10) == msToTicks(1400));
    REQUIRE(firstHitTicksForCard(10) == msToTicks(1500 - 1400));                                   // 1500 - 1400
    REQUIRE(firstHitTicksForCard(15) == msToTicks(1600 - 1000));                                   // Hog Rider 1600 - 1000
    REQUIRE(firstHitTicksForCard(6) == msToTicks(1000 - 300));                                    // Musketeer 1000 - 300
    REQUIRE(firstHitTicksForCard(28) == -1);                                  // Inferno Tower: load is its lock-on
    REQUIRE(loadTicksForCard(3) == 0);                                        // Arrows: a spell
    REQUIRE(firstHitTicksForCard(3) == -1);
}

TEST_CASE("a loaded unit's first hit lands hit speed - load time after it reaches its target", "[load_time]") {
    LoadSwitch keep;
    // The Knight is in reach on the first tick: the game's model hits 0.5 s later
    // (RoyaleAPI "The Secret Stats": 1.2 - 0.7), the engine before 2026-10-29
    // waited the whole 0.7 s load.
    REQUIRE(firstHit(0, true, CombatEntity::LOAD_MODEL_PRELOAD) == 1 + 5);
    REQUIRE(firstHit(0, true, CombatEntity::LOAD_MODEL_WINDUP_PER_TARGET) == 1 + 7);
    // Valkyrie: 0.1 s, not 1.4 s
    REQUIRE(firstHit(10, true, CombatEntity::LOAD_MODEL_PRELOAD) == 1 + 1);
    REQUIRE(firstHit(10, true, CombatEntity::LOAD_MODEL_WINDUP_PER_TARGET) == 1 + 14);
}

TEST_CASE("after the first hit, hits stay one hit speed apart", "[load_time]") {
    LoadSwitch keep;
    CombatEntity::loadTimesEnabled = true;
    CombatEntity::loadModel = CombatEntity::LOAD_MODEL_PRELOAD;
    auto h = hitTicks(0, 60);  // Knight, 12-tick hit speed
    REQUIRE(h.size() >= 3);
    REQUIRE(h.front() == 6);
    for (size_t i = 1; i < h.size(); ++i) REQUIRE(h[i] - h[i - 1] == 12);
}

TEST_CASE("Princess Towers carry no card's load time and no deploy second", "[load_time][tower]") {
    // towerTroopStats() leaves CardStats::id at 0, the Knight's: every Princess
    // Tower used to wind up 0.7 s on every new target and stand inert for 1 s
    // after reset().
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    ClashEnv env(deck, deck);
    env.reset();
    for (const auto& u : env.getUnits()) {
        if (u.tower) REQUIRE(u.deployTicks == 0);
    }
    // A Knight deployed in a Princess Tower's range is shot on the tower's
    // cooldown alone, the same with or without load times.
    auto firstTowerShot = [&](bool on) {
        LoadSwitch keep;
        CombatEntity::loadTimesEnabled = on;
        ClashEnv e(deck, deck);
        e.reset();
        e.inject(0, 14.0f, 22.0f, 0, -1.0f, 0);  // own Knight near the enemy right Princess Tower
        int full = -1;
        for (int t = 1; t <= 40; ++t) {
            e.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
            for (const auto& u : e.getUnits()) {
                if (u.cardId != 0 || u.team != 0) continue;
                if (full < 0) full = u.hp;
                if (u.hp < full) return t;
            }
        }
        return -1;
    };
    const int on = firstTowerShot(true), off = firstTowerShot(false);
    REQUIRE(on > 0);
    REQUIRE(on == off);
}

TEST_CASE("a spawned body takes its card's first hit; Sparky spawns unloaded", "[load_time]") {
    REQUIRE(firstHitTicksForUnitName("Barbarians") == firstHitTicksForCard(8));
    REQUIRE(firstHitTicksForUnitName("Skeletons") == firstHitTicksForCard(24));
    REQUIRE(firstHitTicksForUnitName("Golemite") == -1);  // no playable card of that name
    REQUIRE(loadFirstHitForCard(60));                      // Sparky: the client's LoadFirstHit
    REQUIRE_FALSE(loadFirstHitForCard(0));
    LoadSwitch keep;
    CombatEntity::loadTimesEnabled = true;
    CombatEntity::loadModel = CombatEntity::LOAD_MODEL_PRELOAD;
    // Sparky reloads her full 3 s (her 4 s hit speed - 1 s windup) before her first shot.
    auto h = hitTicks(60, 120);
    REQUIRE_FALSE(h.empty());
    REQUIRE(h.front() >= 1 + 30 + 10);
}
