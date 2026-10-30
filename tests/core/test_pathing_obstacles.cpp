#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "Board.h"
#include "GameManager.h"
#include "CardRegistry.h"
#include "FidelitySwitches.h"
#include "Tower.h"
#include <memory>
#include <set>
#include <vector>

// Pathing around what a unit isn't going to (bot-77 decisions.md, 2026-10-30, Cole's play page): a Giant dropped behind
// its King Tower crawled along it for ~5 s, and two Giants meeting on a bridge stopped there for good. The client routes
// over tiles (globals.csv PATHFINDING_*: a building tile costs 50 to a tile's 8); Fidelity::obstacleSlide and
// Fidelity::bridgeOwnLine are the engine's stand-ins.

namespace {

constexpr int GIANT = 2;

struct Match {
    GameManager game;

    Match() : game(std::vector<int>{ GIANT, 0, 1, 3, 4, 5, 6, 7 }, std::vector<int>{ GIANT, 0, 1, 3, 4, 5, 6, 7 }) {
        game.seed(1);
        game.reset();
        for (const auto& e : game.getBoard().getEntities())
            if (auto t = std::dynamic_pointer_cast<Tower>(e)) t->sleep();
    }
    Board& board() { return game.getBoard(); }
    void step(int n = 1) { for (int i = 0; i < n; ++i) game.step(); }
    std::shared_ptr<Entity> giant(float x, float y, int team) {
        std::set<int> before;
        for (const auto& e : board().getEntities()) before.insert(e->id);
        CardRegistry::getInstance().getCard(GIANT)->spawnEntity(x, y, team, board());
        board().commitPendingEntities();
        for (const auto& e : board().getEntities())
            if (!before.count(e->id)) return e;
        return nullptr;
    }
};

struct Switches {
    bool cc = Fidelity::clientCollision, slide = Fidelity::obstacleSlide, line = Fidelity::bridgeOwnLine;
    ~Switches() { Fidelity::clientCollision = cc; Fidelity::obstacleSlide = slide; Fidelity::bridgeOwnLine = line; }
};

} // namespace

TEST_CASE("a Giant dropped behind its King Tower walks around it at its speed", "[pathing][regression]") {
    Switches restore;
    Fidelity::clientCollision = true;
    for (bool slide : { true, false }) {
        Fidelity::obstacleSlide = slide;
        Match m;
        auto g = m.giant(9.0f, 0.0f, 0);
        m.step(10);  // the deploy
        const Vector2D start = g->position;
        m.step(30);  // 3 s
        INFO("slide " << slide << ", at (" << g->position.x << ", " << g->position.y << ")");
        // the Giant walks ~0.75 tiles/s: 2+ tiles in 3 s round the tower; crawling along it, well under 1
        REQUIRE((g->position.distanceTo(start) > 2.0f) == slide);
    }
}

TEST_CASE("two Giants meeting on a bridge pass each other", "[pathing][regression]") {
    Switches restore;
    Fidelity::clientCollision = true;
    Fidelity::obstacleSlide = true;
    Fidelity::bridgeOwnLine = true;
    for (float offset : { 0.0f, 0.3f, 0.76f }) {  // dead on, a little apart, the play page's case
        Match m;
        auto ours = m.giant(2.5f + offset / 2.0f, 15.0f, 0);
        auto theirs = m.giant(2.5f - offset / 2.0f, 19.0f, 1);
        m.step(10);  // the deploy
        m.step(60);  // 6 s: ~4.5 tiles each; the gap was 4
        INFO("offset " << offset << ": ours (" << ours->position.x << ", " << ours->position.y << "), theirs ("
                       << theirs->position.x << ", " << theirs->position.y << ")");
        REQUIRE(ours->position.y > 19.0f);
        REQUIRE(theirs->position.y < 15.0f);
    }
}

TEST_CASE("without the pathing switches two Giants dead on stop on the bridge", "[pathing][regression]") {
    Switches restore;
    Fidelity::clientCollision = true;
    Fidelity::obstacleSlide = false;
    Fidelity::bridgeOwnLine = false;
    Match m;
    auto ours = m.giant(2.5f, 15.0f, 0);
    auto theirs = m.giant(2.5f, 19.0f, 1);
    m.step(70);
    REQUIRE(ours->position.y < 17.5f);
    REQUIRE(theirs->position.y > 15.5f);
}
