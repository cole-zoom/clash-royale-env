#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include "ArenaLayout.h"
#include "Entity.h"
#include "StatsEventBus.h"
#include "Tick.h"

class Board {
private:
    int width, height;
    std::vector<std::shared_ptr<Entity>> activeEntities;
    std::vector<std::shared_ptr<Entity>> pendingEntities;
    int idCounter = 1000;

    // The river band [15.5, 17.5), centred on 16.5 so it is symmetric under the
    // tower layout's y -> 33 - y mirror. Both teams' own-half placement bound
    // then sits at the same mirrored row.
    float riverY_start = 15.5f;
    float riverY_end = 17.5f;
    // Bridge centres from ArenaLayout: 2.5 and 14.5, on the seam between each
    // bridge's two tiles (see ArenaLayout.h).
    Vector2D leftBridge{ ArenaLayout::LEFT_BRIDGE_X, ArenaLayout::BRIDGE_Y };
    Vector2D rightBridge{ ArenaLayout::RIGHT_BRIDGE_X, ArenaLayout::BRIDGE_Y };

    // Scratch for resolveCollisions and the colliders() cache; members so the
    // allocation happens once per Board. Neither holds meaning between calls.
    struct Body {
        Entity* entity;
        float radius;
        float body;   // bodyRadiusOf: the troop's own radius (Fidelity::clientBodies) or the implicit one
        float mass;   // the client's Mass (Fidelity::clientBodies), 0 = equal pushes
        bool building;
        bool troop;
        bool flying;
        bool alive;
    };
    struct Collider { size_t index; float radius; };
    mutable std::vector<Body> bodyScratch;
    mutable std::vector<Collider> colliderCache;
    mutable bool collidersDirty = true;

    // The arena is 18x34: one row behind each King is dead space except a
    // centred opening of real ground (isBackRowDeadZone).
    static constexpr float BACK_ROW_OPENING_HALF_WIDTH = 3.0f;

public:
    // Half a bridge's two-tile width; correct only with seam-centred bridges.
    static constexpr float BRIDGE_HALF_WIDTH = 1.0f;

    // Cell i covers [i - 0.5, i + 0.5], so the board's physical extent is x in
    // [-0.5, width - 0.5] and y likewise. isValidPlacement's footprint check
    // needs this physical edge: checking footprints against the index range
    // would reject a troop at both edge columns.
    static constexpr float CELL_HALF_EXTENT = 0.5f;

private:

public:
    // Public: every fire site already holds a Board&.
    StatsEventBus statsEvents;

    // Set once per tick by GameManager::step, read deep in the
    // update/performAttack chain to stamp DamageDealtEvent without threading a
    // tick through every override.
    int currentTick = 0;

    // Elixir grants (Elixir Collector) dropped here from deep inside update();
    // GameManager::step drains them into the players' elixir each tick.
    float pendingElixirGrant[2] = { 0.0f, 0.0f };

    Board(int w = 18, int h = 34) : width(w), height(h) {}

    int allocateId() { return idCounter++; }

