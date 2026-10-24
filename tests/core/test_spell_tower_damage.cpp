#include <catch_amalgamated.hpp>
#include "ClashEnv.h"

// Spells hit Crown Towers for their published fraction (SpellTowerDamage.h): Fireball 30%, Rocket 25%, The Log 20%,
// and troops in the same blast still take the full damage.
namespace {
int towerLoss(int spellId, float x, float y) {
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    ClashEnv env(deck, deck);
    env.reset();
    const int before = env.getTowerHp(1, 1);
    env.inject(spellId, x, y, 0);
    for (int t = 0; t < 40; ++t) env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
    return before - env.getTowerHp(1, 1);
}
}

TEST_CASE("Fireball and Rocket deal their crown-tower fraction to a princess tower", "[spell][tower]") {
    const int fireball = towerLoss(7, 3.0f, 27.0f);   // centred on team 1's left princess tower
    REQUIRE(fireball == Catch::Approx(689 * 0.30f).margin(1));
    const int rocket = towerLoss(30, 3.0f, 27.0f);
    REQUIRE(rocket == Catch::Approx(1485 * 0.25f).margin(1));
}

TEST_CASE("Poison keeps its own crown-tower value", "[spell][tower]") {
    const CardStats* poison = CardRegistry::getInstance().getStats(32);
    REQUIRE(poison != nullptr);
    REQUIRE(poison->spellCrownTowerDamage == 21);
}

TEST_CASE("The Miner hits a Crown Tower for 25% of its damage", "[spell][tower]") {
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7};
    ClashEnv env(deck, deck);
    env.reset();
    const int before = env.getTowerHp(1, 1);
    env.inject(52, 3.0f, 25.0f, 0, -1.0f, 0);  // Miner (team 0) right next to team 1's left princess tower
    int first = -1;
    for (int t = 0; t < 40 && first < 0; ++t) {
        env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
        if (env.getTowerHp(1, 1) < before) first = before - env.getTowerHp(1, 1);
    }
    REQUIRE(first == Catch::Approx(194 * 0.25f).margin(1));
}
