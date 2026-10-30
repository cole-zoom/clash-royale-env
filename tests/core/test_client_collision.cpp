#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "CardRegistry.h"
#include "GameManager.h"
#include "Troop.h"

// Unit collision as the client's code runs it (Fidelity::clientCollision; bot-77 docs/client-code-notes.md §5,
// separation 0x71fff6058c): each body pushes only itself, in entity order, overlap x neighbour Mass / own Mass,
// averaged and capped at 0.15 a 50 ms pass, one pass an engine tick (two a 100 ms game.step()); the river lets a push in one cell deep; JumpEnabled
// troops jump river cells at JumpSpeed without colliding.

namespace {
constexpr int KNIGHT = 0, GIANT = 2, MINI_PEKKA = 5, HOG = 15, BATTLE_RAM = 81;
const std::vector<int> DECK = { 0, 1, 2, 3, 4, 5, 6, 7 };
constexpr float CONV = REAL_TILES_PER_MIN_TO_ENGINE * MOVEMENT_SPEED_SCALE;  // client speed -> tiles per tick

template <bool* Flag>
struct SwitchTo {
    bool was = *Flag;
    explicit SwitchTo(bool on) { *Flag = on; }
    ~SwitchTo() { *Flag = was; }
};

// Spawns a card's first body for `team`, ready to act (no deploy second), and commits it.
std::shared_ptr<CombatEntity> place(Board& board, int cardId, float x, float y, int team = 0) {
    const size_t before = board.pendingEntityCount();
    CardRegistry::getInstance().getCard(cardId)->spawnEntity(x, y, team, board);
    auto e = std::dynamic_pointer_cast<CombatEntity>(board.getPendingEntity(before));
    for (size_t i = before; i < board.pendingEntityCount(); ++i) {
        if (auto* c = dynamic_cast<CombatEntity*>(board.getPendingEntity(i).get())) c->deployTicksRemaining = 0;
    }
    board.commitPendingEntities();
    return e;
}

// The Hog's distance walked in 3.9 s from (14.5, 8.0), with a Mini P.E.K.K.A 1.2 tiles ahead of it or alone.
// `pekkaFirst`: the Mini P.E.K.K.A is earlier in entity order.
float hogDistance(bool withPekka, bool pekkaFirst) {
    GameManager game(DECK, DECK);
    Board& board = game.getBoard();
    std::shared_ptr<CombatEntity> hog;
    if (withPekka && pekkaFirst) place(board, MINI_PEKKA, 14.5f, 9.2f);
    hog = place(board, HOG, 14.5f, 8.0f);
    if (withPekka && !pekkaFirst) place(board, MINI_PEKKA, 14.5f, 9.2f);
    const Vector2D start = hog->position;
    for (int t = 0; t < 39; ++t) game.step();
    return hog->position.distanceTo(start);
}
}  // namespace

TEST_CASE("client collision: a Hog behind a walking Mini P.E.K.K.A keeps its own pace", "[client_collision]") {
    SwitchTo<&Fidelity::clientCollision> on(true);
    const float alone = hogDistance(false, false);
    REQUIRE(alone > 9.0f);
    for (bool pekkaFirst : { true, false }) {
        CAPTURE(pekkaFirst);
        REQUIRE(hogDistance(true, pekkaFirst) == Catch::Approx(alone).epsilon(0.02));
    }
}

TEST_CASE("client collision off: the engine's pairwise split still slows the Hog (unchanged)", "[client_collision]") {
    SwitchTo<&Fidelity::clientCollision> off(false);
    const float alone = hogDistance(false, false);
    REQUIRE(hogDistance(true, false) < alone * 0.97f);
}

