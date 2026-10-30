#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "ClashEnv.h"
#include "LoadTimes.h"
#include <vector>

// The 50 ms engine tick (Tick.h, 2026-10-29): the engine runs the game client's logic tick, and the API keeps its
// 100 ms step. These pin both halves: timings land on the 50 ms grid, and everything the bindings see (the clock, the
// tick arguments and readings, the observation's scales) means what it meant at 100 ms.

namespace {
const std::vector<int> DECK = { 15, 6, 25, 40, 24, 72, 33, 7 };   // the 2.6 Hog Cycle
constexpr int KNIGHT = 0, DART_GOBLIN = 20;
}  // namespace

// ---------------- the grid ----------------

TEST_CASE("the engine tick is 50 ms; one API step is two of them", "[tick50]") {
    REQUIRE(TICK_MS == 50);
    REQUIRE(STEP_MS == 100);
    REQUIRE(TICKS_PER_STEP == 2);
    REQUIRE(msToTicks(1150) == 23);
    REQUIRE(msToTicks(1100) == 22);
    REQUIRE(msToTicks(3300) == 66);
    REQUIRE(secs(2.5) == 50);
    REQUIRE(secs(0.3) == 6);
    REQUIRE(ticksToSteps(23) == 11);
    REQUIRE(ticksToStepsCeil(23) == 12);
    REQUIRE(perTick(15.0f) == Catch::Approx(0.75f));
}

TEST_CASE("a 1.15 s hit speed lands hits 1.15 s apart, not 1.2 s", "[tick50]") {
    NoLoadTimes noLoad;
    Board board;
    auto attacker = std::make_shared<StationaryCombatant>(1, 5.0f, 5.0f, 1000, 0, 1.0f, 10, msToTicks(1150));
    auto target = std::make_shared<StationaryCombatant>(2, 5.0f, 6.0f, 100000, 1, 1.0f, 0, 10);
    spawn(board, attacker);
    spawn(board, target);
    std::vector<int> hits;
    for (int t = 1; t <= secs(5.0); ++t) {
        const int before = attacker->attackCount;
        attacker->update(board);
        if (attacker->attackCount > before) hits.push_back(t);
    }
    REQUIRE(hits.size() >= 3);
    for (size_t i = 1; i < hits.size(); ++i) REQUIRE((hits[i] - hits[i - 1]) * TICK_MS == 1150);
}

TEST_CASE("client load times keep their 50 ms: Dart Goblin's first hit is 350 ms, not 400", "[tick50][load_time]") {
    // client BlowdartGoblin LoadTime 450, HitSpeed 800: 100 ms ticks rounded 350 to 400 and 450 to 500.
    REQUIRE(loadMsForCard(DART_GOBLIN) == 450);
    REQUIRE(loadTicksForCard(DART_GOBLIN) * TICK_MS == 450);
    REQUIRE(firstHitTicksForCard(DART_GOBLIN) * TICK_MS == 350);
}

TEST_CASE("a client-cycle Knight in reach lands its first hit 0.5 s after it starts, on the 50 ms grid",
          "[tick50][client_cycle]") {
    Board board;
    CardRegistry::getInstance().getCard(KNIGHT)->spawnEntity(9.0f, 5.0f, 0, board);
    board.commitPendingEntities();
    auto knight = std::dynamic_pointer_cast<CombatEntity>(board.getEntities().back());
    knight->deployTicksRemaining = 0;
    auto target = std::make_shared<DummyEntity>(100, 9.0f, 6.5f, 100000, 1);
    spawn(board, target);
    REQUIRE(knight->clientCycleOn());
    int start = -1, hit = -1;
    for (int t = 1; t <= secs(2.0) && hit < 0; ++t) {
        knight->update(board);
        if (start < 0 && knight->attacking) start = t;
        if (target->hp < 100000) hit = t;
    }
    REQUIRE(start == 1);
    REQUIRE((hit - start) * TICK_MS == 1200 - 700);   // HitSpeed - LoadTime, a whole number of 50 ms ticks
}

TEST_CASE("the King's 3.3 s activation is 66 ticks, not 33 rounded", "[tick50][king]") {
    Board board;
    auto king = std::make_shared<Tower>(1, 9.0f, 29.0f, 4824, 0, 7.0f, 109, secs(1.0), 'R');
    king->sleep();
    spawn(board, king);
    king->update(board);
    king->takeDamage(1);
    int ticks = 0;
    while (!king->isAwake() && ticks < secs(10.0)) { king->update(board); ++ticks; }
    REQUIRE(ticks * TICK_MS == 3300 + TICK_MS);   // the trigger tick, then 3.3 s
}

// ---------------- the API keeps 100 ms ----------------

TEST_CASE("one env step frame is 100 ms: two engine ticks, one API tick", "[tick50][api]") {
    ClashEnv env(DECK, DECK, 3600);
    env.seed(7);
    REQUIRE(env.getCurrentTick() == 0);
    env.setElixirForTeam(0, 5.0f);
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
    REQUIRE(env.getCurrentTick() == 1);
    REQUIRE(env.debugGame().getCurrentTick() == TICKS_PER_STEP);
    REQUIRE(env.getElixirForTeam(0) == Catch::Approx(5.0f + 0.035f).margin(1e-5));   // 0.035 elixir per 100 ms
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 10);
    REQUIRE(env.getCurrentTick() == 11);
    REQUIRE(env.debugGame().getCurrentTick() == 11 * TICKS_PER_STEP);
}

