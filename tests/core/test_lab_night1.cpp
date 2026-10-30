#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "Board.h"
#include "GameManager.h"
#include "CardRegistry.h"
#include "CombatEntity.h"
#include "Tower.h"
#include <memory>
#include <set>
#include <vector>

// Lab night 1 (bot-77 decisions.md, 2026-10-29/30): scripted friendly battles recorded from the game and compared
// with the engine. Each case pins a behaviour the replays measured.

namespace {

constexpr int GOBLIN_HUT = 95;

struct Match {
    GameManager game;

    Match() : game(std::vector<int>{ GOBLIN_HUT, 0, 1, 2, 3, 4, 5, 6 }, std::vector<int>{ GOBLIN_HUT, 0, 1, 2, 3, 4, 5, 6 }) {
        game.seed(1);
        game.reset();
        for (const auto& e : game.getBoard().getEntities())
            if (auto t = std::dynamic_pointer_cast<Tower>(e)) t->sleep();
    }
    Board& board() { return game.getBoard(); }
    void step(int n = 1) { for (int i = 0; i < n; ++i) game.step(); }
    std::shared_ptr<Entity> hut(float x, float y, int team) {
        CardRegistry::getInstance().getCard(GOBLIN_HUT)->spawnEntity(x, y, team, board());
        board().commitPendingEntities();
        for (const auto& e : board().getEntities())
            if (e->name == "Goblin Hut" && e->team == team) return e;
        return nullptr;
    }
    // A harmless, immobile troop (not a building) with plenty of HP.
    std::shared_ptr<StationaryCombatant> dummy(float x, float y, int team) {
        auto d = std::make_shared<StationaryCombatant>(board().allocateId(), x, y, 1000000, team, 0.5f, 0, 1000);
        spawn(board(), d);
        return d;
    }
    int goblins(int team) {
        int n = 0;
        for (const auto& e : board().getEntities())
            if (e->isAlive() && e->team == team && e->name == "Spear Goblins") ++n;
        return n;
    }
};

} // namespace

// Replays r44 / r54 / r55: the first Spear Goblin shows 0.7-1.2 s after the Hut lands, with the Hog in range.
TEST_CASE("a Goblin Hut with an enemy in range spawns as its deploy ends, then every 2.2 s", "[lab][goblin_hut][regression]") {
    Match m;
    m.dummy(9.0f, 12.0f, 1);
    m.hut(9.0f, 9.0f, 0);
    int first = -1;
    for (int s = 1; s <= 60 && first < 0; ++s) {
        m.step();
        if (m.goblins(0) > 0) first = s;
    }
    REQUIRE(first >= 10);  // not before the Hut's 1 s deploy
    REQUIRE(first <= 11);  // the step its deploy ends
    m.step(21);            // 2.1 s on: the second is still to come
    REQUIRE(m.goblins(0) == 1);
    m.step(2);             // 2.2 s after the first
    REQUIRE(m.goblins(0) == 2);
}

// r54: the Hog was 6.8 tiles from the Hut's centre as its deploy ended, and it still woke: Range 6 is edge to edge.
TEST_CASE("a Goblin Hut's range is edge to edge", "[lab][goblin_hut][regression]") {
    Match m;
    m.dummy(9.0f, 15.8f, 1);  // 6.8 from the centre; inside 6 + the Hut's radius + the dummy's
    m.hut(9.0f, 9.0f, 0);
    m.step(12);
    REQUIRE(m.goblins(0) == 1);
}

TEST_CASE("a Goblin Hut stays asleep for an enemy just past its edge-to-edge range", "[lab][goblin_hut][regression]") {
    Match m;
    auto hut = m.hut(9.0f, 9.0f, 0);
    auto d = m.dummy(9.0f, 9.0f, 1);
    d->position.y = 9.0f + 6.0f + bodyRadiusOf(*hut) + bodyRadiusOf(*d) + 0.1f;
    m.step(40);
    REQUIRE(m.goblins(0) == 0);
}

// L09: the Spear Goblins appear on the Hog's side of the Hut.
TEST_CASE("a Goblin Hut's Spear Goblin appears toward the enemy that woke it", "[lab][goblin_hut][regression]") {
    for (float ex : { 4.0f, 14.0f }) {
        Match m;
        m.dummy(ex, 13.0f, 1);
        m.hut(9.0f, 9.0f, 0);
        std::shared_ptr<Entity> g;
        for (int s = 0; s < 15 && !g; ++s) {
            m.step();
            for (const auto& e : m.board().getEntities())
                if (e->isAlive() && e->team == 0 && e->name == "Spear Goblins") g = e;
        }
        REQUIRE(g);
        INFO("enemy x " << ex << ", goblin at (" << g->position.x << ", " << g->position.y << ")");
        REQUIRE((g->position.x - 9.0f) * (ex - 9.0f) > 0.0f);  // on the enemy's side
        REQUIRE(g->position.y > 9.0f);                         // and forward of the Hut
    }
}

// The old reading ran a free 2.2 s timer and skipped a spawn when nobody was near at that tick: up to 30 s late.
TEST_CASE("a sleeping Goblin Hut spawns the moment an enemy comes in range", "[lab][goblin_hut][regression]") {
    Match m;
    m.hut(9.0f, 9.0f, 0);
    m.step(73);  // 7.3 s asleep: nothing
    REQUIRE(m.goblins(0) == 0);
    m.dummy(9.0f, 14.0f, 1);  // 5 tiles away: in range
    m.step();
    REQUIRE(m.goblins(0) == 1);
}

TEST_CASE("a Goblin Hut stops spawning when no enemy is left in range", "[lab][goblin_hut][regression]") {
    Match m;
    auto d = m.dummy(9.0f, 12.0f, 1);
    m.hut(9.0f, 9.0f, 0);
    m.step(12);
    REQUIRE(m.goblins(0) == 1);
    d->hp = 0;  // the enemy goes
    for (const auto& e : m.board().getEntities())
        if (e->team == 0 && e->name == "Spear Goblins") e->hp = 0;  // and the goblin, so it can't walk off and count
    m.step(60);
    REQUIRE(m.goblins(0) == 0);
}

TEST_CASE("an enemy tower doesn't wake a Goblin Hut", "[lab][goblin_hut][regression]") {
    Match m;
    // Right at the river, the closest legal spot to the enemy's princess tower (still farther than 6): no troop near.
    m.hut(3.5f, 15.0f, 0);
    m.step(60);
    REQUIRE(m.goblins(0) == 0);
}
