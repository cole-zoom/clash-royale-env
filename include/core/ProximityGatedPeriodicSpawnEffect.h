#pragma once
#include "PeriodicEffect.h"
#include "CardStats.h"
#include "CardFactories.h"
#include <cmath>

// The Goblin Hut (client goblin_hut_rework.toml, ActionGoblinHutLifeState): asleep until an enemy character (a troop,
// not a building or tower) is within `detectionRadius` of it (Range 6000), then a Spear Goblin at once (ActionDelay
// 1000 = its DeployTime) and one every SpawnInterval 2200 while one stays in range; back to sleep when none is.
// Lab night 2026-10-29 (L21 / L09, three replays): the first Spear Goblin shows 0.7-1.2 s after the Hut, and spawning
// stops once the enemy is gone; the old reading (a free-running 2.2 s timer that skipped a spawn if nobody was near at
// that tick) put the first one 3-30 s late.
class ProximityGatedPeriodicSpawnEffect : public IPeriodicEffect {
    CardStats childStats;
    float detectionRadius;
    static constexpr float SPAWN_OFFSET = 1.2f;

    // The nearest enemy character (a troop, not a building or tower) within detectionRadius, edge to edge.
    const Entity* nearestWaker(const Board& board, const Entity& self) const {
        const Entity* best = nullptr;
        float bestD = 0.0f;
        for (const auto& entity : board.getEntities()) {
            if (entity->team == self.team || !entity->isAlive() || !entity->isTargetable()) continue;
            if (entity->isBuilding()) continue;
            const float d = self.position.distanceTo(entity->position);
            if (d > detectionRadius + bodyRadiusOf(self) + bodyRadiusOf(*entity)) continue;
            if (!best || d < bestD) { best = entity.get(); bestD = d; }
        }
        return best;
    }

public:
    ProximityGatedPeriodicSpawnEffect(CardStats childStats, float detectionRadius)
        : childStats(std::move(childStats)), detectionRadius(detectionRadius) {}

    bool gated() const override { return true; }

    // Edge to edge, as the client measures sight (Range + both radii): r54 had the Hog 6.8 tiles from the Hut's
    // centre as its deploy ended, and the real Hut still woke (three Spear Goblins).
    bool open(const Board& board, const Entity& self) const override { return nearestWaker(board, self) != nullptr; }

    // Each Spear Goblin appears SpawnOffset 1200 (1.2 tiles) from the Hut toward the nearest enemy in range: L09's
    // replay shows them on the Hog's side of the Hut, shooting it as it comes; the engine put them at the Hut's centre
    // and its building push-out shoved them out to one fixed side. SingleDeployOffsetAngle 20 (a turn off that line,
    // its rule unread) is left out.
    void fire(Board& board, const Entity& self) const override {
        Vector2D at = self.position;
        if (const Entity* e = nearestWaker(board, self)) {
            const float dx = e->position.x - at.x, dy = e->position.y - at.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > 1e-4f) { at.x += dx / d * SPAWN_OFFSET; at.y += dy / d * SPAWN_OFFSET; }
        }
        apply(board, at, self.team);
    }

    void apply(Board& board, const Vector2D& position, int team) const override {
        CardFactories::spawn(childStats, position.x, position.y, team, board);
    }
};
