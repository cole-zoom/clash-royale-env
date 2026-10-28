#pragma once
#include "DeathEffect.h"
#include "Board.h"
#include "CombatEntity.h"
#include "OnHitEffect.h"
#include <memory>

// Area damage on death (Giant Skeleton's bomb; Golem, Ice Golem and Balloon
// explosions). No DamageDealtEvent: the dying entity is gone, so there is no
// attacker to attribute.
//
// `onHit` is the optional slot AreaSpell also has; Ice Golem's death explosion
// slows what it hits. nullptr leaves behaviour unchanged.
class AreaDamageOnDeath : public IDeathEffect {
    float radius;
    int damage;
    std::shared_ptr<IOnHitEffect> onHit;

public:
    AreaDamageOnDeath(float radius, int damage, std::shared_ptr<IOnHitEffect> onHit = nullptr)
        : radius(radius), damage(damage), onHit(std::move(onHit)) {}

    void apply(Board& board, const Vector2D& position, int team) const override {
        for (const auto& entity : board.getEntities()) {
            if (entity->team == team || !entity->isAlive() || !entity->isAreaHittable()) continue;
            // Hits what the blast overlaps (radius plus the target's own radius), as AreaSpell and the delayed
            // bomb (DelayedAreaDamageOnDeath) already do. Centre-to-centre, a Skeleton Barrel popping at a
            // Princess Tower (client: Range 0.35, explosion 2.0) never reached the tower's centre 2.35 away
            // (2026-10-28).
            if (position.distanceTo(entity->position) > radius + CombatEntity::effectiveRadiusOf(*entity)) continue;
            entity->takeDamage(damage);
            if (onHit) {
                if (auto combatTarget = std::dynamic_pointer_cast<CombatEntity>(entity)) {
                    onHit->apply(combatTarget);
                }
            }
        }
    }
};
