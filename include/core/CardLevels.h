#pragma once
#include <stdexcept>
#include <string>

// Per-card levels. CardRegistry's stats are level-11 values; a card played at
// another level has its hitpoints and every damage figure it deals scaled by
// M(level) / M(11), where M is the game's PowerLevelMultiplier
// (rarities.csv, Common row: since the unified level scale every rarity
// shares it). Speeds, ranges, hit speeds, counts, radii, durations and heal
// amounts do not scale.
//
// The multiplier is kept as the table's integer percentage and applied once,
// when an entity is added to the board (Board::addEntity -> applyCardLevel),
// so an entity carries its own (Entity::levelMult) and whatever it spawns
// later (death spawns, periodic spawns, spell spawns, death damage) takes it
// as well (Board::LevelScope).

// PowerLevelMultiplier, percent, for levels 1..20 (rarities.csv, Common row).
inline constexpr int CARD_LEVEL_MIN = 1;
inline constexpr int CARD_LEVEL_MAX = 20;
inline constexpr int CARD_LEVEL_POWER_MULTIPLIER[CARD_LEVEL_MAX] = {
    100, 110, 121, 133, 146, 160, 176, 193, 212, 233,
    256, 281, 309, 339, 372, 409, 450, 495, 545, 600,
};
// The registry's level: its multiplier is the identity.
inline constexpr int CARD_LEVEL_BASE = 11;
inline constexpr int CARD_LEVEL_BASE_MULT = CARD_LEVEL_POWER_MULTIPLIER[CARD_LEVEL_BASE - 1];   // 256

inline int cardLevelMultiplier(int level) {
    if (level < CARD_LEVEL_MIN || level > CARD_LEVEL_MAX) {
        throw std::invalid_argument("card level must be 1..20, got " + std::to_string(level));
    }
    return CARD_LEVEL_POWER_MULTIPLIER[level - 1];
}

inline int cardLevelForMultiplier(int mult) {
    for (int level = CARD_LEVEL_MIN; level <= CARD_LEVEL_MAX; ++level) {
        if (CARD_LEVEL_POWER_MULTIPLIER[level - 1] == mult) return level;
    }
    return -1;
}

// floor(base * mult / 256) for an integer level-11 stat: the client truncates
// per-level stats to integers. Exact integer math, so the base level returns
// `base` unchanged.
inline int scaleByLevel(int base, int mult) {
    if (mult == CARD_LEVEL_BASE_MULT) return base;
    return static_cast<int>(static_cast<long long>(base) * mult / CARD_LEVEL_BASE_MULT);
}
