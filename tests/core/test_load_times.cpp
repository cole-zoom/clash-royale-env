#include <catch_amalgamated.hpp>
#include "ClashEnv.h"
#include "LoadTimes.h"

// The first-hit windup: a unit in range of a new target waits its card's load
// time (LoadTimes.h, RoyaleAPI cr-api-data) before the first hit, then hits at
// its attack cooldown. Measured through the env, one tick at a time.
namespace {
// Ticks from the start until the defender's hp first drops. The attacker is
// injected already deployed and in range of a stationary building it targets.
int ticksToFirstHit(int attackerCard, bool loadTimes) {
    const bool was = CombatEntity::loadTimesEnabled;
    CombatEntity::loadTimesEnabled = loadTimes;
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    ClashEnv env(deck, deck);
    env.reset();
    env.inject(25, 9.0f, 18.0f, 1, -1.0f, 0);          // a Cannon (team 1), the target
    env.inject(attackerCard, 9.0f, 16.6f, 0, -1.0f, 0);  // the attacker, just in front of it
    // A hit is a drop of more than the Cannon's own decay (27 hp a second).
    int first = -1;
    int prev = 824;  // the Cannon's full hp (injected units join on the first tick)
    for (int t = 1; t <= 60 && first < 0; ++t) {
        env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
        for (const auto& u : env.getUnits()) {
            if (u.cardId != 25) continue;
            if (prev >= 0 && prev - u.hp > 100) first = t;
            prev = u.hp;
        }
    }
    CombatEntity::loadTimesEnabled = was;
    return first;
}
}

TEST_CASE("a unit's first hit waits its card's load time", "[load_time]") {
    REQUIRE(CombatEntity::loadTimesEnabled);           // on by default
    REQUIRE(loadTicksForCard(0) == 7);                 // Knight: 700 ms
    REQUIRE(loadTicksForCard(10) == 14);               // Valkyrie: 1400 ms
    const int knightOff = ticksToFirstHit(0, false);
    const int knightOn = ticksToFirstHit(0, true);
    REQUIRE(knightOff > 0);
    REQUIRE(knightOn - knightOff == 7);
    REQUIRE(ticksToFirstHit(10, true) - ticksToFirstHit(10, false) == 14);
}

TEST_CASE("cards with no load time are unchanged", "[load_time]") {
    REQUIRE(loadTicksForCard(3) == 0);                 // Arrows: a spell
}
