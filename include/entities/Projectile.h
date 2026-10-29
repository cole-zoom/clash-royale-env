#pragma once
#include "Entity.h"
#include "CombatEntity.h"
#include "Board.h"
#include "OnHitEffect.h"
#include "StatsEvents.h"
#include "PelletSpread.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

// One shot of a PelletSpread (Hunter pellets, Firecracker sparks): flies a
// straight line at a fixed speed and collides with whatever enemy it touches,
// ground or air, within collisionRadius plus the enemy's own radius (the
// surface convention effectiveRangeTo uses). A non-piercing shot is gone on its
// first collision, the earliest along its path; a piercing one hits each enemy
// once and flies on. Either vanishes after `range` tiles. Not homing: it goes
// where it was aimed. Collision is swept over each tick's whole step, so a fast
// shot cannot tunnel through a small target.
class PelletProjectile : public Entity {
private:
    float dirX, dirY;
    float speed;
    float rangeLeft;
    float collisionRadius;
    int damage;
    bool piercing;
    int attackerId;
    int attackerCardId;
    std::vector<int> hitIds; // piercing only: who this shot has already hit

public:
    PelletProjectile(int id, float x, float y, int team, float dirX, float dirY, const PelletSpread& spread,
        int damage, int attackerId, int attackerCardId)
        : Entity(id, x, y, 1, team, '.'), dirX(dirX), dirY(dirY), speed(spread.speed), rangeLeft(spread.range),
        collisionRadius(spread.collisionRadius), damage(damage), piercing(spread.piercing),
        attackerId(attackerId), attackerCardId(attackerCardId) {}

    bool isTargetable() const override { return false; }

    // No entity pointers: ids only, so the default remap is enough.
    std::shared_ptr<Entity> snapshot() const override {
        return std::make_shared<PelletProjectile>(*this);
    }

    void update(Board& board) override {
        if (rangeLeft <= 0.0f || speed <= 0.0f) { hp = 0; return; }
        const float step = std::min(speed, rangeLeft);

        // Everything this tick's segment touches, by how far along it.
        struct Touch { float along; std::shared_ptr<Entity> entity; };
        std::vector<Touch> touched;
        for (const auto& e : board.getEntities()) {
            if (e->team == team || !e->isAlive() || !e->isAreaHittable()) continue;
            if (piercing && std::find(hitIds.begin(), hitIds.end(), e->id) != hitIds.end()) continue;
            const float rx = e->position.x - position.x;
            const float ry = e->position.y - position.y;
            const float along = std::max(0.0f, std::min(rx * dirX + ry * dirY, step));
            const float cx = rx - dirX * along;
            const float cy = ry - dirY * along;
            if (std::sqrt(cx * cx + cy * cy) <= collisionRadius + CombatEntity::effectiveRadiusOf(*e)) {
                touched.push_back({ along, e });
            }
        }
        std::stable_sort(touched.begin(), touched.end(),
            [](const Touch& a, const Touch& b) { return a.along < b.along; });

        for (const auto& t : touched) {
            t.entity->takeDamage(damage);
            board.statsEvents.notifyDamageDealt(
                { attackerId, team, attackerCardId, t.entity->id, t.entity->cardId, t.entity->team, damage,
                  board.currentTick, t.entity->isTower() });
            if (!piercing) {
                position.x += dirX * t.along;
                position.y += dirY * t.along;
                hp = 0;
                return;
            }
            hitIds.push_back(t.entity->id);
        }

        position.x += dirX * step;
        position.y += dirY * step;
        rangeLeft -= step;
        if (rangeLeft <= 0.0f) hp = 0;
    }
};

// Fires spread.count PelletProjectiles from `from`, fanned evenly across
// spread.fanDegrees about the direction from `from` toward `aim` (or along
// (fallbackDirX, fallbackDirY) when the two coincide). Each deals `damage`.
inline void firePelletFan(Board& board, const Vector2D& from, const Vector2D& aim, float fallbackDirX,
        float fallbackDirY, const PelletSpread& spread, int damage, int team, int attackerId, int attackerCardId) {
    if (spread.count <= 0) return;
    float dx = aim.x - from.x, dy = aim.y - from.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 1e-4f) { dx = fallbackDirX; dy = fallbackDirY; len = std::sqrt(dx * dx + dy * dy); }
    if (len <= 1e-4f) { dx = 0.0f; dy = (team == 0) ? 1.0f : -1.0f; len = 1.0f; }
    const float base = std::atan2(dy / len, dx / len);
    const float fan = spread.fanDegrees * 3.14159265f / 180.0f;
    for (int i = 0; i < spread.count; ++i) {
        const float offset = (spread.count > 1) ? (-0.5f * fan + fan * i / (spread.count - 1)) : 0.0f;
        const float a = base + offset;
        board.addEntity(std::make_shared<PelletProjectile>(board.allocateId(), from.x, from.y, team,
            std::cos(a), std::sin(a), spread, damage, attackerId, attackerCardId));
    }
}

