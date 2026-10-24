#pragma once
#include <algorithm>
#include <vector>
#include "Entity.h"

// Squad formations at deploy, fitted to the replays (bot-77 scripts/sim_formations.py: the bodies' spread at
// their first detections, 200 train games). Scales a card's spawn offsets (x, y) and, where the engine had a flat
// line but the game has depth, staggers alternate bodies (x order) forward / back by `stagger` engine rows.
struct FormationFit { float kx, ky, stagger; };

inline FormationFit formationFitForCard(int cardId) {
    switch (cardId) {
        case 24: return {0.995f, 1.000f, 0.496f};  // Skeletons (n 385)
        case 23: return {1.221f, 1.000f, 0.333f};  // Spear Goblins (n 28)
        case 76: return {1.587f, 1.000f, 0.000f};  // Guards (n 32)
        case 78: return {2.473f, 3.898f, 0.000f};  // Bats (n 22)
        case 82: return {2.027f, 0.428f, 0.000f};  // Royal Hogs (n 31)
        case 83: return {1.852f, 1.000f, 0.000f};  // Wall Breakers (n 31)
        case 17: return {1.353f, 1.000f, 0.000f};  // Elite Barbarians (n 44)
        case 65: return {3.714f, 1.000f, 0.000f};  // Skeleton Dragons (n 25)
        default: return {1.0f, 1.0f, 0.0f};
    }
}

inline std::vector<Vector2D> fittedOffsets(int cardId, std::vector<Vector2D> offs) {
    const FormationFit f = formationFitForCard(cardId);
    for (auto& o : offs) { o.x *= f.kx; o.y *= f.ky; }
    if (f.stagger != 0.0f && offs.size() > 1) {
        std::vector<size_t> order(offs.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return offs[a].x < offs[b].x; });
        float mean = 0.0f;
        for (size_t r = 0; r < order.size(); ++r) mean += (r % 2) ? 1.0f : -1.0f;
        mean /= static_cast<float>(order.size());
        for (size_t r = 0; r < order.size(); ++r) offs[order[r]].y += f.stagger * (((r % 2) ? 1.0f : -1.0f) - mean);
    }
    return offs;
}