TEST_CASE("a troop covers one 100 ms step's distance per frame", "[tick50][api]") {
    ClashEnv env(DECK, DECK, 3600);
    env.seed(7);
    env.inject(KNIGHT, 3.0f, 8.0f, 0, -1.0f, 0);   // own Knight, walking at once
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);   // commit
    auto knightAt = [&]() {
        for (const auto& u : env.getUnits()) if (u.cardId == KNIGHT && u.team == 0) return Vector2D{ u.x, u.y };
        return Vector2D{ -1.0f, -1.0f };
    };
    const Vector2D p0 = knightAt();
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
    // SPEED_MEDIUM: 60 tiles a minute in the client's units, 0.2 x that per 100 ms in engine tiles
    REQUIRE(knightAt().distanceTo(p0) == Catch::Approx(SPEED_MEDIUM * 0.2f).epsilon(1e-3));
}

TEST_CASE("tick arguments and readings are in 100 ms steps", "[tick50][api]") {
    ClashEnv env(DECK, DECK, 3600);
    env.seed(7);
    // inject's deploy_ticks: steps
    env.inject(KNIGHT, 3.0f, 8.0f, 0, -1.0f, 4);
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);   // commits it, then 100 ms of deploy
    int deploy = -1;
    for (const auto& u : env.getUnits()) if (u.cardId == KNIGHT && u.team == 0) deploy = u.deployTicks;
    REQUIRE(deploy == 3);
    // a fresh body: the default 1.0 s deploy reads 10
    env.inject(KNIGHT, 14.0f, 8.0f, 0);
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
    int fresh = -1;
    for (const auto& u : env.getUnits()) if (u.cardId == KNIGHT && u.team == 0 && u.x > 10.0f) fresh = u.deployTicks;
    REQUIRE(fresh == 9);
    // set_current_tick / get_current_tick: steps on both clocks
    env.setCurrentTick(1200);
    REQUIRE(env.getCurrentTick() == 1200);
    REQUIRE(env.getElixirMultiplier() == 2.0f);   // 2:00
    env.setCurrentTick(1199);
    REQUIRE(env.getElixirMultiplier() == 1.0f);
    REQUIRE(GameManager::elixirMultiplierAtStep(GameManager::DOUBLE_ELIXIR_STEP) == 2.0f);
    REQUIRE(GameManager::DOUBLE_ELIXIR_STEP == 1200);
    REQUIRE(GameManager::TRIPLE_ELIXIR_STEP == 2400);
    // note_played_card / get_last_played_tick: steps
    env.setCurrentTick(300);
    env.notePlayedCard(1, 15);
    REQUIRE(env.getLastPlayedTick(1, 15) == 300);
    // set_tower_stats' cooldown: steps (8 = 0.8 s)
    REQUIRE(env.setTowerStats(1, 1, 2534, 90, 8));
    REQUIRE(env.debugGame().findTower(1, 1)->getAttackCooldown() == secs(0.8));
}

TEST_CASE("the observation's attribute channels keep their per-100 ms scale", "[tick50][observation]") {
    ClashEnv env(DECK, DECK, 3600);
    env.seed(7);
    env.inject(KNIGHT, 3.0f, 8.0f, 0, -1.0f, 0);
    env.stepSelfPlayFast(-1, 0, 0, -1, 0, 0, 1);
    const auto obs = env.getObservationForTeam(0);
    int x = -1, y = -1;
    for (const auto& u : env.getUnits())
        if (u.cardId == KNIGHT && u.team == 0) { x = static_cast<int>(u.x); y = static_cast<int>(u.y); }
    REQUIRE(x >= 0);
    auto at = [&](int ch) { return obs[ch * ClashEnv::BOARD_HEIGHT * ClashEnv::BOARD_WIDTH + y * ClashEnv::BOARD_WIDTH + x]; };
    // speed: tiles per 100 ms; DPS: damage per 100 ms (Knight 202 every 1.2 s)
    REQUIRE(at(ClashEnv::CH_SPEED) == Catch::Approx(SPEED_MEDIUM * 0.2f / ClashEnv::MAX_UNIT_SPEED));
    REQUIRE(at(ClashEnv::CH_DPS) == Catch::Approx(202.0f / 12.0f / ClashEnv::MAX_UNIT_DPS));
    // the clock scalar: API steps over max_ticks
    REQUIRE(obs[ClashEnv::EXTRA_SCALARS_START] == Catch::Approx(env.getCurrentTick() / 3600.0f));
    REQUIRE(ClashEnv::CYCLE_RECENCY_TAU_TICKS == 200.0f);   // 20 s in API steps
}

// ---------------- client collision, one pass a tick ----------------

TEST_CASE("client collision: one pass an engine tick, each capped at 0.15", "[tick50][client_collision]") {
    struct On { bool was = Fidelity::clientCollision; On() { Fidelity::clientCollision = true; } ~On() { Fidelity::clientCollision = was; } } on;
    GameManager game(DECK, DECK);
    Board& board = game.getBoard();
    auto place = [&](float x, float y, int team) {
        const size_t before = board.pendingEntityCount();
        CardRegistry::getInstance().getCard(KNIGHT)->spawnEntity(x, y, team, board);
        auto e = std::dynamic_pointer_cast<CombatEntity>(board.getPendingEntity(before));
        e->deployTicksRemaining = 0;
        board.commitPendingEntities();
        return e;
    };
    auto a = place(8.5f, 10.0f, 0);
    auto b = place(8.55f, 10.0f, 1);
    float sep = a->position.distanceTo(b->position);
    for (int t = 0; t < 6; ++t) {
        game.tick();
        const float now = a->position.distanceTo(b->position);
        REQUIRE(now - sep <= 2 * Board::CLIENT_MAX_PUSH + 0.01f);   // each body's one pass this tick
        sep = now;
    }
}