TEST_CASE("client collision: an equal-mass pair separates by at most the cap a pass", "[client_collision]") {
    SwitchTo<&Fidelity::clientCollision> on(true);
    Board board;
    auto a = place(board, KNIGHT, 8.0f, 8.0f, 0);
    auto b = place(board, KNIGHT, 8.05f, 8.0f, 1);   // head-on, 0.95 overlapping (radii 0.5)
    board.clientSelfPush(*a);
    REQUIRE(a->position.x == Catch::Approx(8.0f - Board::CLIENT_MAX_PUSH));   // capped, and only itself
    REQUIRE(b->position.x == Catch::Approx(8.05f));
    board.clientSelfPush(*b);
    REQUIRE(b->position.x == Catch::Approx(8.05f + Board::CLIENT_MAX_PUSH));
    REQUIRE(b->position.x - a->position.x == Catch::Approx(0.05f + 2 * Board::CLIENT_MAX_PUSH));
    REQUIRE(a->position.y == Catch::Approx(8.0f));   // no sideways nudge

    // Through GameManager: one 100 ms step is two ticks, each a pass for each body, so a pair standing on each other
    // gains at most 4 x 0.15. Enemies,
    // so they stand and fight rather than walk apart.
    GameManager game(DECK, DECK);
    Board& gb = game.getBoard();
    auto c = place(gb, KNIGHT, 8.5f, 10.0f, 0);   // clear of every building
    auto d = place(gb, KNIGHT, 8.55f, 10.0f, 1);
    float sep = c->position.distanceTo(d->position);
    for (int t = 0; t < 4; ++t) {
        game.step();
        const float now = c->position.distanceTo(d->position);
        REQUIRE(now - sep <= 4 * Board::CLIENT_MAX_PUSH + 0.01f);
        sep = now;
    }
    REQUIRE(sep >= 1.0f);   // and they do come apart
}

TEST_CASE("client collision: the lighter body moves by the Mass ratio, the heavier hardly at all", "[client_collision]") {
    SwitchTo<&Fidelity::clientCollision> on(true);
    Board board;
    auto giant = place(board, GIANT, 8.0f, 8.0f);   // radius 0.75, Mass 18
    auto knight = place(board, KNIGHT, 9.22f, 8.0f);  // radius 0.5, Mass 6: overlap 0.03
    REQUIRE(giant->mass == 18);
    REQUIRE(knight->mass == 6);
    board.clientSelfPush(*giant);
    board.clientSelfPush(*knight);
    const float giantMoved = 8.0f - giant->position.x;
    const float knightMoved = knight->position.x - 9.22f;
    REQUIRE(giantMoved == Catch::Approx(0.03f * 6.0f / 18.0f + 0.001f).margin(1e-4));
    // Sequential: the Knight sees the overlap the Giant's own push left.
    REQUIRE(knightMoved == Catch::Approx((0.03f - giantMoved) * 18.0f / 6.0f + 0.001f).margin(1e-4));
    REQUIRE(knightMoved > 5.0f * giantMoved);
}

TEST_CASE("client collision: a push enters the first river cell and no further", "[client_collision][river]") {
    SwitchTo<&Fidelity::clientCollision> on(true);
    Board board;
    // The per-cell clamp: from the first river row, a move deeper stops at its border; a nudge comes with a push.
    REQUIRE(board.isRiverCellAt(8.0f, 15.7f));
    REQUIRE_FALSE(board.isRiverCellAt(14.5f, 16.5f));   // bridge
    REQUIRE_FALSE(board.isRiverCellAt(8.0f, 15.5f));    // the bank line is dry
    Vector2D m = board.clientRiverMove({ 8.0f, 15.7f }, { 8.2f, 16.1f }, false);
    REQUIRE(m.x == Catch::Approx(8.2f));
    REQUIRE(m.y < 16.0f);
    REQUIRE(m.y > 15.99f);
    m = board.clientRiverMove({ 8.0f, 15.7f }, { 8.0f, 15.75f }, true);
    REQUIRE(m.y == Catch::Approx(15.75f - Board::CLIENT_RIVER_NUDGE));
    m = board.clientRiverMove({ 8.0f, 15.2f }, { 8.0f, 15.35f }, true);   // from dry: not clamped
    REQUIRE(m.y == Catch::Approx(15.35f));

    // A crowd on the bank, Giants shoving Knights and Mini P.E.K.K.As toward the water: nobody ends a tick deeper
    // than the first river cell, and some do get pushed into it.
    GameManager game(DECK, DECK);
    Board& gb = game.getBoard();
    std::vector<std::shared_ptr<CombatEntity>> crowd;
    for (int i = 0; i < 4; ++i) {
        crowd.push_back(place(gb, KNIGHT, 6.5f + i * 0.9f, 15.3f));
        crowd.push_back(place(gb, MINI_PEKKA, 6.9f + i * 0.9f, 15.1f));
        crowd.push_back(place(gb, GIANT, 6.7f + i * 0.9f, 14.2f));
    }
    bool anyInRiver = false;
    for (int t = 0; t < 80; ++t) {
        game.step();
        for (const auto& u : crowd) {
            if (!u->isAlive() || !gb.isRiverCellAt(u->position.x, u->position.y)) continue;
            anyInRiver = true;
            CAPTURE(t, u->name, u->position.x, u->position.y);
            const bool firstRow = u->position.y < 16.0f || u->position.y > 17.0f;
            const bool besideBridge = !gb.isRiverCellAt(u->position.x + Board::CLIENT_RIVER_CELL, u->position.y)
                || !gb.isRiverCellAt(u->position.x - Board::CLIENT_RIVER_CELL, u->position.y);
            REQUIRE((firstRow || besideBridge));
        }
    }
    REQUIRE(anyInRiver);
}