    // A fully independent copy for decision-time search: every entity is
    // duplicated via Entity::snapshot(), so the copy can be stepped without
    // touching the original. Copy-constructing a Board is still a shallow copy
    // that shares entities.
    //
    // Not carried across:
    //   * `statsEvents` starts empty. The collectors are stateful, and a rollout's hits would land in the live match's statistics, which feed the reward. GameManager::snapshot copies them properly.
    //   * GameManager's own state (tick, game over, elixir), which GameManager::snapshot handles.
    Board deepCopy() const {
        Board copy(width, height);

        // Geometry copied explicitly rather than trusting the in-class
        // initialisers to still agree.
        copy.riverY_start = riverY_start;
        copy.riverY_end = riverY_end;
        copy.leftBridge = leftBridge;
        copy.rightBridge = rightBridge;

        // idCounter carries across, or rollout spawns would reuse ids and
        // id-keyed lookups would join the wrong entity.
        copy.idCounter = idCounter;
        copy.currentTick = currentTick;
        copy.pendingElixirGrant[0] = pendingElixirGrant[0];
        copy.pendingElixirGrant[1] = pendingElixirGrant[1];

        std::unordered_map<int, std::shared_ptr<Entity>> byOldId;
        byOldId.reserve(activeEntities.size() + pendingEntities.size());

        // Pending entities too: a card played this tick lives there until the
        // next commit.
        copy.activeEntities.reserve(activeEntities.size());
        for (const auto& e : activeEntities) {
            auto c = e->snapshot();
            byOldId[e->id] = c;
            copy.activeEntities.push_back(std::move(c));
        }
        copy.pendingEntities.reserve(pendingEntities.size());
        for (const auto& e : pendingEntities) {
            auto c = e->snapshot();
            byOldId[e->id] = c;
            copy.pendingEntities.push_back(std::move(c));
        }

        // Must be a second pass: a projectile can home on an entity copied
        // after it. A no-op except for Projectile.
        for (const auto& e : copy.activeEntities) e->remapSnapshotReferences(byOldId);
        for (const auto& e : copy.pendingEntities) e->remapSnapshotReferences(byOldId);

        return copy;
    }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    // The river band, for callers such as the own-half placement check.
    float getRiverStart() const { return riverY_start; }
    float getRiverEnd() const { return riverY_end; }
    // Is this column a bridge? The single definition, shared by the physics
    // (clampToBoard) and the observation encoder so they cannot disagree.
    //
    // Takes a float to serve both: continuous positions, and integer cell
    // centres (cell i covers [i-0.5, i+0.5]).
    bool isOnBridge(float x) const {
        return (x >= leftBridge.x - BRIDGE_HALF_WIDTH && x <= leftBridge.x + BRIDGE_HALF_WIDTH)
            || (x >= rightBridge.x - BRIDGE_HALF_WIDTH && x <= rightBridge.x + BRIDGE_HALF_WIDTH);
    }

    const Vector2D& getLeftBridge() const { return leftBridge; }
    const Vector2D& getRightBridge() const { return rightBridge; }

    // True for the unplaceable corners of the two back rows, outside the
    // centred opening; false everywhere else.
    bool isBackRowDeadZone(float x, float y) const {
        bool inBackRow = (y < 1.0f) || (y > static_cast<float>(height) - 2.0f);
        if (!inBackRow) return false;
        float centerX = (static_cast<float>(width) - 1.0f) / 2.0f;
        return (x < centerX - BACK_ROW_OPENING_HALF_WIDTH) || (x > centerX + BACK_ROW_OPENING_HALF_WIDTH);
    }

    void addEntity(std::shared_ptr<Entity> entity) {
        pendingEntities.push_back(entity);
    }

    // Lets GameManager::playCard find the entity a deploy just spawned;
    // spawnEntity returns nothing.
    size_t pendingEntityCount() const { return pendingEntities.size(); }
    const std::shared_ptr<Entity>& getPendingEntity(size_t index) const { return pendingEntities[index]; }

    // GameManager::step passes the real tick; tests call it without one.
    void commitPendingEntities(int tick = 0) {
        if (!pendingEntities.empty()) {
            for (const auto& e : pendingEntities) {
                statsEvents.notifyEntitySpawned({ e->id, e->cardId, e->team, tick });
            }
            activeEntities.insert(activeEntities.end(), pendingEntities.begin(), pendingEntities.end());
            pendingEntities.clear();
            collidersDirty = true;   // membership changed; see colliders()
        }
    }

    const std::vector<std::shared_ptr<Entity>>& getEntities() const {
        return activeEntities;
    }

    void cleanDeadEntities(int tick = 0) {
        // Nothing died: skip the three passes below, which would all be no-ops.
        // Most ticks take this branch.
        bool anyDead = false;
        for (const auto& e : activeEntities) {
            if (!e->isAlive()) { anyDead = true; break; }
        }
        if (!anyDead) return;
        collidersDirty = true;   // membership is about to change

        // Death effects fire before the erase, via Entity::onDeath.
        for (const auto& e : activeEntities) {
            if (!e->isAlive()) {
                e->onDeath(*this);
                statsEvents.notifyEntityDied({ e->id, e->cardId, e->team, tick });
            }
        }

        // Every living entity hears of every death (Skeleton King's souls). A
        // separate pass, so it reaches only entities already on the board, not
        // anything a death effect just spawned.
        for (const auto& dying : activeEntities) {
            if (!dying->isAlive()) {
                for (const auto& other : activeEntities) {
                    if (other->isAlive()) other->onNearbyDeath(*this, dying->position, dying->team);
                }
            }
        }

        activeEntities.erase(
            std::remove_if(activeEntities.begin(), activeEntities.end(),
                [](const std::shared_ptr<Entity>& e) { return !e->isAlive(); }),
            activeEntities.end()
        );
    }

