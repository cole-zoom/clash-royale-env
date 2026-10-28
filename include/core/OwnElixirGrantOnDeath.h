#pragma once
#include "DeathEffect.h"
#include "Board.h"

// Grants elixir to the dying entity's OWN team (Elixir Collector's ManaOnDeath), via Board::pendingElixirGrant.
// EnemyElixirGrantOnDeath is the opponent's counterpart.
class OwnElixirGrantOnDeath : public IDeathEffect {
    float amount;

public:
    explicit OwnElixirGrantOnDeath(float amount) : amount(amount) {}

    void apply(Board& board, const Vector2D& /*position*/, int team) const override {
        board.pendingElixirGrant[team] += amount;
    }
};
