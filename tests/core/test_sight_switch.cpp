#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "ClashEnv.h"

// The edge-to-edge calibration switch widens sight by both radii and is off
// by default (the strict centre-to-centre rule test_sight_range.cpp pins).
TEST_CASE("sightEdgeToEdge adds both radii to sight, and defaults off", "[sight][calibration]") {
    REQUIRE_FALSE(CombatEntity::sightEdgeToEdge);
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    ClashEnv env(deck, deck);
    env.reset();
    env.inject(0, 9.0f, 8.0f, 0, -1.0f, 0);    // Knight, team 0
    env.inject(0, 9.0f, 14.2f, 1, -1.0f, 0);   // Knight, team 1: 6.2 apart, past 5.5 sight, inside 5.5 + radii
    ClashEnv strict = env;
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
    auto locked = [](const ClashEnv& e) {
        for (const auto& u : e.getUnits()) if (u.cardId == 0 && u.team == 0) return u.targetId;
        return -2;
    };
    int strictTarget = locked(env);
    CombatEntity::sightEdgeToEdge = true;
    strict.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
    int edgeTarget = locked(strict);
    CombatEntity::sightEdgeToEdge = false;
    int enemyKnight = -1;
    for (const auto& u : strict.getUnits()) if (u.cardId == 0 && u.team == 1) enemyKnight = u.id;
    REQUIRE(edgeTarget == enemyKnight);
    REQUIRE(strictTarget != enemyKnight);
}