    // Push a point out of a circular obstacle, with a small perpendicular slide
    // so it travels around instead of sticking. Shared by movement and
    // collision resolution, so once a tick: 0.05 tile per 100 ms.
    static constexpr float SLIDE_PER_TICK = perTick(0.5f);
    static Vector2D pushAwayFrom(Vector2D pos, const Vector2D& obstacleCenter, float minDist) {
        float dx = pos.x - obstacleCenter.x;
        float dy = pos.y - obstacleCenter.y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < minDist) {
            // Direction and distance are computed separately: a point exactly
            // on the centre is pushed the full minDist along +x. The ordinary
            // case is unchanged.
            float ux, uy;
            if (dist < 0.001f) {
                ux = 1.0f; uy = 0.0f;   // coincident: any direction
            } else {
                ux = dx / dist; uy = dy / dist;
            }
            float push = minDist - dist;
            pos.x += ux * push + uy * SLIDE_PER_TICK;
            pos.y += uy * push - ux * SLIDE_PER_TICK;
        }
        return pos;
    }

    // Indices of everything with a real collision radius (buildings and towers,
    // a handful of entities), so resolvePositionAgainstBuildings does not scan
    // the whole list per moving troop.
    //
    // Radii never change, so only membership invalidates the cache, and it
    // changes only in commitPendingEntities and cleanDeadEntities, which set
    // the flag. Liveness is checked live.
    const std::vector<Collider>& colliders() const {
        if (collidersDirty) {
            colliderCache.clear();
            for (size_t i = 0; i < activeEntities.size(); ++i) {
                const float r = activeEntities[i]->getCollisionRadius();
                if (r > 0.0f) colliderCache.push_back(Collider{ i, r });
            }
            collidersDirty = false;
        }
        return colliderCache;
    }

    Vector2D resolvePositionAgainstBuildings(const Vector2D& pos, int entityId, float ownRadius = Entity::IMPLICIT_TROOP_RADIUS) const {
        Vector2D resolved = pos;
        // Ascending index order: the pushes compose, so order is part of the
        // answer.
        for (const Collider& c : colliders()) {
            const auto& entity = activeEntities[c.index];
            if (entity->id == entityId || !entity->isAlive()) continue;

            resolved = pushAwayFrom(resolved, entity->position, c.radius + ownRadius);
        }
        return resolved;
    }

    // Separates every overlapping pair for one tick, then re-clamps everyone to
    // the board and river.
    void resolveCollisions() {
        // Per-entity facts read once per entity instead of per pair; within
        // this call radius, targetability and liveness are constant. Positions
        // are not hoisted, since they are what this mutates.
        const size_t count = activeEntities.size();
        bodyScratch.clear();
        bodyScratch.reserve(count);
        for (const auto& e : activeEntities) {
            const float r = e->getCollisionRadius();
            bodyScratch.push_back(Body{
                e.get(), r, bodyRadiusOf(*e), (Fidelity::clientBodies && e->mass > 0) ? static_cast<float>(e->mass) : 0.0f,
                r > 0.0f, (r <= 0.0f && e->isTargetable()), e->isFlying, e->isAlive() });
        }

        for (size_t i = 0; i < count; ++i) {
            const Body& b1 = bodyScratch[i];
            if (!b1.alive) continue;
            for (size_t j = i + 1; j < count; ++j) {
                const Body& b2 = bodyScratch[j];

                if (!b2.alive) continue;

                Entity* e1 = b1.entity;
                Entity* e2 = b2.entity;

                const float r1 = b1.radius;
                const float r2 = b2.radius;

                const bool isBuilding1 = b1.building;
                const bool isBuilding2 = b2.building;
                const bool isTroop1 = b1.troop;
                const bool isTroop2 = b2.troop;

                // Only entities in the same plane collide; fliers pass through
                // everything.
                if (isTroop1 && isTroop2 && b1.flying == b2.flying) {
                    float dx = e1->position.x - e2->position.x;
                    float dy = e1->position.y - e2->position.y;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    // Both bodies' radii (2 x IMPLICIT_TROOP_RADIUS unless Fidelity::clientBodies).
                    float minRadius = b1.body + b2.body;

                    if (dist < minRadius) {
                        if (dist < 0.001f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
                        float overlap = minRadius - dist;
                        // The lighter body gives way (the client's Mass): each moves by the other's share of the
                        // total mass; equal halves without masses.
                        const float share1 = (b1.mass > 0.0f && b2.mass > 0.0f) ? b2.mass / (b1.mass + b2.mass) : 0.5f;
                        const float share2 = 1.0f - share1;
                        const float ux = dx / dist, uy = dy / dist;

                        // Tiny orthogonal nudge to prevent jitter lock: 0.01 tile per 100 ms.
                        const float noise = perTick(0.1f);
                        e1->position.x += ux * overlap * share1 + uy * noise;
                        e1->position.y += uy * overlap * share1 - ux * noise;
                        e2->position.x -= ux * overlap * share2 + uy * noise;
                        e2->position.y -= uy * overlap * share2 - ux * noise;
                    }
                }

                if (isTroop1 && isBuilding2 && !b1.flying) {
                    e1->position = pushAwayFrom(e1->position, e2->position, r2 + b1.body);
                }
                if (isTroop2 && isBuilding1 && !b2.flying) {
                    e2->position = pushAwayFrom(e2->position, e1->position, r1 + b2.body);
                }
            }
        }

        // Re-clamp: collisions can push a troop into the river or off the
        // board. Each entity's clampPosition respects riverIgnores.
        for (auto& entity : activeEntities) {
            if (entity->isAlive()) {
                entity->clampPosition(*this);
            }
        }
    }

    // ---- Fidelity::clientCollision: the client's body pushing (bot-77 docs/client-code-notes.md §5) ----
    // Separation 0x71fff6058c and move step 0x71fff60bd4, one logic tick = 50 ms = one engine tick (Tick.h); lengths
    // in tiles (client 1/1000).
    static constexpr float CLIENT_MAX_OVERLAP = 0.3f;     // overlap counted per neighbour, at most
    static constexpr float CLIENT_MAX_SHARE = 0.299f;     // overlap x Mass ratio, at most
    static constexpr float CLIENT_MIN_PUSH = 0.001f;      // added to every contact's push
    static constexpr float CLIENT_CONTACT_SKIN = 0.02f;   // bodies this close count as contacts
    static constexpr float CLIENT_MAX_PUSH = 0.15f;       // the averaged push, per 50 ms tick
    static constexpr float CLIENT_RIVER_CELL = 0.5f;      // tilemap cell (36 x 64 over the arena)
    static constexpr float CLIENT_RIVER_NUDGE = 0.256f;   // toward the nearer bank, per 50 ms tick, while pushed in a river cell

    // A river tilemap cell: the river band, off the bridges. Cells are CLIENT_RIVER_CELL squares on the board's
    // half-tile grid (the band [15.5, 17.5) is four rows of them); a cell is a bridge cell if its centre is on a
    // bridge (isOnBridge, so the bridges stay where the engine has them).
    bool isRiverCellAt(float x, float y) const {
        if (!(y > riverY_start && y < riverY_end)) return false;
        const float cellCentreX = (std::floor(x / CLIENT_RIVER_CELL) + 0.5f) * CLIENT_RIVER_CELL;
        return !isOnBridge(cellCentreX);
    }

    // The client's terrain move (0x720017de58) for a body the river stops. From a dry cell the move is not clamped
    // (a push can carry the centre into the first river cell). From a river cell it is clamped per axis at the cell
    // border, x then y: it cannot enter another river cell, the other axis keeps moving (a slide); dry cells are
    // open. `pushed`: the move carries a body push, which adds the nudge toward the nearer bank (by map half), inside
    // the clamped move.
    Vector2D clientRiverMove(const Vector2D& from, Vector2D to, bool pushed) const {
        if (!isRiverCellAt(from.x, from.y)) return to;
        const float riverMid = (riverY_start + riverY_end) / 2.0f;
        if (pushed) to.y += (from.y < riverMid) ? -CLIENT_RIVER_NUDGE : CLIENT_RIVER_NUDGE;
        constexpr float EDGE = 1e-4f;   // just inside the border
        const float cx = std::floor(from.x / CLIENT_RIVER_CELL);
        const float cy = std::floor(from.y / CLIENT_RIVER_CELL);
        float x = to.x;
        if (std::floor(x / CLIENT_RIVER_CELL) != cx && isRiverCellAt(x, from.y)) {
            x = (x > from.x) ? (cx + 1.0f) * CLIENT_RIVER_CELL - EDGE : cx * CLIENT_RIVER_CELL + EDGE;
        }
        float y = to.y;
        if (isRiverCellAt(x, from.y) && std::floor(y / CLIENT_RIVER_CELL) != cy && isRiverCellAt(x, y)) {
            y = (y > from.y) ? (cy + 1.0f) * CLIENT_RIVER_CELL - EDGE : cy * CLIENT_RIVER_CELL + EDGE;
        }
        return Vector2D{ x, y };
    }

    // One 50 ms separation pass for one body, applied to it at once (the next body sees where it went), plus `step`
    // (its own move this tick) in the same move. Per neighbour on its layer (ground / air) within
    // radius + radius + CLIENT_CONTACT_SKIN: min(overlap clamped to [0, 0.3] x its Mass / own Mass, 0.299) + 0.001,
    // away from it; coincident centres 0.001 along y, toward the unit's own side. The pushes are averaged over the
    // contacts and capped at CLIENT_MAX_PUSH. Mass is the client's (Fidelity::clientBodies); equal without it.
    // No sideways nudge. Buildings are not contacts here (settleClientPushTick keeps their static push-out).
    void clientSelfPush(Entity& self, const Vector2D& step = Vector2D{ 0.0f, 0.0f }) {
        float px = 0.0f, py = 0.0f;
        int contacts = 0;
        if (self.isAlive() && self.collidesAsBody(*this)) {
            const float r1 = bodyRadiusOf(self);
            const float m1 = static_cast<float>(self.mass);
            for (const auto& o : activeEntities) {
                Entity& other = *o;
                if (&other == &self || !other.isAlive() || other.getCollisionRadius() > 0.0f) continue;
                if (other.isFlying != self.isFlying || !other.collidesAsBody(*this)) continue;
                const float r2 = bodyRadiusOf(other);
                const float dx = self.position.x - other.position.x;
                const float dy = self.position.y - other.position.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist >= r1 + r2 + CLIENT_CONTACT_SKIN) continue;
                ++contacts;
                if (dist < 0.001f) {
                    py += (self.team == 0) ? -CLIENT_MIN_PUSH : CLIENT_MIN_PUSH;
                    continue;
                }
                const float m2 = static_cast<float>(other.mass);
                const float ratio = (Fidelity::clientBodies && m1 > 0.0f && m2 > 0.0f) ? m2 / m1 : 1.0f;
                const float overlap = std::max(0.0f, std::min(r1 + r2 - dist, CLIENT_MAX_OVERLAP));
                const float push = std::min(overlap * ratio, CLIENT_MAX_SHARE) + CLIENT_MIN_PUSH;
                px += dx / dist * push;
                py += dy / dist * push;
            }
            if (contacts > 0) {
                px /= static_cast<float>(contacts);
                py /= static_cast<float>(contacts);
                const float len = std::sqrt(px * px + py * py);
                if (len > CLIENT_MAX_PUSH) { px *= CLIENT_MAX_PUSH / len; py *= CLIENT_MAX_PUSH / len; }
            }
        }
        if (contacts == 0 && step.x == 0.0f && step.y == 0.0f) return;
        const Vector2D from = self.position;
        Vector2D to{ from.x + px + step.x, from.y + py + step.y };
        if (self.respectsRiver()) {
            to = clientRiverMove(from, to, contacts > 0);
            if (contacts > 0 && isRiverCellAt(to.x, to.y)) self.riverPushed = true;
        }
        self.position = to;
    }

    // GameManager::tick under Fidelity::clientCollision, one engine tick = one 50 ms client tick. Per entity in order:
    // its update(), then clientMoveStep, which takes back the move update() made and applies it together with the
    // body's separation push from where it stood (the client's move step: push + walking step in one move, before the
    // next unit's turn). Then settleClientPushTick: buildings' static push-out and the clamps, as resolveCollisions
    // ends. Only bodies (troops: no collision radius, hit by area damage) push; projectiles and spells keep their move.
    void clientMoveStep(size_t index, const Vector2D& before) {
        Entity& e = *activeEntities[index];
        if (!e.isAlive() || e.getCollisionRadius() > 0.0f || !e.isAreaHittable()) return;
        const Vector2D step{ e.position.x - before.x, e.position.y - before.y };
        e.position = before;
        clientSelfPush(e, step);
    }
    void settleClientPushTick() {
        // Buildings: the engine's static push-out (resolveCollisions' troop-building case), unchanged.
        for (const auto& e : activeEntities) {
            if (!e->isAlive() || e->isFlying || e->getCollisionRadius() > 0.0f || !e->isTargetable()) continue;
            e->position = resolvePositionAgainstBuildings(e->position, e->id, bodyRadiusOf(*e));
        }
        for (auto& entity : activeEntities) {
            if (entity->isAlive()) entity->clampPosition(*this);
        }
    }

    // Keeps a position on the board and out of the river unless allowed in it.
    Vector2D clampToBoard(Vector2D pos, bool ignoresRiver) const {
        pos.x = std::max(0.0f, std::min(pos.x, static_cast<float>(width - 1)));
        pos.y = std::max(0.0f, std::min(pos.y, static_cast<float>(height - 1)));

        if (!ignoresRiver && pos.y > riverY_start && pos.y < riverY_end) {
            // isOnBridge(), so the observation encoder gets the same answer.
            if (!isOnBridge(pos.x)) {
                float riverMid = (riverY_start + riverY_end) / 2.0f;
                pos.y = (pos.y < riverMid) ? riverY_start : riverY_end;
            }
        }
        return pos;
    }

    // The single "standing on the waypoint" tolerance, shared with
    // Troop::moveTowards, which does not move closer than this. If
    // getNextWaypoint hands back a point the mover is already within this of,
    // the unit freezes for the rest of the match (tests/core/test_board.cpp).
    static constexpr float WAYPOINT_ARRIVAL_EPS = 0.01f;

    Vector2D getNextWaypoint(const Vector2D& currentPos, const Vector2D& targetPos) const {
        bool isCurrentBelow = currentPos.y <= riverY_start;
        bool isTargetBelow = targetPos.y <= riverY_start;
        bool isCurrentAbove = currentPos.y >= riverY_end;
        bool isTargetAbove = targetPos.y >= riverY_end;

        if ((isCurrentBelow && isTargetBelow) || (isCurrentAbove && isTargetAbove) ||
            (!isCurrentBelow && !isCurrentAbove && !isTargetBelow && !isTargetAbove)) {
            return targetPos;
        }

        float distToLeft = currentPos.distanceTo(leftBridge);
        float distToRight = currentPos.distanceTo(rightBridge);
        float bridgeX = (distToLeft < distToRight) ? leftBridge.x : rightBridge.x;

        if (isCurrentBelow) {
            // The bank tests are inclusive, so a unit already on the near bank
            // is still "below"; hand it the far bank, or it is routed to where
            // it stands.
            Vector2D nearBank{bridgeX, riverY_start};
            if (currentPos.distanceTo(nearBank) <= WAYPOINT_ARRIVAL_EPS) {
                return Vector2D{bridgeX, riverY_end};
            }
            return nearBank;
        } else if (isCurrentAbove) {
            Vector2D nearBank{bridgeX, riverY_end};
            if (currentPos.distanceTo(nearBank) <= WAYPOINT_ARRIVAL_EPS) {
                return Vector2D{bridgeX, riverY_start};
            }
            return nearBank;
        } else {
            // Inside the river band, heading for the exit bank. Within
            // WAYPOINT_ARRIVAL_EPS of it this leg is done: return targetPos,
            // which the first branch returns anyway once y reaches the bank.
            // Handing back the edge itself would freeze the unit just short of
            // dry land (tests/core/test_board.cpp, the bridge-exit block).
            if (isTargetAbove) {
                Vector2D exitBank{bridgeX, riverY_end};
                if (currentPos.distanceTo(exitBank) <= WAYPOINT_ARRIVAL_EPS) {
                    return targetPos;
                }
                return exitBank;
            } else if (isTargetBelow) {
                Vector2D exitBank{bridgeX, riverY_start};
                if (currentPos.distanceTo(exitBank) <= WAYPOINT_ARRIVAL_EPS) {
                    return targetPos;
                }
                return exitBank;
            } else {
                return targetPos;
            }
        }
    }
};