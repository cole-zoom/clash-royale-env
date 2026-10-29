#pragma once

// Process-wide switches for the client-mechanics fixes of 2026-10-29 (bot-77 sim fidelity, P2). Each is on by
// default; switching one off restores the engine's earlier behaviour, so a fix can be measured alone and compared
// against lab battles (bindings: set_target_queueing, set_aoe_lands_without_target, set_crown_tower_sight_bonus,
// set_client_projectiles, set_client_bodies, set_king_activation_delay). Values come from the client's logic tables
// (ClientMechanics.h, generated) and globals.csv.
struct Fidelity {
    // USE_TARGET_QUEUEING / CURRENT_TARGET_IGNORES_PENDING_DAMAGE (globals.csv, both TRUE): a unit choosing a new
    // target skips one that damage already in flight will kill; it keeps shooting its current target regardless.
    static inline bool targetQueueing = true;
    // ALLOW_AOE_ATTACKS_WITHOUT_TARGET (TRUE): an area shot (splash, piercing line, Firecracker's burst) whose
    // target dies in flight still lands where the target was.
    static inline bool aoeLandsWithoutTarget = true;
    // EXTRA_SIGHT_RANGE_TO_CROWN_TOWERS (2000): troops see crown towers 2 tiles further than their SightRange.
    static inline bool crownTowerSightBonus = true;
    // Each unit's projectile Speed and Homing from the client (was 1.5 tiles a tick for troops, 2.0 for towers,
    // every shot homing; buildings hit at once). Non-homing area shots land where the target stood at launch.
    static inline bool clientProjectiles = true;
    // Each unit's CollisionRadius and Mass from the client (was 0.4 for every troop, 1.0 for every building, 1.5 /
    // 2.0 for Princess / King; equal pushes): attack reach, body spacing, who pushes whom.
    static inline bool clientBodies = true;
    // The King shoots CLIENT_KING_ACTIVATION_MS (3.3 s) after its trigger (damage or a Princess Tower lost), not at
    // once (king_tower.toml: WaitForKingTowerActivation -> ActionDuration).
    static inline bool kingActivationDelay = true;
};