class Projectile : public Entity {
private:
    // Entity-typed, like findTarget's result; applyHit narrows to CombatEntity
    // where on-hit effects need it.
    std::weak_ptr<Entity> target;
    float speed;
    int damage;
    std::vector<std::shared_ptr<IOnHitEffect>> onHitEffects;

    // The shooter's identity, carried forward to stamp DamageDealtEvent on
    // arrival.
    int attackerId;
    int attackerCardId;

    // The shooter's splash radius (CombatEntity::applySplashDamage); 0 is a
    // single-target hit.
    float splashRadius;

    // Piercing line (Bowler, Magic Archer; see
    // CombatEntity::applyLineSplashDamage). With lineSplash set, splashRadius
    // is the line's half-width. `origin` is where the shot was fired from,
    // since `position` snaps to the impact point.
    bool lineSplash;
    float lineSplashRange;
    Vector2D origin;

    // Boomerang (Executioner): hits its target on arrival and again
    // returnDelayTicks later. The real axe pierces everything along its path
    // both ways; there is no path-collision primitive, so it hits its one
    // target twice.
    bool returnsToSender;
    int returnDelayTicks;
    bool outboundHitLanded = false;

    // Burst on arrival (Firecracker): instead of hitting, the shell bursts at
    // the impact point into this fan of shots, flying on along the shot's
    // line, each dealing `damage`. count == 0 is an ordinary hit.
    PelletSpread burst;

    // Where the shot is going: the target's position, followed while it lives if the shot homes, else fixed at
    // launch (the client's Homing; Fidelity::clientProjectiles). An area shot whose target dies on the way still
    // lands here (Fidelity::aoeLandsWithoutTarget).
    Vector2D aimPoint;
    bool homing = true;
    // A ground-only shooter's splash stays on the ground (Bomb Tower).
    bool splashHitsAir = true;

    bool isAreaShot() const { return splashRadius > 0.0f || lineSplash || burst.count > 0; }

public:
    Projectile(int id, float x, float y, int team, std::weak_ptr<Entity> target, float speed, int damage,
        std::vector<std::shared_ptr<IOnHitEffect>> onHitEffects = {},
        bool returnsToSender = false, int returnDelayTicks = 0,
        int attackerId = -1, int attackerCardId = -1, float splashRadius = 0.0f,
        bool lineSplash = false, float lineSplashRange = 0.0f)
        : Entity(id, x, y, 1, team, '-'), target(target), speed(speed), damage(damage),
        onHitEffects(std::move(onHitEffects)), attackerId(attackerId), attackerCardId(attackerCardId),
        splashRadius(splashRadius), lineSplash(lineSplash), lineSplashRange(lineSplashRange), origin{ x, y },
        returnsToSender(returnsToSender), returnDelayTicks(returnDelayTicks) {
        auto t = target.lock();
        aimPoint = t ? t->position : Vector2D{ x, y };
    }

    // Non-homing (the client's Homing false): only an area shot flies to a fixed point; a single-target shot keeps
    // homing (no client single-target projectile of the engine's cards lands off its target).
    void setHoming(bool h) { homing = h || !isAreaShot(); }
    void setSplashHitsAir(bool on) { splashHitsAir = on; }

    // The pending-damage skip (Fidelity::pendingDamageSkip): the damage this shot still carries to its target, and
    // when it lands: the ticks update() still needs (it lands on the tick its step covers the remaining distance).
    int incomingTargetId() const override {
        if (outboundHitLanded) return -1;
        auto t = target.lock();
        return (t && t->isAlive()) ? t->id : -1;
    }
    int incomingDamage() const override { return outboundHitLanded ? 0 : damage; }
    int incomingEtaMs() const override {
        auto t = target.lock();
        const Vector2D to = (t && t->isAlive() && homing) ? t->position : aimPoint;
        const float d = position.distanceTo(to);
        const int ticks = (speed > 0.0f) ? std::max(1, static_cast<int>(std::ceil(d / speed))) : 1;
        return ticks * 100;
    }

    bool isTargetable() const override { return false; }

    void setBurst(const PelletSpread& spread) { burst = spread; homing = homing || !isAreaShot(); }

    // For Board::deepCopy. The copy still points `target` at the original
    // board's entity; Board::deepCopy repairs that with
    // remapSnapshotReferences(), which is a no-op for every other type. Not
    // cleared defensively: a projectile with no target dies next tick, which
    // would silently delete every shot in flight from a rollout.
    std::shared_ptr<Entity> snapshot() const override {
        return std::make_shared<Projectile>(*this);
    }

