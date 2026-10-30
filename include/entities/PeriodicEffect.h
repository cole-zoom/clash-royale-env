#pragma once
#include "Entity.h"

// Behaviour fired on a repeating timer while a CombatEntity lives; the timer
// counterpart of IDeathEffect. Effects that spawn live in core/ because they
// need CardFactories.
class IPeriodicEffect {
public:
    virtual ~IPeriodicEffect() = default;
    virtual void apply(Board& board, const Vector2D& position, int team) const = 0;
    // A gated effect (Goblin Hut) sleeps while closed: when its timer runs out it waits, and fires the tick the gate
    // opens, then every interval while it stays open. Its first chance is the end of the deploy, not one interval in.
    virtual bool gated() const { return false; }
    virtual bool open(const Board& board, const Entity& self) const {
        (void)board; (void)self;
        return true;
    }
    // A gated effect fires through this (it may aim off its owner, e.g. toward the enemy that woke it).
    virtual void fire(Board& board, const Entity& self) const { apply(board, self.position, self.team); }
};
