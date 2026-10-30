#pragma once
#include "Troop.h"
#include "Projectile.h"

class RangedTroop : public Troop {
public:
    // Boomerang (Executioner): the projectile hits on arrival and again
    // boomerangReturnDelayTicks later.
    bool boomerang = false;
    int boomerangReturnDelayTicks = 0;

    // Pellets (Hunter): each attack is a fan of colliding shots fired from
    // here instead of one homing projectile; `damage` is per pellet.
    PelletSpread pellets;
    // Burst (Firecracker): the homing shell bursts at the impact point into
    // this fan instead of hitting; `damage` is per spark.
    PelletSpread burstPellets;

    // A second, melee attack (Three Musketeers' bayonet): against a GROUND
    // target within meleeAltRange (surface to surface, as attackRange) the
    // attack is a direct hit for meleeAltDamage instead of the shot. One or
    // the other per attack, never both; air targets always get the shot.
    float meleeAltRange = 0.0f;
    int meleeAltDamage = 0;

    RangedTroop(int id, float x, float y, int hp, int team,
        float speed, float attackRange, int damage, int attackCooldown, char symbol = 'A')
        : Troop(id, x, y, hp, team, symbol, speed, attackRange, damage, attackCooldown) {}

    std::shared_ptr<Entity> clone(int newId) const override {
        auto copy = std::make_shared<RangedTroop>(*this);
        copy->becomeCloneCopy(newId); // Clone: full damage, 1 hp, no ability slot
        return copy;
    }

    // For Board::deepCopy: id and hp preserved, unlike clone().
    std::shared_ptr<Entity> snapshot() const override {
        return std::make_shared<RangedTroop>(*this);
    }

protected:
    void performAttack(Board& board, std::shared_ptr<Entity> target) override {
        if (meleeAltRange > 0.0f && !target->isFlying
            && position.distanceTo(target->position) <= meleeAltRange + ownEffectiveRadius() + effectiveRadiusOf(*target)) {
            // The registry damage scaled the way getCurrentDamage scales the shot (Rage, ...).
            const int base = getCurrentDamage();
            int dealt = (damage > 0) ? static_cast<int>(static_cast<long long>(meleeAltDamage) * base / damage) : meleeAltDamage;
            if (target->isTower() && crownTowerFraction < 1.0f) dealt = static_cast<int>(dealt * crownTowerFraction + 0.5f);
            target->takeDamage(dealt);
            board.statsEvents.notifyDamageDealt(
                { id, team, cardId, target->id, target->cardId, target->team, dealt, board.currentTick, target->isTower() });
            applyOnHitEffects(target);
            return;
        }
        if (pellets.count > 0) {
            firePelletFan(board, position, target->position, 0.0f, 0.0f, pellets, getCurrentDamage(), team, id, cardId);
            return;
        }
        // On-hit effects travel with the shot and land when it does.
        auto arrow = std::make_shared<Projectile>(
            board.allocateId(), position.x, position.y, team, target, shotSpeed(perTick(15.0f)), getCurrentDamage(), onHitEffects,
            boomerang, boomerangReturnDelayTicks, id, cardId, splashRadius, lineSplash, lineSplashRange);
        if (burstPellets.count > 0) arrow->setBurst(burstPellets);
        // after setBurst: a non-homing shell (Firecracker) flies to where the target stood
        arrow->setHoming(shotHoming());
        board.addEntity(arrow);
    }
};