    // Re-points this shot at the equivalent entity on the copied board. A
    // target missing from the map (already dead) is cleared rather than left
    // pointing across boards.
    void remapSnapshotReferences(
        const std::unordered_map<int, std::shared_ptr<Entity>>& byOldId) override {
        auto t = target.lock();
        if (!t) return; // expired: nothing to repair
        auto it = byOldId.find(t->id);
        target = (it != byOldId.end())
            ? std::weak_ptr<Entity>(it->second)
            : std::weak_ptr<Entity>();
    }

    // The id this shot is homing on, or -1. Lets the deepCopy tests assert the
    // remap directly.
    int getTargetId() const {
        auto t = target.lock();
        return t ? t->id : -1;
    }

    void update(Board& board) override {
        auto t = target.lock();
        const bool alive = t && t->isAlive();
        if (alive && homing) aimPoint = t->position;
        // A dead target ends the shot, unless it is an area shot still on its way out
        // (Fidelity::aoeLandsWithoutTarget; a boomerang's return is never made without its target).
        const bool landsAnyway = Fidelity::aoeLandsWithoutTarget && isAreaShot() && !returnsToSender;
        if (!alive && (outboundHitLanded || !landsAnyway)) {
            hp = 0;
            return;
        }

        if (!outboundHitLanded) {
            float dist = position.distanceTo(aimPoint);
            if (dist <= speed) {
                position = aimPoint; // snap to the impact point
                if (burst.count > 0) {
                    // Along the line the shell flew; the shooter's own
                    // facing when it was fired point-blank.
                    firePelletFan(board, position, Vector2D{ 2.0f * position.x - origin.x, 2.0f * position.y - origin.y },
                        position.x - origin.x, position.y - origin.y, burst, damage, team, attackerId, attackerCardId);
                    hp = 0;
                    return;
                }
                if (alive && homing) {
                    applyHit(board, t);
                } else {
                    landArea(board, alive ? t : nullptr);
                }
                outboundHitLanded = true;
                if (!returnsToSender) hp = 0; // a normal projectile is done after one hit
                // else stay alive for the return trip
            } else {
                float dx = aimPoint.x - position.x;
                float dy = aimPoint.y - position.y;
                position.x += (dx / dist) * speed;
                position.y += (dy / dist) * speed;
            }
        } else if (returnDelayTicks > 0) {
            returnDelayTicks--;
        } else {
            applyHit(board, t); // return trip: second hit, same target
            hp = 0;
        }
    }

private:
    void applyHit(Board& board, const std::shared_ptr<Entity>& t) {
        t->takeDamage(damage);
        board.statsEvents.notifyDamageDealt(
            { attackerId, team, attackerCardId, t->id, t->cardId, t->team, damage, board.currentTick, t->isTower() });
        // On-hit effects (e.g. Ice Wizard's freeze) land on arrival, and only
        // against a CombatEntity.
        if (!onHitEffects.empty()) {
            if (auto combatTarget = std::dynamic_pointer_cast<CombatEntity>(t)) {
                for (const auto& effect : onHitEffects) {
                    effect->apply(combatTarget);
                }
            }
        }
        if (lineSplash) {
            applyLineSplashDamage(board, origin, t->position, lineSplashRange, splashRadius,
                t->id, attackerId, team, attackerCardId, damage);
        } else {
            applySplashDamage(board, t->position, splashRadius, t->id, attackerId, team, attackerCardId, damage,
                splashHitsAir);
        }
    }

    // An area shot landing at aimPoint without a homing hit: its target died on the way (t null), or it does not
    // home and lands where the target stood at launch (t alive, hit only if still inside the area). Everyone in
    // the area takes the damage; on-hit effects reach the target if it is caught.
    void landArea(Board& board, const std::shared_ptr<Entity>& t) {
        if (t && t->isAreaHittable() && !onHitEffects.empty()) {
            const bool caught = lineSplash || aimPoint.distanceTo(t->position) <= splashRadius;
            if (caught) {
                if (auto combatTarget = std::dynamic_pointer_cast<CombatEntity>(t)) {
                    for (const auto& effect : onHitEffects) effect->apply(combatTarget);
                }
            }
        }
        if (lineSplash) {
            applyLineSplashDamage(board, origin, aimPoint, lineSplashRange, splashRadius,
                -1, attackerId, team, attackerCardId, damage);
        } else {
            applySplashDamage(board, aimPoint, splashRadius, -1, attackerId, team, attackerCardId, damage,
                splashHitsAir);
        }
    }
};
