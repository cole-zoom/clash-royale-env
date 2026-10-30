#include <catch_amalgamated.hpp>
#include "ArenaLayout.h"
#include "Board.h"
#include "GameManager.h"
#include "FidelitySwitches.h"
#include <vector>

// The pocket (Cole, 2026-10-30: "Once you take a tower, you can place higher up"; "pocket = 4 tiles past the river"):
// with an enemy Princess Tower down, that lane's half of their side opens from the river to 4 tiles past it.

namespace {

void killPrincess(GameManager& game, int team, bool left) {
    for (const auto& e : game.getBoard().getEntities())
        if (e->isTower() && e->team == team && e->symbol != 'R' && ArenaLayout::isLeftLane(e->position.x) == left)
            e->takeDamage(e->hp);
    game.getBoard().cleanDeadEntities(0);
}

struct Restore { bool was = Fidelity::pocketDeploy; ~Restore() { Fidelity::pocketDeploy = was; } };

} // namespace

TEST_CASE("taking a Princess Tower opens its lane's pocket, 4 tiles past the river", "[placement][pocket][regression]") {
    Restore r;
    Fidelity::pocketDeploy = true;
    GameManager game(std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 }, std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 });
    game.seed(1);
    game.reset();
    const float R = 0.5f;  // a small troop's footprint
    REQUIRE_FALSE(game.isValidPlacement(0, 3.0f, 18.0f, false, R));  // nothing down: own half only
    killPrincess(game, 1, true);                                      // their left Princess falls
    for (float y : { 18.0f, 19.0f, 20.0f, 21.0f }) REQUIRE(game.isValidPlacement(0, 3.0f, y, false, R));
    REQUIRE(game.isValidPlacement(0, 8.0f, 20.0f, false, R));        // the whole left half
    REQUIRE_FALSE(game.isValidPlacement(0, 3.0f, 22.0f, false, R));  // 5 past the river: no
    REQUIRE_FALSE(game.isValidPlacement(0, 9.0f, 19.0f, false, R));  // the right lane: its tower stands
    REQUIRE_FALSE(game.isValidPlacement(0, 3.0f, 16.5f, false, R));  // the river itself: no
    REQUIRE_FALSE(game.isValidPlacement(1, 3.0f, 15.0f, false, R));  // theirs: our towers all stand
    killPrincess(game, 0, false);                                     // our right Princess falls: theirs opens, mirrored
    for (float y : { 15.0f, 12.0f }) REQUIRE(game.isValidPlacement(1, 14.0f, y, false, R));
    REQUIRE_FALSE(game.isValidPlacement(1, 14.0f, 11.0f, false, R));
    Fidelity::pocketDeploy = false;
    REQUIRE_FALSE(game.isValidPlacement(0, 3.0f, 19.0f, false, R));
}