TEST_CASE("client collision: a Hog jumps river cells at JumpSpeed and collides with nothing meanwhile", "[client_collision][jump]") {
    SwitchTo<&Fidelity::clientCollision> on(true);
    GameManager game(DECK, DECK);
    Board& board = game.getBoard();
    auto hog = place(board, HOG, 8.5f, 14.0f);   // centre lane: straight over the water
    REQUIRE(hog->jumpSpeed == Catch::Approx(160 * CONV));
    auto troop = std::dynamic_pointer_cast<Troop>(hog);
    REQUIRE(troop->getSpeed() == Catch::Approx(120 * CONV));
    int jumped = 0;
    for (int t = 0; t < 40; ++t) {
        const Vector2D before = hog->position;
        const bool startsOver = board.isRiverCellAt(before.x, before.y);
        game.step();
        if (startsOver && board.isRiverCellAt(hog->position.x, hog->position.y)) {
            ++jumped;
            // game.step() is 100 ms: TICKS_PER_STEP ticks of the jump
            REQUIRE(hog->position.distanceTo(before) == Catch::Approx(hog->jumpSpeed * TICKS_PER_STEP).epsilon(0.01));
            REQUIRE_FALSE(hog->collidesAsBody(board));
        }
    }
    REQUIRE(jumped >= 2);
    // Other riverIgnores troops are not JumpEnabled.
    auto ghost = place(board, 47, 3.0f, 5.0f);   // Royal Ghost
    REQUIRE(ghost->jumpSpeed == 0.0f);
}

TEST_CASE("client collision: a Battle Ram and a Mini P.E.K.K.A placed together both cross the bridge", "[client_collision][bridge]") {
    SwitchTo<&Fidelity::clientCollision> on(true);
    for (float bridgeX : { ArenaLayout::LEFT_BRIDGE_X, ArenaLayout::RIGHT_BRIDGE_X }) {
        for (float dx : { 0.0f, 0.6f, -0.6f }) {
            for (bool ramFirst : { true, false }) {
                CAPTURE(bridgeX, dx, ramFirst);
                GameManager game(DECK, DECK);
                Board& board = game.getBoard();
                std::shared_ptr<CombatEntity> ram, pekka;
                if (ramFirst) ram = place(board, BATTLE_RAM, bridgeX, 12.0f);
                pekka = place(board, MINI_PEKKA, bridgeX + dx, 11.0f);
                if (!ramFirst) ram = place(board, BATTLE_RAM, bridgeX, 12.0f);
                int ramAt = -1, pekkaAt = -1;
                for (int t = 0; t < 100 && (ramAt < 0 || pekkaAt < 0); ++t) {
                    game.step();
                    if (ramAt < 0 && ram->position.y >= board.getRiverEnd()) ramAt = t;
                    if (pekkaAt < 0 && pekka->position.y >= board.getRiverEnd()) pekkaAt = t;
                }
                REQUIRE(ramAt >= 0);     // within 10 s
                REQUIRE(pekkaAt >= 0);
            }
        }
    }
}
