#pragma once

// The fraction of a spell's (or a troop's direct hit's) damage it deals to a Crown Tower, from the installed
// client's logic tables (2026-10-28; bot-77 scripts/audit_client_stats.py): a projectile's or damage buff's
// CrownTowerDamagePercent, or an area object's TowerDamage / BaseDamage. Was RoyaleAPI cr-api-data (2023-12),
// whose values had drifted: Fireball 0.30 -> 0.23, The Log 0.20 -> 0.13, Arrows 0.30 -> 0.20, ...
// -1 for cards with no such value (they keep 100%).
inline float crownTowerFractionForCard(int cardId) {
    switch (cardId) {
        case 3: return 0.20f;   // Arrows: ArrowsSpell -80%
        case 7: return 0.23f;   // Fireball: FireballSpell -77%
        case 29: return 0.2533f;  // Zap: TowerDamage 19 / BaseDamage 75
        case 30: return 0.23f;  // Rocket: RocketSpell -77%
        case 31: return 0.25f;  // Lightning: LighningSpell -75%
        case 33: return 0.13f;  // The Log: LogProjectileRolling -87%
        case 100: return 0.25f;  // Giant Snowball: SnowballSpell -75%
        case 102: return 0.2857f;  // Goblin Curse: CrownTowerDamagePerHit 4 / DamagePerSecond 14
        case 106: return 0.30f;  // Tornado: BUFF.Tornado -70%
        case 107: return 0.2586f;  // Freeze: TowerDamage 15 / BaseDamage 58
        case 108: return 0.2571f;  // Rage: RageDamage TowerDamage 18 / BaseDamage 70
        case 124: return 0.2533f;  // Zap (Evolution)
        case 158: return 0.25f;  // Giant Snowball (Evolution)
        case 52: return 0.20f;  // Miner: CHARACTER.Miner -80%
        default: return -1.0f;
    }
}
