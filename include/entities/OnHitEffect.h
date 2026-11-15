#pragma once
#include <memory>

class CombatEntity;

// Behaviour triggered when a CombatEntity lands an attack, independent of how
// the attack deals damage, so cards like Ice Wizard are a normal troop plus an
// effect.
//
// Interface only, with CombatEntity forward-declared: concrete effects that
// call into it live in their own headers, since CombatEntity.h includes this
// one.
class IOnHitEffect {
public:
    virtual ~IOnHitEffect() = default;
    virtual void apply(std::shared_ptr<CombatEntity> target) const = 0;
    // A copy dealing its damage at card level `mult` (CardLevels.h), or nullptr when the effect deals none
    // (slows, stuns, curses). Used once, when the entity carrying it is levelled.
    virtual std::shared_ptr<IOnHitEffect> atLevel(int mult) const { (void)mult; return nullptr; }
};
