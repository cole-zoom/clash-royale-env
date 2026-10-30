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

// Carried units (the client's SpawnAttach, goblingiant.toml / ram_rider.toml; the Goblin Machine's rocket launcher by
// the same rule): they ride their carrier, can't be targeted or hit, don't push, and die with it. They used to be
// separate units that walked on their own at speed 0.5 with their own HP (the Ram Rider's 1766, the turret's 2150).

namespace {

constexpr int GOBLIN_MACHINE = 55;
constexpr int RAM_RIDER = 87;
constexpr int GOBLIN_GIANT = 88;

struct Match {
    GameManager game;

    Match() : game(std::vector<int>{ GOBLIN_MACHINE, RAM_RIDER, GOBLIN_GIANT, 0, 1, 2, 3, 4 },
                   std::vector<int>{ GOBLIN_MACHINE, RAM_RIDER, GOBLIN_GIANT, 0, 1, 2, 3, 4 }) {
        game.seed(1);
        game.reset();
        for (const auto& e : game.getBoard().getEntities())
            if (auto t = std::dynamic_pointer_cast<Tower>(e)) t->sleep();
    }
    Board& board() { return game.getBoard(); }
    void step(int n = 1) { for (int i = 0; i < n; ++i) game.step(); }
    std::vector<std::shared_ptr<Entity>> spawnCard(int cardId, float x, float y, int team) {
        std::set<int> before;
        for (const auto& e : board().getEntities()) before.insert(e->id);
        CardRegistry::getInstance().getCard(cardId)->spawnEntity(x, y, team, board());
        board().commitPendingEntities();
        std::vector<std::shared_ptr<Entity>> out;
        for (const auto& e : board().getEntities())
            if (!before.count(e->id)) out.push_back(e);
        return out;
    }
    std::vector<std::shared_ptr<Entity>> riders(const std::vector<std::shared_ptr<Entity>>& es) {
        std::vector<std::shared_ptr<Entity>> out;
        for (const auto& e : es) if (e->attachedToId >= 0) out.push_back(e);
        return out;
    }
    int alive(const std::string& name, int team) {
        int n = 0;
        for (const auto& e : board().getEntities())
            if (e->isAlive() && e->team == team && e->name == name) ++n;
        return n;
    }
};

} // namespace

TEST_CASE("each carrier card spawns its second unit attached to the first", "[carried][regression]") {
    struct Case { int card; int riders; };
    for (const Case c : { Case{ RAM_RIDER, 1 }, Case{ GOBLIN_GIANT, 2 }, Case{ GOBLIN_MACHINE, 1 } }) {
        Match m;
        auto es = m.spawnCard(c.card, 3.0f, 10.0f, 0);
        auto rs = m.riders(es);
        INFO("card " << c.card);
        REQUIRE(static_cast<int>(rs.size()) == c.riders);
        REQUIRE(es.front()->attachedToId < 0);  // the carrier itself rides nothing
        for (const auto& r : rs) {
            REQUIRE(r->attachedToId == es.front()->id);
            REQUIRE_FALSE(r->isTargetable());
            REQUIRE_FALSE(r->isAreaHittable());
        }
    }
}

TEST_CASE("a carried unit stays in its seat while its carrier walks and crosses the river", "[carried][regression]") {
    Match m;
    auto es = m.spawnCard(GOBLIN_GIANT, 3.0f, 10.0f, 0);
    auto giant = es.front();
    auto rs = m.riders(es);
    std::vector<Vector2D> seats;
    for (const auto& r : rs) seats.push_back(r->attachOffset);
    for (int s = 0; s < 120; ++s) {  // 12 s: over the bridge into the far half
        m.step();
        for (size_t i = 0; i < rs.size(); ++i) {
            REQUIRE(rs[i]->position.x == Catch::Approx(giant->position.x + seats[i].x).margin(1e-4));
            REQUIRE(rs[i]->position.y == Catch::Approx(giant->position.y + seats[i].y).margin(1e-4));
        }
    }
    REQUIRE(giant->position.y > 18.0f);  // it did cross
}

TEST_CASE("a Ram Rider's rider dies with the Ram and leaves nothing", "[carried][regression]") {
    Match m;
    auto es = m.spawnCard(RAM_RIDER, 3.0f, 10.0f, 0);
    m.step(15);
    REQUIRE(m.alive("Ram Rider", 0) == 2);
    es.front()->hp = 0;
    m.step(2);
    REQUIRE(m.alive("Ram Rider", 0) == 0);
}

TEST_CASE("a Goblin Machine's launcher dies with the machine", "[carried][regression]") {
    Match m;
    auto es = m.spawnCard(GOBLIN_MACHINE, 3.0f, 10.0f, 0);
    m.step(15);
    REQUIRE(m.alive("Goblin Machine", 0) == 2);
    es.front()->hp = 0;
    m.step(2);
    REQUIRE(m.alive("Goblin Machine", 0) == 0);
}

TEST_CASE("Goblin Giant's Spear Goblins drop off as ordinary ones when it dies", "[carried][regression]") {
    Match m;
    auto es = m.spawnCard(GOBLIN_GIANT, 3.0f, 10.0f, 0);
    m.step(15);
    es.front()->hp = 0;
    m.step(2);
    int dropped = 0;
    for (const auto& e : m.board().getEntities()) {
        if (!e->isAlive() || e->team != 0 || e->name != "Spear Goblins") continue;
        ++dropped;
        REQUIRE(e->attachedToId < 0);
        REQUIRE(e->isTargetable());
    }
    REQUIRE(dropped == 2);
}

TEST_CASE("a carried unit still shoots", "[carried][regression]") {
    Match m;
    m.spawnCard(RAM_RIDER, 3.0f, 10.0f, 0);
    auto d = std::make_shared<StationaryCombatant>(m.board().allocateId(), 6.0f, 13.0f, 1000000, 1, 0.5f, 0, 1000);
    spawn(m.board(), d);
    m.step(40);
    REQUIRE(d->hp < 1000000);
}
