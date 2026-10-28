#pragma once
#include "CardStats.h"

// Spirit Empress's ground and flying forms; GameManager::playCard picks one by
// the elixir held at play time (the real switching rule is unsourced). Both
// share id 165 and the same placement footprint, so only cost and stats differ.
//
// client merge_maiden.toml (2026-10-28), level 11: both Hitpoints 438 -> 1121,
// Damage 125 -> 320 (was Liquipedia's level 9, 926 / 249). Ground
// (MergeMaiden_Normal): melee 1.2, 1.2 s, Speed 90 (Fast), ground-only, 3
// elixir. Flying (MergeMaiden_Mounted): range 5, 1.4 s, Speed 60 (Medium), air
// and ground, 6 elixir.
inline CardStats spiritEmpressGroundStats() {
    CardStats stats;
    stats.id = 165;
    stats.name = "Spirit Empress";
    stats.cost = 3.0f;
    stats.archetype = Archetype::MeleeSquad;
    stats.hp = 1121;
    // The client's speed tier (was off-tier 0.85 with no source). Scaled here
    // because direct assignment bypasses CardStats::troop().
    stats.speed = SPEED_FAST * MOVEMENT_SPEED_SCALE;
    stats.attackRange = 1.2f;
    stats.damage = 320;
    stats.attackCooldown = 12;
    stats.symbol = '<';
    return stats;
}

inline CardStats spiritEmpressFlyingStats() {
    CardStats stats;
    stats.id = 165;
    stats.name = "Spirit Empress";
    stats.cost = 6.0f;
    stats.archetype = Archetype::RangedSquad;
    stats.hp = 1121;
    // Scaled for the same reason as the ground form.
    stats.speed = SPEED_MEDIUM * MOVEMENT_SPEED_SCALE;
    stats.attackRange = 5.0f;
    stats.damage = 320;
    stats.attackCooldown = 14;
    stats.symbol = '<';
    stats.isFlying = true;
    stats.targetsAir = true;
    return stats;
}
