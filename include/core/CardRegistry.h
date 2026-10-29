#pragma once
#include "SpellTowerDamage.h"
#include "SpawnFormations.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include "Board.h"
#include "CardStats.h"
#include "CardFactories.h"
#include "FreezeOnHit.h"
#include "SpawnOnDeath.h"
#include "AreaDamageOnDeath.h"
#include "DelayedAreaDamageOnDeath.h"
#include "MoveSlowOnHit.h"
#include "CompositeDeathEffect.h"
#include "PeriodicSpawnEffect.h"
#include "CurseOnHit.h"
#include "AreaBuffOnDeath.h"
#include "AreaStunEffect.h"
#include "ElixirGrantEffect.h"
#include "EnemyElixirGrantOnDeath.h"
#include "OwnElixirGrantOnDeath.h"
#include "SpawnOnDeathForEnemyTeam.h"
#include "ProximityGatedPeriodicSpawnEffect.h"
#include "CursedHogOnHit.h"
#include "MightyMinerEscapeEffect.h"
#include "GoldenKnightDashEffect.h"
#include "SkeletonKingSoulSummonEffect.h"
#include "ArcherQueenCloakEffect.h"
#include "MonkDeflectEffect.h"
#include "SpawnOnAbility.h"
#include "GoblinsteinLightningLinkEffect.h"
#include "BossBanditGetawayGrenadeEffect.h"
#include "CappedSpawnOnHitEffect.h"
#include "PoisonOnHit.h"
#include "PeriodicFreezeNearestEffect.h"
#include "DarkGuardOnDamageEffect.h"
#include "HeroMiniPekkaBoostEffect.h"
#include "HeroKnightTauntEffect.h"
#include "HeroWizardFieryFlightEffect.h"
#include "HeroGiantHurlEffect.h"
#include "HeroMegaMinionWarpEffect.h"
#include "HeroMagicArcherTripleThreatEffect.h"
#include "HeroIceGolemSnowstormEffect.h"
#include "HeroBarbarianBarrelRerollEffect.h"

// A registered card. spawnEntity is one of the archetype factories in
// CardFactories.h bound to the card's CardStats.
struct CardDefinition {
    int id;
    std::string name;
    float cost;
    bool isSpell;
    // True only for DefensiveBuilding cards. Towers are not registered, and
    // neither are spawned helper bodies, so "not in the registry" never means
    // "a tower" (see DamageDealtEvent::targetIsTower).
    bool isBuilding;
    // The footprint isValidPlacement keeps clear of existing buildings
    // (CardFactories::placementRadius). Unused for spells.
    float placementRadius;
    // Rolling-sweep shape (The Log, Barbarian Barrel; 0 otherwise), emitted in
    // GameLogger's cardMeta so the viewer, which cannot derive engine geometry,
    // draws it from the replay.
    float rollWidth;
    float rollRange;
    // Miner, Goblin Drill: placeable anywhere (GameManager::isValidPlacement).
    bool deployAnywhere;
    // Barbarian Barrel: a spell cast on its own side only, as a troop is
    // (GameManager::isValidPlacement). False for every other card.
    bool castOwnSideOnly = false;
    // Surfaced at registry level so deck contents can be inspected before
    // anything is placed.
    bool isChampion;
    // Champion-slot code checks `isChampion || isHero` throughout.
    bool isHero = false;
    // At registry level because Hero Goblins' post-death ability fires when
    // nothing is alive to read them from. 0 / nullptr for every card without a
    // post-death ability.
    float abilityElixirCost = 0.0f;
    int abilityUsableAfterDeathTicks = 0;
    std::shared_ptr<IPeriodicEffect> postDeathAbilityEffect;
    // Display metadata for GameLogger's cardMeta block, not used by gameplay
    // (spawnEntity captures CardStats). hp is 0 for spells.
    int hp = 0;
    char symbol = '?';
    bool isFlying = false;
    std::function<void(float x, float y, int team, Board& board)> spawnEntity;

    // Evolution slots (addEvolution, PlayerState::playCard). Cost and placement
    // come from the base stats, since an Evolution changes neither;
    // spawnEvolvedEntity is the only extra.
    bool isEvolution = false;
    int evolutionCycleThreshold = 0;
    int evolvedUsesGranted = 0;
    std::function<void(float x, float y, int team, Board& board)> spawnEvolvedEntity;
};

// Exclusive upper bound on card ids: [0, CARD_ID_COUNT). Kept a few ids above
// the highest registered one (175). Defined here, next to the ids it bounds, so
// ClashEnv (NUM_CARD_IDS, bound to Python) and GameManager share one
// definition.
inline constexpr int CARD_ID_COUNT = 185;

// A client projectile speed (the same tiles-per-minute unit as a troop's Speed)
// in engine tiles per tick, through the troop conversion: HunterProjectile and
// FirecrackerExplosion both fly at 550, 1.215 tiles a tick.
inline constexpr float clientProjectileSpeed(float clientSpeed) {
    return clientSpeed * REAL_TILES_PER_MIN_TO_ENGINE * MOVEMENT_SPEED_SCALE;
}
// Hunter: 10 pellets, 20-degree fan, 6.5 tiles, radius 0.3, stop on first hit.
inline constexpr PelletSpread HUNTER_PELLETS{ 10, 20.0f, 6.5f, 0.3f, clientProjectileSpeed(550.0f), false };
// Firecracker: 5 sparks, 30-degree fan, 5 tiles past the burst, radius 0.4, piercing.
inline constexpr PelletSpread FIRECRACKER_SPARKS{ 5, 30.0f, 5.0f, 0.4f, clientProjectileSpeed(550.0f), true };

class CardRegistry {
private:
    std::unordered_map<int, CardDefinition> cards;
    // A read-only copy of each registered card's stats (ClashEnv's get_card_stats), for checking them against
    // published data. Gameplay never reads it: spawnEntity captures its own copy.
    std::unordered_map<int, CardStats> statsById;

    static CardStats troop(int id, std::string name, float cost, Archetype archetype,
        int hp, float speed, float attackRange, int damage, int attackCooldown, char symbol) {
        CardStats s;
        s.id = id; s.name = std::move(name); s.cost = cost; s.archetype = archetype;
        // `speed` arrives in the engine's original units; MOVEMENT_SPEED_SCALE
        // converts it (CardStats.h).
        s.hp = hp; s.speed = speed * MOVEMENT_SPEED_SCALE; s.attackRange = attackRange;
        s.damage = damage; s.attackCooldown = attackCooldown; s.symbol = symbol;
        return s;
    }

    static CardStats building(int id, std::string name, float cost,
        int hp, char symbol, float attackRange, int damage, int attackCooldown) {
        CardStats s;
        s.id = id; s.name = std::move(name); s.cost = cost; s.archetype = Archetype::DefensiveBuilding;
        s.hp = hp; s.symbol = symbol; s.attackRange = attackRange;
        s.damage = damage; s.attackCooldown = attackCooldown;
        return s;
    }

    static CardStats spell(int id, std::string name, float cost,
        float radius, int damage, int delayTicks, char symbol) {
        CardStats s;
        s.id = id; s.name = std::move(name); s.cost = cost; s.archetype = Archetype::Spell;
        s.spellRadius = radius; s.damage = damage; s.spellDelayTicks = delayTicks; s.symbol = symbol;
        return s;
    }

    // Skeleton Army's 15 skeletons, on a 4-wide grid spaced 0.6.
    static std::vector<Vector2D> skeletonArmyOffsets() {
        std::vector<Vector2D> offsets;
        for (int i = 0; i < 15; ++i) {
            float ox = (static_cast<float>(i % 4) - 1.5f) * 0.6f;
            float oy = (static_cast<float>(i / 4) - 1.0f) * 0.6f;
            offsets.push_back({ ox, oy });
        }
        return offsets;
    }
    // Skeleton Army Evolution: the same grid plus its last cell, 16 total (the
    // 15 plus the Skeleton General).
    static std::vector<Vector2D> skeletonArmyEvolvedOffsets() {
        std::vector<Vector2D> offsets = skeletonArmyOffsets();
        offsets.push_back({ 0.9f, 1.2f });
        return offsets;
    }

    // Golemite: spawned only by a Golem's death, never registered.
    static CardStats golemiteStats() {
        // SPEED_SLOW, the tier of the Golem it splits from.
        return troop(-1, "Golemite", 0.0f, Archetype::MeleeBuildingTargeter, 1039, SPEED_SLOW, 0.25f, 84, 25, 'q')
            .withOffsets({ {-0.3f, 0.0f}, {0.3f, 0.0f} }).withSightRange(7.0f);
    }

    // Child units spawned by death, periodic and spell effects. They reuse the
    // stats of an already-registered card where one exists, and use negative
    // ids clear of -1 (Golemite) and the tower ids -2/-3.
    //
    // A child whose unit is also a playable card takes that card's SPEED_*
    // tier, never a literal; pinned by "every spawned unit moves at the speed
    // of its own playable card" in tests/core/test_card_registry.cpp.
    static CardStats battleRamBarbarianStats() {
        return troop(-10, "Barbarians", 0.0f, Archetype::MeleeSquad, 716, SPEED_MEDIUM, 0.7f, 192, 14, 'B')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} });
    }
    // client skeleton_balloon.toml (2026-10-28): the barrel is Kamikaze -- it pops on reaching its building
    // (Range 0.35, Damage 0) or when shot down, SkeletonBalloonExplosion 64 -> 164 in a 2.0 radius, and
    // SkeletonBalloonDeathSpawn drops 7 Skeletons within 1.48 (the ring layout here is not sourced).
    static std::shared_ptr<IDeathEffect> skeletonBarrelDeath() {
        return std::make_shared<CompositeDeathEffect>(std::vector<std::shared_ptr<IDeathEffect>>{
            std::make_shared<AreaDamageOnDeath>(2.0f, 164),
            std::make_shared<SpawnOnDeath>(skeletonBarrelSkeletonStats().withOffsets({
                {-0.6f,0.0f},{0.6f,0.0f},{-0.3f,0.3f},{0.3f,0.3f},{-0.3f,-0.3f},{0.3f,-0.3f},{0.0f,0.0f}
            }))});
    }
    static CardStats skeletonBarrelSkeletonStats() {
        return troop(-11, "Skeletons", 0.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 'k')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} });
    }
    // The death spawn is one Bat, not three.
    static CardStats nightWitchBatStats() {
        // SPEED_VERY_FAST, matching playable card 78 (the same unit).
        return troop(-12, "Bats", 0.0f, Archetype::MeleeSquad, 81, SPEED_VERY_FAST, 1.2f, 81, 12, 't')
            .withFlying().withTargetsAir();
    }

    // Periodic-spawn children (Witch, Night Witch, Furnace, Barbarian Hut,
    // Goblin Hut, Goblin Drill). Goblin Hut's count and the intervals are not
    // sourced.
    static CardStats witchSkeletonStats() {
        return troop(-13, "Skeletons", 0.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 'k')
            .withOffsets({ {-0.4f, -0.4f}, {0.4f, -0.4f}, {-0.4f, 0.4f}, {0.4f, 0.4f} });
    }
    static CardStats nightWitchPeriodicBatStats() {
        // SPEED_VERY_FAST, as the -12 Bats.
        return troop(-14, "Bats", 0.0f, Archetype::MeleeSquad, 81, SPEED_VERY_FAST, 1.2f, 81, 12, 't')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} })
            .withFlying().withTargetsAir();
    }
    // The Furnace's spirit is the playable Fire Spirit (card 73): one kamikaze
    // hit of area damage, then gone. Without withDieAfterFirstHit it was a
    // permanent 207-per-second turret (UPSTREAM_REQUESTS.md item 32b).
    static CardStats furnaceFireSpiritStats() {
        return troop(-15, "Fire Spirit", 0.0f, Archetype::RangedSquad, 215, SPEED_VERY_FAST, 2.5f, 215, 10, '<')
            .withTargetsAir()
            .withSplash(2.3f)
            .withDieAfterFirstHit();
    }
    static CardStats barbarianHutBarbarianStats() {
        return troop(-16, "Barbarians", 0.0f, Archetype::MeleeSquad, 716, SPEED_MEDIUM, 0.7f, 192, 14, 'B')
            .withOffsets({ {0.0f, 0.0f}, {-0.5f, -0.5f}, {0.5f, -0.5f} });
    }
    // The playable Spear Goblins (card 23), air included (UPSTREAM_REQUESTS.md
    // item 30), deploying in 0.5 s rather than 1 (official August 2025 notes:
    // "Spear Goblin Deploy Delay: 1sec -> 0.5sec").
    static CardStats goblinHutSpearGoblinStats() {
        return troop(-17, "Spear Goblins", 0.0f, Archetype::RangedSquad, 133, SPEED_VERY_FAST, 5.0f, 81, 16, 'S')
            .withTargetsAir()
            .withDeployTime(5);
    }
    static CardStats goblinDrillGoblinStats() {
        return troop(-18, "Goblins", 0.0f, Archetype::MeleeSquad, 202, SPEED_VERY_FAST, 0.5f, 125, 11, 'g');
    }
    // Phoenix's one-time revive: a second Phoenix with no death effect of its
    // own. The real card leaves a vulnerable egg for ~4 s; this revives
    // immediately at full hp.
    static CardStats phoenixReviveStats() {
        return troop(-19, "Phoenix", 0.0f, Archetype::MeleeSquad, 1052, SPEED_MEDIUM, 1.6f, 217, 10, '7')
            .withFlying().withTargetsAir();
    }
    // Spell-spawn children (Goblin Barrel, Royal Delivery, Graveyard).
    static CardStats goblinBarrelGoblinStats() {
        return troop(-20, "Goblins", 0.0f, Archetype::MeleeSquad, 202, SPEED_VERY_FAST, 0.5f, 125, 11, 'g')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f}, {0.0f, 0.4f} });
    }
    static CardStats graveyardSkeletonStats() {
        return troop(-21, "Skeletons", 0.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 'k');
    }
    static CardStats royalDeliveryRecruitStats() {
        return troop(-22, "Royal Recruits", 0.0f, Archetype::MeleeSquad, 547, SPEED_MEDIUM, 1.6f, 133, 13, '@')
            .withShield(240); // Royal Recruits' shield
    }
    static CardStats barbarianBarrelBarbarianStats() {
        return troop(-23, "Barbarians", 0.0f, Archetype::MeleeSquad, 716, SPEED_MEDIUM, 0.7f, 192, 14, 'B');
    }
    // Hero Barbarian Barrel makes the spawned Barbarian the Hero (the barrel
    // spell has no persistent entity): barbarianBarrelBarbarianStats plus
    // "Rowdy Reroll" -- a 3-tile reroll (May 2026) down the barrel's own 2.6
    // corridor, 232 like the barrel, healing half the damage it deals.
    // withAbilitySlotCard(174): this body spawns ~17 ticks after the play, as
    // a -47 helper, so the slot must be told whose ability it carries.
    static CardStats heroBarbarianBarrelBarbarianStats() {
        return troop(-47, "Hero Barbarian Barrel", 0.0f, Archetype::MeleeSquad, 716, SPEED_MEDIUM, 0.7f, 192, 14, 'B')
            .withHeroAbility(1.0f, 0, std::make_shared<HeroBarbarianBarrelRerollEffect>(3.0f, 1.3f, 232, 716), 1)
            .withAbilitySlotCard(174);
    }
    // Compound-card secondary units (Goblin Machine's turret, Ram Rider's
    // crossbow, Goblin Giant's Spear Goblins, the ranged halves of Goblin Gang
    // and Rascals), spawned via CardStats::secondaryUnit at the same point and
    // targeting independently.
    //
    // Goblin Machine's rocket: 304 per shot on a 35-tick cooldown (DPS 86),
    // against the arms' 212 / 12 ticks.
    static CardStats goblinMachineTurretStats() {
        return troop(-24, "Goblin Machine", 0.0f, Archetype::RangedSquad, 2150, 0.5f, 5.0f, 304, 35, '3')
            .withSplash(1.5f).withTargetsAir();
    }
    // The crossbow: 104 per shot on 11 ticks (DPS 94), against the Ram's 250 /
    // 17. withIgnoresRiver matches the Ram, or the crossbow is left on the near
    // bank.
    static CardStats ramRiderCrossbowStats() {
        return troop(-25, "Ram Rider", 0.0f, Archetype::RangedSquad, 1766, 0.5f, 5.0f, 104, 11, '"')
            .withTargetsAir()
            .withIgnoresRiver();
    }
    static CardStats goblinGiantSpearGoblinsStats() {
        return troop(-26, "Spear Goblins", 0.0f, Archetype::RangedSquad, 133, SPEED_VERY_FAST, 5.0f, 81, 16, 'S')
            .withOffsets({ {-0.4f, 0.3f}, {0.4f, 0.3f} }).withTargetsAir();
    }

    // Further death-spawn children, from a later research pass. Ids continue
    // from -29.
    static CardStats lavaHoundPupStats() {
        return troop(-29, "Lava Pups", 0.0f, Archetype::MeleeSquad, 217, 0.5f, 1.6f, 81, 17, 'y')
            .withOffsets({ {-0.8f, 0.0f}, {0.8f, 0.0f}, {-0.5f, 0.7f}, {0.5f, 0.7f}, {-0.5f, -0.7f}, {0.5f, -0.7f} })
            .withFlying().withTargetsAir();
    }
    static CardStats suspiciousBushGoblinStats() {
        return troop(-30, "Bush Goblins", 0.0f, Archetype::MeleeSquad, 338, SPEED_MEDIUM, 0.8f, 256, 14, 'g')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} });
    }
    static CardStats goblinCageBrawlerStats() {
        return troop(-31, "Goblin Brawler", 0.0f, Archetype::MeleeSquad, 1080, 0.7f, 0.8f, 337, 11, 'o');
    }
    // Elixir Golem's split chain: Golem -> 2 Golemites -> 2 Blobs each. Each
    // tier spawns the next and grants the OPPONENT elixir: 1.0 from the Golem,
    // 0.5 from each Golemite and each Blob, 4.0 in total. The Blob is the base
    // case.
    static CardStats elixirBlobStats() {
        return troop(-33, "Elixir Blob", 0.0f, Archetype::MeleeBuildingTargeter, 360, 0.7f, 0.5f, 64, 11, 'e')
            .withDeathEffect(std::make_shared<EnemyElixirGrantOnDeath>(0.5f)).withSightRange(7.5f);
    }
    static CardStats elixirGolemiteStats() {
        return troop(-32, "Elixir Golemite", 0.0f, Archetype::MeleeBuildingTargeter, 762, 0.5f, 1.0f, 128, 11, 'e')
            .withDeathEffect(std::make_shared<CompositeDeathEffect>(
                std::vector<std::shared_ptr<IDeathEffect>>{
                    std::make_shared<SpawnOnDeath>(
                        elixirBlobStats().withOffsets({ {-0.3f, 0.0f}, {0.3f, 0.0f} })),
                    std::make_shared<EnemyElixirGrantOnDeath>(0.5f)
                })).withSightRange(7.5f);
    }
    // Mother Witch's Cursed Hog, spawned for the opposite team via
    // SpawnOnDeathForEnemyTeam (see CursedHogOnHit).
    static CardStats cursedHogStats() {
        return troop(-34, "Cursed Hog", 0.0f, Archetype::MeleeBuildingTargeter, 629, 1.0f, 0.75f, 53, 12, 'h');
    }
    // The single Barbarian released when the Hut dies; the periodic spawn's
    // stats.
    static CardStats barbarianHutDeathBarbarianStats() {
        return troop(-35, "Barbarians", 0.0f, Archetype::MeleeSquad, 716, SPEED_MEDIUM, 0.7f, 192, 14, 'B');
    }
    // Goblin Drill's death spawn: two Goblins (three before a 2021 balance
    // patch).
    static CardStats goblinDrillDeathGoblinStats() {
        return troop(-36, "Goblins", 0.0f, Archetype::MeleeSquad, 202, SPEED_VERY_FAST, 0.5f, 125, 11, 'g')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} });
    }
    // Goblin Demolisher's transformed form: a Very Fast, building-only kamikaze
    // with a 2.5-radius, 404-damage burst on its first hit, spawned when the
    // ranged form kills itself at <= 50% hp (CombatEntity::transformKillsSelf).
    // Reuses the ranged form's 1300 hp (not sourced). The real 10 s lifetime
    // cap is not modelled; dieAfterFirstHit bounds it in practice.
    static CardStats goblinDemolisherKamikazeStats() {
        return troop(-37, "Kamikaze Goblin Demolisher", 0.0f, Archetype::MeleeBuildingTargeter,
            1300, 1.0f, 0.5f, 404, 10, '&')
            .withSplash(2.5f).withDieAfterFirstHit();
    }
    // Skeleton King's summoned skeletons, spawned one at a time in a loop by
    // SkeletonKingSoulSummonEffect.
    static CardStats skeletonKingSummonedSkeletonStats() {
        return troop(-38, "Skeletons", 0.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 'k');
    }
    // Little Prince's Royal Rescue: the Guardienne. Her spawn-in dash and
    // knockback are not modelled.
    static CardStats guardienneStats() {
        return troop(-39, "Guardienne", 0.0f, Archetype::MeleeSquad, 1600, 0.5f, 1.2f, 217, 12, 'u')
            .withCharge(3.0f, 2.0f);
    }
    // Hero Musketeer's "Trusty Turret": a short-range turret that
    // self-destructs after 10 s. withHpTransform with a fraction of exactly 1.0
    // makes the HP-threshold mechanism a plain lifetime timer.
    // becomesStationary stays false: the archetype is already stationary, and
    // that branch's freeze would silence the turret. hp and damage are not
    // sourced.
    static CardStats heroMusketeerTurretStats() {
        return building(-45, "Trusty Turret", 0.0f, 200, 't', 4.0f, 90, 5)
            .withTargetsAir()
            .withHpTransform(1.0f, 100, false);
    }
    // Hero Magic Archer's decoy: soaks hits and draws aggro but deals no
    // damage; a zero-damage, stationary unit. hp and lifetime are not sourced.
    static CardStats heroMagicArcherDecoyStats() {
        return troop(-46, "Decoy", 0.0f, Archetype::MeleeSquad, 100, 0.0f, 1.0f, 0, 100, 'd');
    }
    // Lumberjack Evolution's death-spawn ghost. The real one is invincible
    // while inside its own Rage zone; here it is just invisible.
    static CardStats lumberjackGhostStats() {
        return troop(-44, "Lumberjack Ghost", 0.0f, Archetype::MeleeSquad, 400, 1.0f, 0.7f, 256, 8, 'l')
            .withInvisibility(10);
    }
    static CardStats runnerStats() {
        // Wall Breakers Evolution's Runner. hp scaled to level 11 from level-6
        // data (103 * 1.1^5 ~= 166); damage 175 is the sourced 50% of Wall
        // Breakers' 350. Modelled as an ordinary building-targeter that keeps
        // attacking, not a single detonation, and without the real reduced
        // damage against Crown Towers.
        return troop(-41, "Runner", 0.0f, Archetype::MeleeBuildingTargeter, 166, 1.2f, 0.6f, 175, 8, 'g');
    }
    // Battle Ram Evolution's death spawn: a stronger pair of Barbarians.
    static CardStats battleRamEvolvedBarbarianStats() {
        return troop(-10, "Barbarians", 0.0f, Archetype::MeleeSquad, 830, SPEED_MEDIUM, 0.7f, 230, 14, 'B')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} });
    }
    // Evolved Skeletons' "Never-ending Horde" child. Reuses id 24 on purpose:
    // CappedSpawnOnHitEffect counts by cardId, so the three originals and every
    // child form one population capped at the sourced 8. Children carry no
    // spawn-on-hit of their own, so growth has at most three sources.
    static CardStats evolvedSkeletonChildStats() {
        return troop(24, "Skeletons", 0.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 'k');
    }
    // Skeleton Army Evolution's death-spawn shadow. The real shadows have
    // unlimited hp and are targetable only by spells; here a targetable 500-hp
    // unit.
    static CardStats skeletonArmyShadowStats() {
        return troop(-43, "Skeleton Shadow", 0.0f, Archetype::MeleeSquad, 500, 1.0f, 0.5f, 81, 11, 'k');
    }
    // Royal Ghost Evolution's on-hit Souldier: 51% of her damage (133) and 6.7%
    // of her hp (81), per the sourced table.
    static CardStats royalGhostSouldierStats() {
        return troop(-42, "Souldier", 0.0f, Archetype::MeleeSquad, 81, 0.7f, 1.2f, 133, 18, 'Q');
    }

    // Spells hit a Crown Tower for a published fraction of their damage (SpellTowerDamage.h, generated from
    // RoyaleAPI cr-api-data); a card's own explicit value (Poison's 21) wins. `id`: whose fraction (an
    // Evolution uses its base card's).
    // ... and squads deploy in the formation measured on the replays (SpawnFormations.h).
    static CardStats withCrownTowerDamage(CardStats stats, int id) {
        if (stats.archetype == Archetype::Spell && stats.spellCrownTowerDamage < 0) {
            const float frac = crownTowerFractionForCard(id);
            if (frac >= 0.0f) stats.spellCrownTowerDamage = static_cast<int>(stats.damage * frac + 0.5f);
        }
        stats.spawnOffsets = fittedOffsets(id, stats.spawnOffsets);
        return stats;
    }

    void add(const CardStats& statsIn) {
        const CardStats stats = withCrownTowerDamage(statsIn, statsIn.id);
        statsById[stats.id] = stats;
        CardDefinition def;
        def.id = stats.id;
        def.name = stats.name;
        def.cost = stats.cost;
        def.isSpell = (stats.archetype == Archetype::Spell);
        def.isBuilding = (stats.archetype == Archetype::DefensiveBuilding);
        def.placementRadius = CardFactories::placementRadius(stats.archetype);
        def.rollWidth = stats.spellRollWidth;
        def.rollRange = stats.spellRollRange;
        def.deployAnywhere = stats.deployAnywhere;
        def.castOwnSideOnly = stats.spellOwnSideOnly;
        def.isChampion = stats.isChampion;
        def.isHero = stats.isHero;
        def.abilityElixirCost = stats.abilityElixirCost;
        def.abilityUsableAfterDeathTicks = stats.abilityUsableAfterDeathTicks;
        def.postDeathAbilityEffect = stats.postDeathAbilityEffect;
        def.hp = stats.hp;
        def.symbol = stats.symbol;
        def.isFlying = stats.isFlying;
        def.spawnEntity = [stats](float x, float y, int team, Board& board) {
            CardFactories::spawn(stats, x, y, team, board);
        };
        cards[stats.id] = std::move(def);
    }

    // Registers an Evolution slot: `evolutionId` is what a deck holds instead
    // of the base card's id. Cost, spell/building flags, footprint and
    // deployAnywhere come from baseStats, since an Evolution changes only
    // combat behaviour. The slot is played cycleThreshold times un-evolved,
    // then evolvedUses times evolved, repeating all match (see
    // PlayerState::EvolutionSlotState).
    void addEvolution(int evolutionId, CardStats baseStats, CardStats evolvedStats,
            int cycleThreshold, int evolvedUses) {
        baseStats = withCrownTowerDamage(baseStats, baseStats.id);
        evolvedStats = withCrownTowerDamage(evolvedStats, baseStats.id);
        CardDefinition def;
        def.id = evolutionId;
        def.name = baseStats.name;
        def.cost = baseStats.cost;
        def.isSpell = (baseStats.archetype == Archetype::Spell);
        def.isBuilding = (baseStats.archetype == Archetype::DefensiveBuilding);
        def.placementRadius = CardFactories::placementRadius(baseStats.archetype);
        def.rollWidth = baseStats.spellRollWidth;
        def.rollRange = baseStats.spellRollRange;
        def.deployAnywhere = baseStats.deployAnywhere;
        def.castOwnSideOnly = baseStats.spellOwnSideOnly;
        def.isChampion = baseStats.isChampion;
        def.isHero = baseStats.isHero;
        def.abilityElixirCost = baseStats.abilityElixirCost;
        def.abilityUsableAfterDeathTicks = baseStats.abilityUsableAfterDeathTicks;
        def.postDeathAbilityEffect = baseStats.postDeathAbilityEffect;
        def.hp = baseStats.hp;
        def.symbol = baseStats.symbol;
        def.isFlying = baseStats.isFlying;
        def.spawnEntity = [baseStats](float x, float y, int team, Board& board) {
            CardFactories::spawn(baseStats, x, y, team, board);
        };
        def.isEvolution = true;
        def.evolutionCycleThreshold = cycleThreshold;
        def.evolvedUsesGranted = evolvedUses;
        def.spawnEvolvedEntity = [evolvedStats](float x, float y, int team, Board& board) {
            CardFactories::spawn(evolvedStats, x, y, team, board);
        };
        cards[evolutionId] = std::move(def);
    }

    CardRegistry() {
        // Stats are level-11 tournament-standard data (cost, hp, damage, hit
        // speed, range, count). Speeds are the SPEED_* tiers (CardStats.h).

        // === Melee troops ===
        add(troop(0, "Knight", 3.0f, Archetype::MeleeSquad, 1766, SPEED_MEDIUM, 1.2f, 202, 12, 'K'));

        // client goblin.toml Damage 49 -> 125 (was 120) (2026-10-28)
        add(troop(4, "Goblins", 2.0f, Archetype::MeleeSquad, 202, SPEED_VERY_FAST, 0.5f, 125, 11, 'g')
            .withOffsets({ {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f} }));

        add(troop(5, "Mini PEKKA", 4.0f, Archetype::MeleeSquad, 1390, SPEED_FAST, 0.8f, 755, 16, 'M'));

        add(troop(8, "Barbarians", 5.0f, Archetype::MeleeSquad, 716, SPEED_MEDIUM, 0.7f, 192, 14, 'B')
            .withOffsets({ {0.0f, 0.0f}, {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f} }));

        add(troop(10, "Valkyrie", 4.0f, Archetype::MeleeSquad, 1907, SPEED_MEDIUM, 1.2f, 266, 15, 'V')
            .withSplash(2.0f)); // 360-degree swing

        add(troop(12, "Skeleton Army", 3.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 's')
            .withOffsets(skeletonArmyOffsets()));

        add(troop(13, "P.E.K.K.A.", 7.0f, Archetype::MeleeSquad, 3760, SPEED_SLOW, 1.2f, 842, 18, 'E').withSightRange(5.0f));

        // Prince jumps the river (withIgnoresRiver). The charge distance and
        // damage are not sourced; the charge speed is (2x, the game files'
        // charge_speed_multiplier 200; UPSTREAM_REQUESTS.md item 35).
        add(troop(14, "Prince", 5.0f, Archetype::MeleeSquad, 1920, SPEED_MEDIUM, 1.6f, 391, 14, 'p')
            .withCharge(3.0f, 2.0f).withChargeSpeed(2.0f)
            .withIgnoresRiver());

        // client angrybarbarian.toml SightRange 6.0 (was 5.5) (2026-10-28)
        add(troop(17, "Elite Barbarians", 6.0f, Archetype::MeleeSquad, 1341, SPEED_FAST, 1.2f, 384, 14, 'e').withSightRange(6.0f)
            .withOffsets({ {-0.5f, 0.0f}, {0.5f, 0.0f} }));

        // The death-potion radius, multiplier and duration are not sourced.
        add(troop(21, "Lumberjack", 4.0f, Archetype::MeleeSquad, 1282, SPEED_VERY_FAST, 0.7f, 256, 8, 'l')
            .withDeathEffect(std::make_shared<AreaBuffOnDeath>(2.5f, 1.75f, 55)));

        add(troop(24, "Skeletons", 1.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 'k')
            .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f} }));

        // Giant Skeleton, after the 2026-05-04 rework: hp -7% (3361 x 0.93),
        // and a death bomb that lands where he dies and explodes 3.0 s later
        // (fuse 29: see DelayedAreaDamageOnDeath) in a 3-tile radius, for
        // 688 x 1.29 ~= 886, the same against a Crown Tower since the rework
        // removed its 2x. The bomb's knockback (1.0, Fireball's) is not sourced.
        // UPSTREAM_REQUESTS.md item 32c.
        // client giantskeleton.toml Hitpoints 1313 -> 3361 (was 3126) (2026-10-28)
        add(troop(39, "Giant Skeleton", 6.0f, Archetype::MeleeSquad, 3361, SPEED_MEDIUM, 0.8f, 276, 13, 'J')
            .withDeathEffect(std::make_shared<DelayedAreaDamageOnDeath>(3.0f, 886, 29, 1.0f, 39, "Giant Skeleton Bomb"))
            .withSightRange(5.0f));

        // Electro Wizard: the zap has no travel time, so the archetype is
        // MeleeSquad (archetypes describe how damage lands here, not range).
        // Splits to the 2 closest enemies (each takes damage/2; full damage if
        // only one is in range). Each hit and the deploy zap (radius 3) stun
        // for 0.5 s.
        add(troop(35, "Electro Wizard", 4.0f, Archetype::MeleeSquad, 714, SPEED_FAST, 5.0f, 118, 18, 'z')
            .withTargetsAir()
            .withSplitTargets(2)
            .withOnHit(std::make_shared<FreezeOnHit>(5, 0.0f))
            .withSpawnEffect(3.0f, 118, std::make_shared<FreezeOnHit>(5, 0.0f)));

        // Flying troops. Minions and Minion Horde are one unit at two squad
        // sizes; Mega Minion is a single heavier flier. All hit air and ground.
        add(troop(41, "Minions", 3.0f, Archetype::MeleeSquad, 230, SPEED_FAST, 2.5f, 107, 12, 'm')
            .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f} })
            .withFlying().withTargetsAir());

        add(troop(42, "Minion Horde", 5.0f, Archetype::MeleeSquad, 230, SPEED_FAST, 2.5f, 107, 12, 'h')
            .withOffsets({ {-0.6f, -0.3f}, {0.0f, -0.3f}, {0.6f, -0.3f},
                           {-0.6f, 0.3f}, {0.0f, 0.3f}, {0.6f, 0.3f} })
            .withFlying().withTargetsAir());

        add(troop(43, "Mega Minion", 3.0f, Archetype::MeleeSquad, 837, SPEED_MEDIUM, 1.6f, 312, 15, 'F')
            .withFlying().withTargetsAir());

        // === Ranged troops ===
        // Each needs its own confirmed Target field before .withTargetsAir();
        // "ranged" does not imply it.
        add(troop(1, "Archers", 3.0f, Archetype::RangedSquad, 304, SPEED_MEDIUM, 5.0f, 112, 9, 'A')
            .withOffsets({ {0.0f, 0.0f}, {1.0f, 0.0f} })
            .withTargetsAir());

        add(troop(6, "Musketeer", 4.0f, Archetype::RangedSquad, 721, SPEED_MEDIUM, 6.0f, 217, 10, 'U')
            .withTargetsAir().withSightRange(6.0f));
        // Bomber is ground-only; so are Bowler, Sparky and Cannon Cart.
        // Splash radii audited against cr-api-data on 2026-10-28 (max of area_damage_radius and the projectile's
        // radius): Bomber, Witch and Mortar had none, Princess and Valkyrie were 1.5 against 2.5 / 2.0.
        // client bomber.toml projectile Damage 83 -> 212 (was 225) (2026-10-28)
        add(troop(9, "Bomber", 2.0f, Archetype::RangedSquad, 304, SPEED_MEDIUM, 4.5f, 212, 18, 'b')
            .withSplash(1.5f));
        // client wizard.toml projectile Damage 119 -> 305 (was 281) (2026-10-28)
        add(troop(11, "Wizard", 5.0f, Archetype::RangedSquad, 755, SPEED_MEDIUM, 5.5f, 305, 14, 'W')
            .withTargetsAir().withSplash(1.5f));
        // client blowdartgoblin.toml projectile Damage 56 -> 143, SightRange 7.0 (was 151, 7.5) (2026-10-28)
        add(troop(20, "Dart Goblin", 3.0f, Archetype::RangedSquad, 261, SPEED_VERY_FAST, 6.5f, 143, 8, 'd')
            .withTargetsAir().withSightRange(7.0f));
        // A piercing line: 11.5 total (4.0 range + 7.5), half-width 1.8.
        // Knockback is not modelled on the troop attack path.
        add(troop(22, "Bowler", 5.0f, Archetype::RangedSquad, 2081, SPEED_SLOW, 4.0f, 289, 25, 'w')
            .withSplash(1.8f).withLineSplash(11.5f)
            .withSightRange(5.5f)); // client bowler.toml SightRange 5500 (was 4.0) (2026-10-28)

        add(troop(23, "Spear Goblins", 2.0f, Archetype::RangedSquad, 133, SPEED_VERY_FAST, 5.0f, 81, 16, 'S')
            .withTargetsAir()
            .withOffsets({ {0.0f, 0.0f}, {0.7f, 0.0f}, {-0.7f, 0.0f} }));

        // Ice Wizard fires a projectile; the slow lands on arrival.
        // client ice_wizardProjectile Radius 1.5, AoE (was single-target) (2026-10-28)
        add(troop(34, "Ice Wizard", 3.0f, Archetype::RangedSquad, 689, SPEED_MEDIUM, 5.5f, 90, 17, 'i')
            .withTargetsAir()
            .withOnHit(std::make_shared<FreezeOnHit>(25, 0.7f)).withSplash(1.5f)); // client IceWizardSlowDown -30% for 2.5 s (was 3.0 s at -35%) (2026-10-28)

        // Executioner's axe hits on arrival and again 1.5 s later on the
        // return. The real axe pierces everything along its path both ways;
        // here it hits its one target twice (Projectile's returnsToSender).
        // client AxeManProjectile Radius 1.0, AoE (was single-target); the pierce along the path is not modelled (2026-10-28)
        add(troop(36, "Executioner", 5.0f, Archetype::RangedSquad, 1280, SPEED_MEDIUM, 4.5f, 179, 24, 'x')
            .withTargetsAir()
            .withBoomerang(15).withSplash(1.0f));

        add(troop(44, "Baby Dragon", 4.0f, Archetype::RangedSquad, 1152, SPEED_FAST, 3.5f, 168, 15, 'y')
            .withFlying().withTargetsAir()
            .withSplash(1.5f));

        // === Building targeters ===
        add(troop(2, "Giant", 5.0f, Archetype::MeleeBuildingTargeter, 3968, SPEED_SLOW, 1.2f, 253, 15, 'G').withSightRange(7.5f));

        add(troop(15, "Hog Rider", 4.0f, Archetype::MeleeBuildingTargeter, 1697, SPEED_VERY_FAST, 0.8f, 317, 16, 'H')
            .withIgnoresRiver().withSightRange(9.5f));

        // Golem splits into two Golemites and deals a death explosion: two
        // death effects composed, since deathEffect is a single slot.
        add(troop(19, "Golem", 8.0f, Archetype::MeleeBuildingTargeter, 5120, SPEED_SLOW, 0.75f, 312, 25, 'L')
            .withDeathEffect(std::make_shared<CompositeDeathEffect>(
                std::vector<std::shared_ptr<IDeathEffect>>{
                    std::make_shared<SpawnOnDeath>(golemiteStats()),
                    std::make_shared<AreaDamageOnDeath>(2.5f, 200) }))
            .withSightRange(7.5f)    // client golem.toml SightRange 7500 (was 7.0) (2026-10-28)
            .withDeployTime(30));    // client golem.toml DeployTime 3000 (2026-10-28)

        // Ice Golem: the slow is on the death explosion, as in the real card;
        // there is no on-attack slow. The 30 ticks / 0.65 are not sourced.
        // client icegolemite.toml Hitpoints 480 -> 1229 (was 1315) (2026-10-28)
        add(troop(40, "Ice Golem", 2.0f, Archetype::MeleeBuildingTargeter, 1229, SPEED_SLOW, 0.75f, 84, 25, 'c')
            .withDeathEffect(std::make_shared<AreaDamageOnDeath>(
                2.0f, 84, std::make_shared<FreezeOnHit>(30, 0.65f))).withSightRange(7.0f));

        // Balloon: flying and building-only, point-blank range; building
        // targeters never consider air, so no withTargetsAir. The real death
        // bomb drops only if shot down before bombing; here it always does.
        add(troop(45, "Balloon", 5.0f, Archetype::MeleeBuildingTargeter, 1676, SPEED_MEDIUM, 0.1f, 640, 20, 'a')
            .withFlying()
            .withDeathEffect(std::make_shared<AreaDamageOnDeath>(1.5f, 240)).withSightRange(7.7f));

        // === Ranged building targeter ===
        add(troop(18, "Royal Giant", 6.0f, Archetype::RangedBuildingTargeter, 3164, SPEED_SLOW, 5.0f, 307, 18, 'Y').withSightRange(7.5f));

        // === Defensive structures ===
        add(building(25, "Cannon", 3.0f, 824, 'C', 5.5f, 202, 10));
        add(building(26, "Tesla", 4.0f, 1182, 'T', 5.5f, 220, 11).withTargetsAir()
            .withLifetime(250)); // client tesla LifeTime 25 s (was 30) (2026-10-28)
        // withSightRange(6.0f) is not redundant with the 5.5 default: with
        // attackRange 6.0, the default would let it attack what it cannot see.
        // Pinned by test_sight_range.cpp.
        // Its bombs splash (1.5 tiles: the game files' BombTowerProjectile
        // radius 1500), and when it is destroyed it drops a bomb that explodes
        // 3.0 s later (fuse 29: see DelayedAreaDamageOnDeath) in 3 tiles for
        // its own damage (BombTowerBomb: death_damage 105 at level 1, radius
        // 3000, deploy 3000 ms). It had neither: a single-target tower. Both
        // stay on the ground, as the tower does: the splash follows its
        // targetsAir, the bomb is groundOnly. UPSTREAM_REQUESTS.md item 35.
        add(building(27, "Bomb Tower", 4.0f, 1356, 'D', 6.0f, 222, 18).withSightRange(6.0f)
            .withSplash(1.5f)
            .withDeathEffect(std::make_shared<DelayedAreaDamageOnDeath>(3.0f, 222, 29, 0.0f, 27, "Bomb Tower Bomb",
                                                                        /*groundOnly=*/true)));
        // Inferno Tower: 5% of max damage for the first 2 s, 18.75% for the
        // next 2 s, then full, reset by a target switch or a stun. `damage` is
        // the fully ramped max; getCurrentDamage() scales it.
        add(building(28, "Inferno Tower", 5.0f, 1748, 'I', 6.0f, 847, 4)
            .withTargetsAir()
            .withDamageRamp(20, 40, 44.0f / 847.0f, 0.1875f).withSightRange(6.0f)); // client 17 / 62 / 331 -> 44 / 159 / 847, 2 s each (first was 0.05 = 42) (2026-10-28)

        // === Spells ===
        // Arrows: 3 volleys of 123, not one 369 hit.
        add(spell(3, "Arrows", 3.0f, 3.5f, 123, 10, '*').withRepeats(3, 2));
        add(spell(7, "Fireball", 4.0f, 2.5f, 689, 10, 'O')
            .withKnockback(1.0f));
        add(spell(29, "Zap", 2.0f, 2.5f, 192, 3, 'Z'));
        // client RocketSpell Pushback 1800 -> 1.8 (was 1.0) (2026-10-28)
        add(spell(30, "Rocket", 6.0f, 2.0f, 1485, 15, 'r')
            .withKnockback(1.8f));
        // client lightning.toml: three bolts, each on the biggest target not yet hit (HitBiggestTargets,
        // OneHitPerTarget), each stunning 0.5 s (ZapFreeze). Was: every enemy in the radius, no stun (2026-10-28).
        add(spell(31, "Lightning", 6.0f, 3.5f, 1057, 5, 'j')
            .withSpellTopHpTargets(3)
            .withSpellOnHit(std::make_shared<FreezeOnHit>(5, 0.0f)));
        // Poison: 92 damage every second for 8 s; whoever stands in it takes
        // each pulse. A Crown Tower takes 21 per pulse (168 in all: official
        // June 2026 notes, fixed in game on 26 Aug), and enemy troops inside
        // move 15% slower -- movement only, refreshed by each pulse.
        // UPSTREAM_REQUESTS.md items 29 and 32k.
        add(spell(32, "Poison", 4.0f, 3.5f, 92, 0, 'n').withRepeats(8, 10)
            .withCrownTowerDamage(21)
            .withSpellOnHit(std::make_shared<MoveSlowOnHit>(10, 0.85f)));
        // The Log: ground-only, a rolling sweep of published width 3.9 and
        // range 10.1. The radius argument is unused for rollers and kept at 3.9
        // to match. From the bridge the 10.1 reaches an enemy Princess Tower's
        // near edge (9.00 away) but not its centre (10.50); test_area_spell.cpp
        // pins this against ArenaLayout. Speed (1.0 tiles/tick, ~1 s for the
        // roll) and knockback (0.8) are not sourced.
        add(spell(33, "The Log", 2.0f, 3.9f, 269, 8, 'o')
            .withGroundOnly()
            .withRollingSweep(10.1f, 3.9f, 1.0f, 0.7f)); // client LogProjectileRolling Pushback 700 (was 0.8) (2026-10-28)

        // --- the 2026 roster expansion (ids 46+) ---
        // Every remaining non-Champion, non-Evolution, non-Tower-Troop card,
        // from the same data. Mechanics without a hook here are noted per card;
        // cards built entirely around one are listed as excluded at the end of
        // this file.
        //
        // Symbols are cosmetic (combat keys everything off cardId) and the
        // printable ASCII range is exhausted, so later cards reuse symbols.

        // === Melee troops (expansion) ===
        // Dark Prince jumps the river.
        add(troop(46, "Dark Prince", 4.0f, Archetype::MeleeSquad, 1200, SPEED_MEDIUM, 1.2f, 266, 14, 'N')
            .withShield(240) // sourced
            .withCharge(3.0f, 2.0f) // +100% on a charging hit
            .withChargeSpeed(2.0f)  // and runs at 2x (item 35)
            .withSplash(1.1f)       // client darkprince.toml AreaDamageRadius 1100: every hit (2026-10-28)
            .withIgnoresRiver());
        // Royal Ghost jumps the river.
        // client ghost.toml Hitpoints 450 -> 1152, AreaDamageRadius 1.0 (was 1210, single-target) (2026-10-28)
        add(troop(47, "Royal Ghost", 3.0f, Archetype::MeleeSquad, 1152, SPEED_FAST, 1.2f, 261, 18, 'Q').withSplash(1.0f)
            .withInvisibility(5) // brief reveal after attacking
            .withIgnoresRiver());
        // Mega Knight: the deploy slam is the spawn-effect burst (radius 1.3,
        // 430). The periodic jump closes 3.5-5 tiles instantly with a 2.0x,
        // 2.2-radius splash hit (sourced 537 vs 268). The jump can carry him
        // over the river; with no "mid-jump" state, he ignores the river
        // always, as Bandit does. Stats are an exact level-11 row (hp 3993,
        // area 268, spawn 429).
        add(troop(48, "Mega Knight", 7.0f, Archetype::MeleeSquad, 3993, SPEED_MEDIUM, 1.2f, 268, 17, 'X')
            .withSplash(1.3f) // client megaknight.toml AreaDamageRadius 1300 (was 1.5) (2026-10-28)
            .withSpawnEffect(1.3f, 430)
            .withJump(3.5f, 5.0f, 2.0f, 2.2f)
            .withIgnoresRiver());
        // Heals: every attack, allied troops within 3 tiles for 102 (client
        // BattleHealerAll, 40/s for 1 s = 102 at level 11, there 4 pulses,
        // here one heal per landed hit); on deploy, the same radius for 202
        // (BattleHealerSpawnBuff, 79/s for 1 s; there over the first second,
        // here at once). Troops only, never buildings, and never a Battle
        // Healer: it ignores Battle Healer heals, its own and another's
        // (IgnoreBuff). No self-heal: the client's BuffWhenNotAttackingTime is
        // not modelled, as Cole ruled (2026-10-28). Battle Healer jumps the
        // river.
        // client battle_healer.toml Hitpoints 750 -> 1920, Damage 105 -> 268, HitSpeed 2.0 s, Range 1.6 (was 1717, 148, 1.5 s, 1.2) (2026-10-28)
        add(troop(49, "Battle Healer", 4.0f, Archetype::MeleeSquad, 1920, SPEED_MEDIUM, 1.6f, 268, 20, 'f')
            .withHealAura(3.0f, 102)
            .withSpawnHeal(202)
            .withIgnoresRiver());
        // Bandit crosses the river only by dashing; with no separate dash
        // state, she always ignores it.
        // client assassin.toml Range 0.75 (was 1.0) (2026-10-28)
        add(troop(50, "Bandit", 3.0f, Archetype::MeleeSquad, 906, SPEED_FAST, 0.75f, 194, 10, 'u')
            .withCharge(3.0f, 2.0f)
            .withChargeInvulnerability() // invulnerable while charging
            .withSightRange(6.0f)
            .withIgnoresRiver());
        // Berserker: a flat 0.6 s hit speed, Fast, Melee: Short (0.8). There is
        // no rage: the self-heal and hit-speed bonuses the wiki lists are event
        // Modifiers, not the card (UPSTREAM_REQUESTS.md item 32d).
        add(troop(51, "Berserker", 2.0f, Archetype::MeleeSquad, 896, SPEED_FAST, 0.8f, 102, 6, 'v'));
        // client miner.toml Range 1.2 (was 1.0) (2026-10-28)
        add(troop(52, "Miner", 3.0f, Archetype::MeleeSquad, 1210, SPEED_FAST, 1.2f, 194, 13, '0')
            .withDeployAnywhere());
        // client fisherman.toml Range 1.2 (was 1.0) (2026-10-28)
        add(troop(53, "Fisherman", 3.0f, Archetype::MeleeSquad, 870, SPEED_MEDIUM, 1.2f, 194, 13, '1')
            .withHook(6.5f).withSightRange(7.5f));
        // Parry every 3.5 s. The real parry reflects 200% damage and only
        // against ground melee; neither is modelled (takeDamage carries no
        // attacker, and there is no melee/ranged distinction).
        // client ronin.toml Speed 90, HitSpeed 1.3 s (was 63-tier, 1.4 s) (2026-10-28)
        add(troop(54, "Ronin", 5.0f, Archetype::MeleeSquad, 1779, SPEED_FAST, 1.2f, 371, 13, '2')
            .withParry(35));
        // client goblin_machine.toml Hitpoints 885 -> 2266, Damage 91 -> 233, Speed 60 (was 2150, 212, slow) (2026-10-28)
        add(troop(55, "Goblin Machine", 5.0f, Archetype::MeleeSquad, 2266, SPEED_MEDIUM, 1.2f, 233, 12, '3')
            .withSecondaryUnit(goblinMachineTurretStats())); // independently targeting rocket turret

        // Inferno Dragon and Electro Dragon: instant zaps, so MeleeSquad as
        // with Electro Wizard. Inferno Dragon ramps 36 -> 120 -> 422 every 2 s
        // on one target (client infernodragon.toml Damage 14 / VariableDamage2 47 / 3 165,
        // VariableDamageTime 2000 each, Range 3.5; was every 1.5 s, range 5.0) (2026-10-28); `damage` is the ramped max.
        add(troop(56, "Inferno Dragon", 4.0f, Archetype::MeleeSquad, 1295, SPEED_MEDIUM, 3.5f, 422, 4, '4')
            .withFlying().withTargetsAir()
            .withDamageRamp(20, 40, 36.0f / 422.0f, 120.0f / 422.0f));
        // A true chain, not splash: up to 3 distinct targets (hops up to 4
        // tiles), each taking the full 192.
        add(troop(57, "Electro Dragon", 5.0f, Archetype::MeleeSquad, 1049, SPEED_MEDIUM, 3.5f, 192, 21, '5')
            .withFlying().withTargetsAir()
            .withSplitTargets(3)
            .withSplitTargetsFullDamage()
            .withOnHit(std::make_shared<FreezeOnHit>(5, 0.0f)));

        // Night Witch: releases 3 Bats on death and spawns Bats periodically.
        // client darkwitch.toml Range 1.6 (was 1.0) (2026-10-28)
        add(troop(58, "Night Witch", 4.0f, Archetype::MeleeSquad, 906, SPEED_MEDIUM, 1.6f, 314, 13, '6')
            .withDeathEffect(std::make_shared<SpawnOnDeath>(nightWitchBatStats()))
            .withPeriodicEffect(50, std::make_shared<PeriodicSpawnEffect>(nightWitchPeriodicBatStats())));
        // Phoenix: one-time revive (phoenixReviveStats).
        // client phoenix.toml Range 1.6 (was 1.0) (2026-10-28)
        add(troop(59, "Phoenix", 4.0f, Archetype::MeleeSquad, 1052, SPEED_MEDIUM, 1.6f, 217, 10, '7')
            .withFlying().withTargetsAir()
            .withDeathEffect(std::make_shared<SpawnOnDeath>(phoenixReviveStats())));

        // === Ranged troops (expansion) ===
        // client ZapMachineProjectile Radius 1.8 (was 1.5) (2026-10-28)
        add(troop(60, "Sparky", 6.0f, Archetype::RangedSquad, 1451, SPEED_SLOW, 5.0f, 1331, 40, '8')
            .withSplash(1.8f)
            .withStunResetsCooldown() // a stun restarts her charge
            .withSightRange(5.0f));
        // client princess.toml DeployTime 1.2 s (was 1.0) (2026-10-28). Her volley is 5 arrows (MultipleProjectiles) of 169
        // landing spread; modelled as one 168 hit splashing 2.5 (her AreaDamageRadius).
        add(troop(61, "Princess", 3.0f, Archetype::RangedSquad, 261, SPEED_MEDIUM, 9.0f, 168, 30, '9')
            .withTargetsAir()
            .withSplash(2.5f).withSightRange(9.5f).withDeployTime(12));
        // Hunter: 10 pellets of 84 a shot (client hunter.toml MultipleProjectiles 10, HunterProjectile Damage 33
        // -> 84), each its own projectile gone on its first collision (CheckCollisions, ProjectileRadius 0.3),
        // flying up to 6.5 tiles (ProjectileRange) in a 20-degree fan. Point-blank on a big target: 840.
        add(troop(62, "Hunter", 4.0f, Archetype::RangedSquad, 885, SPEED_MEDIUM, 4.0f, 84, 22, '!')
            .withTargetsAir()
            .withPellets(HUNTER_PELLETS));
        // A piercing line: 11.0 long, half-width 0.25.
        // client elitearcher.toml projectile Damage -> 136 (was 143) (2026-10-28)
        add(troop(63, "Magic Archer", 4.0f, Archetype::RangedSquad, 529, SPEED_MEDIUM, 7.0f, 136, 11, '#')
            .withTargetsAir()
            .withSplash(0.25f).withLineSplash(11.0f).withSightRange(7.5f));
        // client firecracker.toml SightRange 8.0 (was 8.5) (2026-10-28)
        // The shell bursts at the target into 5 sparks of 64 (FirecrackerExplosion SpawnCount 5, Damage 25 -> 64),
        // each piercing (radius 0.4) and flying on 5 tiles (ProjectileRange) in a 30-degree fan along the shot's
        // line, so a unit behind the target is hit too. Was one 64 hit in 1.5 splash (2026-10-28).
        add(troop(64, "Firecracker", 3.0f, Archetype::RangedSquad, 304, SPEED_FAST, 6.0f, 64, 30, '$')
            .withTargetsAir()
            .withBurstPellets(FIRECRACKER_SPARKS)
            .withRecoil(1.0f).withSightRange(8.0f)); // kicks back 1 tile after every attack
        add(troop(65, "Skeleton Dragons", 4.0f, Archetype::RangedSquad, 560, SPEED_FAST, 3.5f, 151, 20, '%')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} }).withFlying().withTargetsAir()
            .withSplash(1.5f));
        // At <= 50% hp it kills itself and the kamikaze form spawns in its
        // place (goblinDemolisherKamikazeStats).
        // client goblin_demolisher.toml Speed 60, projectile Damage 73 -> 187 (was slow, 186) (2026-10-28)
        add(troop(66, "Goblin Demolisher", 4.0f, Archetype::RangedSquad, 1300, SPEED_MEDIUM, 5.0f, 187, 11, '&')
            .withSplash(1.5f)
            .withHpTransformIntoDeath(0.5f, std::make_shared<SpawnOnDeath>(goblinDemolisherKamikazeStats())));
        // client dartbarrell.toml projectile Damage -> 179 (was 171) (2026-10-28)
        add(troop(67, "Flying Machine", 4.0f, Archetype::RangedSquad, 614, SPEED_FAST, 6.0f, 179, 11, '+')
            .withFlying().withTargetsAir().withSightRange(6.0f));
        // The curse spawns a Cursed Hog for Mother Witch (CursedHogOnHit).
        add(troop(68, "Mother Witch", 4.0f, Archetype::RangedSquad, 529, SPEED_MEDIUM, 5.5f, 133, 10, ',')
            .withTargetsAir()
            .withOnHit(std::make_shared<CursedHogOnHit>(1.3f, 60, cursedHogStats())));
        // Cannon Cart: a single 1809 hp pool; at 50% it grounds itself for 15
        // s, then self-destructs, keeping its damage, range and hit speed
        // (CombatEntity::transformBecomesStationary).
        // client cannon_cart.toml projectile Damage -> 202 (was 212) (2026-10-28)
        add(troop(69, "Cannon Cart", 5.0f, Archetype::RangedSquad, 1809, SPEED_MEDIUM, 5.5f, 202, 9, '?')
            .withHpTransform(0.5f, 150, true)
            // The sourced sight is 6.0 mobile and 5.5 once grounded; one entity
            // models both, and the mobile value is what matters for chasing.
            .withSightRange(6.0f));
        // Furnace: a walking troop since the August 2025 rework, Medium since
        // 2025-10-06, one Fire Spirit every 7 s.
        // client furnace_rework.toml SightRange 6.0 (was 5.5) (2026-10-28)
        add(troop(70, "Furnace", 4.0f, Archetype::RangedSquad, 727, SPEED_MEDIUM, 5.5f, 179, 17, '/').withSightRange(6.0f)
            .withTargetsAir()
            .withPeriodicEffect(70, std::make_shared<PeriodicSpawnEffect>(furnaceFireSpiritStats())));
        add(troop(71, "Witch", 5.0f, Archetype::RangedSquad, 839, SPEED_MEDIUM, 5.5f, 135, 11, ':')
            .withTargetsAir().withSplash(1.5f)
            .withPeriodicEffect(70, std::make_shared<PeriodicSpawnEffect>(witchSkeletonStats())));

        // "Spirit" troops detonate on their first hit and vanish
        // (CombatEntity::dieAfterFirstHit).
        //
        // Ice Spirit stuns (0.0f), not slows: a stun is FreezeOnHit(ticks,
        // 0.0f) (Electro Spirit, Zappies, Freeze), and 0.5-0.7 is the slow band
        // (Ice Wizard, Ice Golem's explosion). Pinned in
        // tests/core/test_default_deck_qa.cpp.
        //
        // Every Spirit is 215 hp since the official 4 Aug 2026 notes (230 ->
        // 215); Fire Spirit hits for 215 since 16 Sep 2026 (207 -> 215), in a
        // 2.3-tile area (UPSTREAM_REQUESTS.md item 32b).
        // client IceSpiritsProjectile Radius 1.5, AoE (was single-target) (2026-10-28)
        add(troop(72, "Ice Spirit", 1.0f, Archetype::RangedSquad, 215, SPEED_VERY_FAST, 2.5f, 110, 10, ';')
            .withTargetsAir()
            .withOnHit(std::make_shared<FreezeOnHit>(10, 0.0f)).withSplash(1.5f)
            .withDieAfterFirstHit());
        add(troop(73, "Fire Spirit", 1.0f, Archetype::RangedSquad, 215, SPEED_VERY_FAST, 2.5f, 215, 10, '<')
            .withTargetsAir()
            .withSplash(2.3f)
            .withDieAfterFirstHit());
        // The real heal is 4 pulses of 100.25 (401), applied here as one heal
        // on its single hit.
        // client healspirit.toml Speed 120 (Very Fast), projectile Radius 1.5 (was 0.85 off-tier, single-target) (2026-10-28)
        add(troop(74, "Heal Spirit", 1.0f, Archetype::RangedSquad, 215, SPEED_VERY_FAST, 2.5f, 110, 10, '=')
            .withTargetsAir()
            .withHealAura(2.5f, 401).withSplash(1.5f)
            .withDieAfterFirstHit());
        add(troop(75, "Electro Spirit", 1.0f, Archetype::RangedSquad, 215, SPEED_VERY_FAST, 2.5f, 99, 10, '>')
            .withTargetsAir()
            .withSplitTargets(2)
            .withOnHit(std::make_shared<FreezeOnHit>(8, 0.0f))
            .withDieAfterFirstHit());

        // === Swarms (expansion) ===
        // Guards and Royal Recruits shields: 256 and 240.
        // client skeleton_warrior.toml Range 1.6 (was 1.0) (2026-10-28)
        add(troop(76, "Guards", 3.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 1.6f, 117, 10, '?')
            .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f} })
            .withShield(256));
        // client recruit.toml Range 1.6 (was 1.0) (2026-10-28)
        add(troop(77, "Royal Recruits", 7.0f, Archetype::MeleeSquad, 547, SPEED_MEDIUM, 1.6f, 133, 13, '@')
            .withOffsets({ {-2.5f, 0.0f}, {-1.5f, 0.0f}, {-0.5f, 0.0f}, {0.5f, 0.0f}, {1.5f, 0.0f}, {2.5f, 0.0f} })
            .withShield(240));
        // client bat.toml Range 1.2 (was 1.0) (2026-10-28)
        add(troop(78, "Bats", 2.0f, Archetype::MeleeSquad, 81, SPEED_VERY_FAST, 1.2f, 81, 12, 't')
            .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f}, {0.3f, 0.5f}, {-0.3f, 0.5f} })
            .withFlying().withTargetsAir());

        // client minizapmachine.toml HitSpeed 2.3 s (was 2.1) (2026-10-28)
        add(troop(79, "Zappies", 4.0f, Archetype::RangedSquad, 529, SPEED_MEDIUM, 4.5f, 117, 23, '[')
            .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f} })
            .withTargetsAir()
            .withOnHit(std::make_shared<FreezeOnHit>(3, 0.0f)).withSightRange(5.0f));
        // Three Elite Musketeers (client three_musketeer_rework.toml, 2026-10-28): 906 hp each (354), a 204 shot
        // (Damage 80) at range 6 or, against a ground target within 1.6, a 314 bayonet (BaseDamage 123), one or
        // the other per attack; air always gets the shot. 1.3 s. Was 722 hp, 218, 1.0 s.
        add(troop(80, "Three Musketeers", 9.0f, Archetype::RangedSquad, 906, SPEED_MEDIUM, 6.0f, 204, 13, ']')
            .withOffsets({ {-2.0f, 0.0f}, {0.0f, 0.0f}, {2.0f, 0.0f} })
            .withTargetsAir().withSightRange(6.0f)
            .withGroundMeleeAttack(1.6f, 314));

        // === Building targeters (expansion) ===
        // Battle Ram: breaks on its first hit on a building and releases its
        // 2 Barbarians, as the real card does (withDieAfterFirstHit; the hit
        // is doubled if it charged). It used to keep swinging until killed.
        // UPSTREAM_REQUESTS.md item 34. Level 11: 967 hp, 286 damage (572 on
        // a charge; the game's is 573 since a March 2025 rounding change),
        // 0.5-tile reach, a 2x-speed charge, 6.5 sight (official September
        // 2026 notes: 5.5 -> 6.5). It had 691 / 192 / 1.0 reach / no charge
        // speed / 5.5 sight. Item 35. The Barbarians keep their 1 s deploy:
        // the game files' death_spawn_deploy_time is 1000 ms.
        add(troop(81, "Battle Ram", 4.0f, Archetype::MeleeBuildingTargeter, 967, SPEED_MEDIUM, 0.5f, 286, 14, '^')
            .withDeathEffect(std::make_shared<SpawnOnDeath>(battleRamBarbarianStats()))
            .withCharge(3.0f, 2.0f).withChargeSpeed(2.0f).withDieAfterFirstHit().withSightRange(6.5f));
        // Royal Hogs jump the river.
        // client royalhog.toml Range 0.75 (was 1.0) (2026-10-28)
        // No charge (2026-10-29): royalhog.toml has no ChargeRange / DamageSpecial (only Battle Ram, Ram Rider,
        // Prince, Dark Prince and the Royal Recruits Evolution do); the engine gave them 2x after 3 tiles.
        add(troop(82, "Royal Hogs", 5.0f, Archetype::MeleeBuildingTargeter, 837, SPEED_VERY_FAST, 0.75f, 74, 12, '_')
            .withOffsets({ {-1.0f, -0.3f}, {-0.3f, 0.3f}, {0.3f, -0.3f}, {1.0f, 0.3f} })
            .withSightRange(9.5f)
            .withIgnoresRiver());
        // client wallbreaker.toml projectile Damage 118 -> 302, Range 0.5 (was 350, 1.0) (2026-10-28)
        add(troop(83, "Wall Breakers", 2.0f, Archetype::MeleeBuildingTargeter, 330, SPEED_VERY_FAST, 0.5f, 302, 12, '{')
            .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} })
            .withSplash(1.5f)
            .withDieAfterFirstHit().withSightRange(7.0f));
        // client electrogiant.toml (2026-10-28): Damage 72 -> 184, Range 1.2, SightRange 7.5, no AreaDamageRadius
        // (its hits are single-target; the zap is the periodic stun below). Was 163, 1.0, 5.5, splash 1.5.
        add(troop(84, "Electro Giant", 7.0f, Archetype::MeleeBuildingTargeter, 3952, SPEED_SLOW, 1.2f, 184, 18, '|')
            .withSightRange(7.5f)
            .withPeriodicEffect(50, std::make_shared<AreaStunEffect>(2.5f, 5)));
        // From the client's suspicious_bush.toml (2026-10-28): a kamikaze building-targeter that pops on reaching a
        // building (Kamikaze, Damage 0, Range 1.6, HitSpeed 0.3 s), sight 7.5, Medium speed, invisible while walking
        // (area damage still lands), two Bush Goblins on death. Was: attacked the tower for 256 every 1.4 s, slow,
        // sight 5.5, and spells couldn't touch it (Cole).
        add(troop(85, "Suspicious Bush", 2.0f, Archetype::MeleeBuildingTargeter, 81, SPEED_MEDIUM, 1.6f, 0, 3, '}')
            .withSightRange(7.5f).withDieAfterFirstHit()
            .withInvisibility(5)
            .withDeathEffect(std::make_shared<SpawnOnDeath>(suspiciousBushGoblinStats())));
        // client giant_buffer_v2.toml Hitpoints 1100 -> 2816, Speed 60 (Medium), SightRange 7.5 (was 2662, 0.5 off-tier, 5.5) (2026-10-28)
        add(troop(86, "Rune Giant", 4.0f, Archetype::MeleeBuildingTargeter, 2816, SPEED_MEDIUM, 1.2f, 153, 15, '~')
            .withAllyBuffAura(3.0f, 3, 1.5f, 50, 2).withSightRange(7.5f));
        // Ram Rider jumps the river.
        // client ram_rider.toml (Ram) Damage 106 -> 271, Range 0.8 (was 250, 1.0) (2026-10-28)
        add(troop(87, "Ram Rider", 5.0f, Archetype::MeleeBuildingTargeter, 1766, SPEED_MEDIUM, 0.8f, 271, 17, '"')
            .withCharge(3.0f, 2.0f).withChargeSpeed(2.0f) // the ram charges at 2x (item 35)
            .withSecondaryUnit(ramRiderCrossbowStats()) // the rider's independently targeting crossbow (default 5.5 sight)
            .withSightRange(7.5f) // the ram itself
            .withIgnoresRiver());
        add(troop(88, "Goblin Giant", 6.0f, Archetype::MeleeBuildingTargeter, 3110, SPEED_MEDIUM, 1.2f, 176, 15, '`')
            .withSecondaryUnit(goblinGiantSpearGoblinsStats()).withSightRange(7.5f)); // carried Spear Goblins, independently targeting
        // Skeleton Barrel: pops on reaching its building (skeletonBarrelDeath). Was: hit the building for 81 a
        // second until shot down, then 2 Skeletons and no explosion (2026-10-28).
        add(troop(89, "Skeleton Barrel", 3.0f, Archetype::MeleeBuildingTargeter, 532, SPEED_FAST, 0.35f, 0, 3, ',')
            .withFlying().withDieAfterFirstHit()
            .withDeathEffect(skeletonBarrelDeath()).withSightRange(7.7f));
        // The split chain: Golem -> 2 Golemites (each -> 2 Blobs), with elixir
        // to the opponent at each tier (see elixirGolemiteStats,
        // elixirBlobStats).
        // client elixir_golem.toml Range 0.8 (was 1.0) (2026-10-28)
        add(troop(90, "Elixir Golem", 3.0f, Archetype::MeleeBuildingTargeter, 1569, SPEED_SLOW, 0.8f, 253, 11, '(')
            .withDeathEffect(std::make_shared<CompositeDeathEffect>(
                std::vector<std::shared_ptr<IDeathEffect>>{
                    std::make_shared<SpawnOnDeath>(
                        elixirGolemiteStats().withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} })),
                    std::make_shared<EnemyElixirGrantOnDeath>(1.0f)
                }))
            .withSightRange(7.5f));

        // === Ranged building targeter (expansion) ===
        // Lava Hound releases 6 Lava Pups; their spread is an engine choice.
        // client lavahound.toml projectile Damage 28 -> 72, HitSpeed 1.5 s (was 53, 1.3 s) (2026-10-28)
        add(troop(91, "Lava Hound", 7.0f, Archetype::RangedBuildingTargeter, 3581, SPEED_SLOW, 3.5f, 72, 15, ')')
            .withFlying()
            .withDeathEffect(std::make_shared<SpawnOnDeath>(lavaHoundPupStats())));

        // === Defensive structures (expansion) ===
        // X-Bow: the default 30 s building lifetime matches the real card;
        // withDeployDelay adds the ~3.5 s lock-on.
        // client xbow.toml (2026-10-28): xbow_projectile Damage 24 -> 61, HitSpeed 0.4 s (was 43, 0.3 s)
        add(building(92, "X-Bow", 6.0f, 1600, 'P', 11.5f, 61, 4)
            .withDeployDelay(35).withSightRange(11.5f));
        // client mortar.toml HitSpeed 4.7 s (was 5.0) (2026-10-28)
        add(building(93, "Mortar", 4.0f, 1369, 'R', 11.5f, 266, 47)
            .withMinRange(3.5f).withSightRange(11.5f) // blind spot
            .withSplash(2.0f)
            .withDeployDelay(35)); // client mortar.toml DeployTime 3500, as the X-Bow's (2026-10-28)

        // Spawner buildings do not attack in the real game: 0-damage structures
        // spawning via PeriodicSpawnEffect. Barbarian Hut: 3 every 15 s, plus
        // one Barbarian on its death.
        add(building(94, "Barbarian Hut", 6.0f, 1164, '2', 0.0f, 0, 100)
            .withPeriodicEffect(140, std::make_shared<PeriodicSpawnEffect>(barbarianHutBarbarianStats())) // client SpawnPauseTime 14 s (was 15) (2026-10-28)
            .withDeathEffect(std::make_shared<SpawnOnDeath>(barbarianHutDeathBarbarianStats())));
        // Goblin Hut spawns only while an enemy is within 6 tiles, every 2.2 s
        // (ProximityGatedPeriodicSpawnEffect), and releases one Spear Goblin on
        // death.
        // 1180 hp since the 2025-10-06 -4% (was 1228).
        add(building(95, "Goblin Hut", 4.0f, 1180, '3', 0.0f, 0, 100)
            .withPeriodicEffect(22, std::make_shared<ProximityGatedPeriodicSpawnEffect>(goblinHutSpearGoblinStats(), 6.0f))
            .withDeathEffect(std::make_shared<SpawnOnDeath>(goblinHutSpearGoblinStats())));
        // Tombstone: 2 Skeletons per pulse, 4 on its death.
        add(building(96, "Tombstone", 3.0f, 529, '5', 0.0f, 0, 100)
            .withPeriodicEffect(35, std::make_shared<PeriodicSpawnEffect>(skeletonBarrelSkeletonStats()))
            .withDeathEffect(std::make_shared<SpawnOnDeath>(witchSkeletonStats())));
        // Goblin Cage: its whole function is releasing a Goblin Brawler on
        // death.
        // client goblincage.toml LifeTime 20 s (was 30) (2026-10-28)
        add(building(97, "Goblin Cage", 4.0f, 780, '6', 0.0f, 0, 100).withLifetime(200)
            .withDeathEffect(std::make_shared<SpawnOnDeath>(goblinCageBrawlerStats())));
        // Goblin Drill: a one-time 360-degree emergence burst (the spawn
        // effect; knockback not modelled), periodic Goblins, and 2 Goblins on
        // death.
        // client goblindrilldig.toml BUILDING.GoblinDrill LifeTime 10 s (was 30) (2026-10-28)
        add(building(98, "Goblin Drill", 4.0f, 1313, '7', 0.0f, 0, 100).withLifetime(100)
            .withPeriodicEffect(30, std::make_shared<PeriodicSpawnEffect>(goblinDrillGoblinStats()))
            .withDeployAnywhere()
            .withSpawnEffect(2.0f, 84)
            .withDeathEffect(std::make_shared<SpawnOnDeath>(goblinDrillDeathGoblinStats())));
        // Elixir Collector: no attack; credits elixir (ElixirGrantEffect). client elixircollector.toml: 1 every
        // 15 s (ManaGenerateTimeMs), LifeTime 110 s, 1 more when it dies (ManaOnDeath) -- 8 in a full life (was 1
        // every 8 s for 30 s, nothing on death: 3) (2026-10-28).
        add(building(99, "Elixir Collector", 6.0f, 1070, '9', 0.0f, 0, 100)
            .withPeriodicEffect(150, std::make_shared<ElixirGrantEffect>(1.0f))
            .withLifetime(1100)
            .withDeathEffect(std::make_shared<OwnElixirGrantOnDeath>(1.0f)));

        // === Spells (expansion) ===
        // client SnowballSpell: IceWizardSlowDown (-30%) for BuffTime 3.0 s, Pushback 1800 (was 1.5 s at -50%, 1.0) (2026-10-28)
        add(spell(100, "Giant Snowball", 2.0f, 2.5f, 179, 8, '!')
            .withSpellOnHit(std::make_shared<FreezeOnHit>(30, 0.7f))
            .withKnockback(1.8f));
        // Barbarian Barrel: rolls 4.5 tiles at width 2.6 (published), and its
        // Barbarian spawns where the barrel stops. No knockback: the real
        // barrel does not throw what it rolls over. Cast on its own side only,
        // unlike The Log's river allowance (UPSTREAM_REQUESTS.md item 32n).
        // client BarbLogProjectileRolling Damage 84 -> 215 (was DeckShop's 232) (2026-10-28)
        add(spell(101, "Barbarian Barrel", 2.0f, 2.5f, 215, 8, '#')
            .withGroundOnly()
            .withOwnSideOnly()
            .withRollingSweep(4.5f, 2.6f, 0.5f, 0.0f)
            .withSpellSpawn(std::make_shared<PeriodicSpawnEffect>(barbarianBarrelBarbarianStats())));
        // Goblin Curse: only the damage over time is modelled, not the
        // damage-taken amplification or the Goblin on cursed death.
        // client goblin_curse.toml GoblinCurseDamage: 14/s -> 36 a second for 6 s, towers 4 -> 10 a hit, and
        // the -15% movement slow, as Poison's (was 43, full to towers, no slow) (2026-10-28)
        add(spell(102, "Goblin Curse", 2.0f, 3.0f, 36, 8, '$').withRepeats(6, 10)
            .withSpellOnHit(std::make_shared<MoveSlowOnHit>(10, 0.85f)));
        // Earthquake: ground-only damage over time with the 50% movement slow. client earthquake.toml
        // DamagePerSecond 32 -> 81 a second for 3 s to troops; BuildingDamagePercent 350 -> 286 to other buildings;
        // CrownTowerDamagePercent -40 -> 49 to Crown Towers (147 in all), with no other reduction on top. Was 82 to
        // everything (2026-10-28).
        add(spell(103, "Earthquake", 3.0f, 3.5f, 81, 8, '%').withRepeats(3, 10).withGroundOnly()
            .withCrownTowerDamage(49).withBuildingDamage(286)
            .withSpellOnHit(std::make_shared<FreezeOnHit>(30, 0.5f)));
        // Void: three damage tiers (340 per hit on 1 target, 160 on 2-4, 76 on
        // 5+), once per wave.
        add(spell(104, "Void", 3.0f, 2.5f, 0, 8, '&').withRepeats(3, 10)
            .withSpellTieredDamage(340, 160, 76));
        // Vines: only the 3 highest-HP enemies in radius. Pulling flyers to the
        // ground is not modelled.
        add(spell(105, "Vines", 3.0f, 2.5f, 135, 8, '+').withRepeats(3, 7)
            .withSpellTopHpTargets(3));
        // client tornado.toml: 60/s (-> 154) every 0.55 s for 1.05 s = two hits of 84, kept as one 168 hit so
        // the unsourced pull below is applied once; towers -70% (was 154, full to towers) (2026-10-28)
        add(spell(106, "Tornado", 3.0f, 5.5f, 168, 8, '_')
            .withKnockback(-1.5f)); // negative: pulls toward the centre
        // Freeze: client freeze.toml BaseDamage 58 -> 148, TowerDamage 15 -> 38, a 3.5 s full stun (was no
        // damage, 4 s) (2026-10-28)
        add(spell(107, "Freeze", 4.0f, 3.0f, 148, 8, '~')
            .withSpellOnHit(std::make_shared<FreezeOnHit>(35, 0.0f)));
        // Rage: buffs allied damage in radius. Duration and multiplier are not
        // sourced.
        // client ragebottle.toml RageDamage: BaseDamage 70 -> 179, TowerDamage 18 -> 46 on landing (was none) (2026-10-28)
        add(spell(108, "Rage", 2.0f, 3.0f, 179, 8, '(')
            .withSpellBuff(1.35f, 45));
        // Goblin Barrel: drops 3 Goblins; the barrel itself deals no damage.
        add(spell(109, "Goblin Barrel", 3.0f, 0.5f, 0, 8, '[')
            .withSpellSpawn(std::make_shared<PeriodicSpawnEffect>(goblinBarrelGoblinStats())));
        // Graveyard: 12 Skeletons (official June 2026), the first 2.2 s after
        // the cast (delay 21: an AreaSpell applies on the update after its
        // delay reaches 0), then one every 0.5 s, cycling seven fixed points --
        // the cast point and six on a 3.3-tile ring (AreaSpell::
        // spawnRingRadius) -- rather than all rising on the cast point, where
        // one small splash caught them all. Arrivals outrun a Princess
        // Tower's 0.8 s fire rate 1.6:1. How many connect now depends on the
        // placement, as in the real game: 81 to 810 tower damage over 15 s
        // against a defended Princess Tower, most on its outer side.
        // UPSTREAM_REQUESTS.md items 32i and 32j.
        add(spell(110, "Graveyard", 5.0f, 4.0f, 0, 21, ']')
            .withRepeats(12, 5)
            .withSpellSpawnRing(3.3f)
            .withSpellSpawn(std::make_shared<PeriodicSpawnEffect>(graveyardSkeletonStats())));
        // Royal Delivery: a shielded Royal Recruit plus its own landing damage.
        // client RoyalDeliveryProjectile Damage 150 -> 384, Radius 3.0 (was 438, 2.0) (2026-10-28)
        add(spell(111, "Royal Delivery", 3.0f, 3.0f, 384, 8, '^')
            .withSpellSpawn(std::make_shared<PeriodicSpawnEffect>(royalDeliveryRecruitStats())));

        // === Compound troops ===
        // Goblin Gang and Rascals: both halves spawn via the primary unit plus
        // secondaryUnit.
        // client goblin.toml Damage 49 -> 125 (was 120) (2026-10-28)
        add(troop(112, "Goblin Gang", 3.0f, Archetype::MeleeSquad, 202, SPEED_VERY_FAST, 0.5f, 125, 11, 'g')
            .withOffsets({ {-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.5f} })
            .withSecondaryUnit(
                troop(-27, "Spear Goblins", 0.0f, Archetype::RangedSquad, 133, SPEED_VERY_FAST, 5.0f, 81, 16, 'S')
                    .withOffsets({ {-0.5f, 0.5f}, {0.5f, 0.5f}, {0.0f, -0.5f} })));
        // Rascals: only the Boy's stats are sourced; the two Girls reuse Spear
        // Goblins' ranged stats.
        // client rascalboy.toml Range 0.8 (was 1.2) (2026-10-28)
        add(troop(113, "Rascals", 5.0f, Archetype::MeleeSquad, 1832, SPEED_MEDIUM, 0.8f, 217, 15, 'X')
            .withSecondaryUnit(
                troop(-28, "Rascals", 0.0f, Archetype::RangedSquad, 133, 0.5f, 5.0f, 81, 17, 'r')
                    .withOffsets({ {-0.5f, 0.0f}, {0.5f, 0.0f} })));

        // Clone: duplicates every allied troop in radius; each clone has a
        // fresh id and 1 hp.
        add(spell(114, "Clone", 3.0f, 3.0f, 0, 8, ')')
            .withSpellClone());

        // === Champions ===
        // Mighty Miner: MeleeSquad like the Miner, but not deploy-anywhere.
        // Damage ramps 40 -> 204 -> 409 on one target, 2 s per stage
        // (Liquipedia, 2025-01-08 patch), so mid 20 and full 40 ticks; `damage`
        // is the max. "Explosive Escape" (1 elixir, ~13 s cooldown): a lane
        // swap leaving a bomb that detonates after ~1 s for 332 with 1.8-tile
        // knockback. The bomb radius (2.5) is not sourced. See
        // MightyMinerEscapeEffect.
        add(troop(115, "Mighty Miner", 4.0f, Archetype::MeleeSquad, 2250, SPEED_MEDIUM, 1.6f, 409, 4, '\'')
            .withDamageRamp(20, 40, 44.0f / 409.0f, 205.0f / 409.0f) // client 17 / 80 / 160 -> 44 / 205 / 410 (was 40 / 204) (2026-10-28)
            .withChampionAbility(1.0f, 130, std::make_shared<MightyMinerEscapeEffect>(2.5f, 332, 10, 1.8f)));

        // Golden Knight's "Dashing Dash": up to 10 dashes to the nearest enemy
        // within 5.5 tiles, stopping at a Crown Tower (GoldenKnightDashEffect).
        // client golden_knight.toml Damage -> 169 (was 161) (2026-10-28)
        add(troop(116, "Golden Knight", 4.0f, Archetype::MeleeSquad, 1799, SPEED_MEDIUM, 1.2f, 169, 9, 'k')
            .withChampionAbility(1.0f, 120, std::make_shared<GoldenKnightDashEffect>(335, 5.5f, 10)));

        // Skeleton King: collects up to 10 souls from any death within
        // soulCollectionRadius (not sourced); "Soul Summoning" spawns 6 + 1 per
        // soul, consuming them.
        // client skeleton_king.toml Damage 80 -> 205 (was 180) (2026-10-28)
        add(troop(117, "Skeleton King", 4.0f, Archetype::MeleeSquad, 2298, SPEED_MEDIUM, 1.2f, 205, 16, 'K')
            .withSplash(1.3f)
            .withSoulCollection(5.0f, 10)
            .withChampionAbility(2.0f, 200,
                std::make_shared<SkeletonKingSoulSummonEffect>(skeletonKingSummonedSkeletonStats(), 6, 3.5f)));

        // Archer Queen's "Cloaking Cape": untargetable and ~2.8x attack speed
        // for 3.5 s.
        // client archer_queen.toml projectile Damage -> 233 (was 225) (2026-10-28)
        add(troop(118, "Archer Queen", 5.0f, Archetype::RangedSquad, 1000, SPEED_MEDIUM, 5.0f, 233, 12, 'Q')
            .withTargetsAir()
            .withChampionAbility(1.0f, 170, std::make_shared<ArcherQueenCloakEffect>(35, 1.0f / 2.8f)));

        // Monk: a 3-hit combo, 140 / 140 / 422 (client monk.toml Damage 55, VariableDamage2 55, VariableDamage3
        // 165), every 0.8 s, the count carrying across targets; the third hit knocks back 1.8 tiles
        // (MeleePushback3). "Pensive Protection" (65% damage reduction for 4 s): see MonkDeflectEffect.
        // client spells_characters.csv ManaCost 5 (was 4) (2026-10-28)
        add(troop(119, "Monk", 5.0f, Archetype::MeleeSquad, 2214, SPEED_MEDIUM, 1.2f, 140, 8, 'M')
            .withAttackSequence({ 140, 140, 422 }, 1.8f)
            .withChampionAbility(1.0f, 170, std::make_shared<MonkDeflectEffect>(0.35f, 40)));

        // Little Prince: hit speed ramps 1.2 s -> 0.8 s -> 0.4 s on one target
        // (3 attacks per stage, as tick thresholds). "Royal Rescue" summons the
        // Guardienne.
        // client little_prince.toml Speed 60 (was slow) (2026-10-28)
        add(troop(120, "Little Prince", 3.0f, Archetype::RangedSquad, 698, SPEED_MEDIUM, 5.5f, 104, 12, 'p')
            .withTargetsAir()
            .withHitSpeedRamp(36, 60, 8.0f / 12.0f, 4.0f / 12.0f)
            .withChampionAbility(3.0f, 300, std::make_shared<SpawnOnAbility>(guardienneStats())));

        // Goblinstein: the Monster (front, building-only tank, carries the
        // ability) plus the Doctor (secondary, 3 tiles behind at a fixed
        // offset, a brief stun on hit). "Lightning Link": see
        // GoblinsteinLightningLinkEffect.
        // client goblinstein.toml Hitpoints 875 -> 2240, Speed 60, SightRange 7.5 (was 2385, slow, 5.5) (2026-10-28)
        add(troop(121, "Goblinstein", 5.0f, Archetype::MeleeBuildingTargeter, 2240, SPEED_MEDIUM, 1.2f, 128, 15, 'G')
            .withSightRange(7.5f)
            .withChampionAbility(2.0f, 170, std::make_shared<GoblinsteinLightningLinkEffect>(2.0f, 107, 5, 40))
            .withSecondaryUnit(
                troop(-40, "Goblinstein", 0.0f, Archetype::RangedSquad, 721, 0.5f, 5.5f, 92, 18, 'D')
                    .withOffsets({ {0.0f, 3.0f} })
                    .withTargetsAir()
                    .withOnHit(std::make_shared<FreezeOnHit>(5, 0.0f))));

        // Boss Bandit: the Bandit's charge; dash invulnerability is inferred
        // from the Bandit, not sourced for this card. Always ignores the river,
        // as the Bandit does. "Getaway Grenade": brief invisibility and a
        // 6-tile teleport back toward her own side, limited to 2 uses per
        // deploy.
        // client boss_bandit.toml Speed 90, Damage 96 -> 246, SightRange 6.0 (was 63-tier, 245, 5.5) (2026-10-28)
        add(troop(122, "Boss Bandit", 6.0f, Archetype::MeleeSquad, 2624, SPEED_FAST, 0.8f, 246, 11, 'x')
            .withSightRange(6.0f)
            .withCharge(3.0f, 2.0f)
            .withChargeInvulnerability()
            .withIgnoresRiver()
            .withChampionAbility(1.0f, 30, std::make_shared<BossBanditGetawayGrenadeEffect>(10, 6.0f), 2));

        // === Evolutions ===
        // A deck holds the Evolution's id instead of the base card's. Unless
        // noted, each unlocks after 2 cycles and then evolves one play in
        // three, repeating (addEvolution).
        //
        // Wall Breakers ("Powder Barrels"): a defeated Wall Breaker's barrel
        // explodes in a 1.5-tile radius (the reduced Crown Tower damage is not
        // modelled), then a Runner rolls on to the nearest building
        // (runnerStats). One Runner per Wall Breaker. Sources disagree on a
        // walking-damage buff, so the stats stay unchanged.
        addEvolution(123,
            troop(83, "Wall Breakers", 2.0f, Archetype::MeleeBuildingTargeter, 330, SPEED_VERY_FAST, 0.5f, 302, 12, '{')
                .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} })
                .withSplash(1.5f)
                .withDieAfterFirstHit().withSightRange(7.0f),
            troop(83, "Wall Breakers", 2.0f, Archetype::MeleeBuildingTargeter, 330, SPEED_VERY_FAST, 0.5f, 302, 12, '{')
                .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} })
                .withSplash(1.5f)
                .withDieAfterFirstHit().withSightRange(7.0f)
                .withDeathEffect(std::make_shared<CompositeDeathEffect>(
                    std::vector<std::shared_ptr<IDeathEffect>>{
                        std::make_shared<AreaDamageOnDeath>(1.5f, 150),
                        std::make_shared<SpawnOnDeath>(runnerStats())
                    })),
            2, 1);

        // Zap ("Triple Shock"): three zaps instead of one. The growing radius
        // between zaps is not modelled; the 3-tick gap is not sourced.
        addEvolution(124,
            spell(29, "Zap", 2.0f, 2.5f, 192, 3, 'Z'),
            spell(29, "Zap", 2.0f, 2.5f, 192, 3, 'Z').withRepeats(3, 3),
            2, 1);

        // Skeletons ("Never-ending Horde"): see evolvedSkeletonChildStats.
        addEvolution(125,
            troop(24, "Skeletons", 1.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 'k')
                .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f} }),
            troop(24, "Skeletons", 1.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 'k')
                .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f} })
                .withOnHitSpawn(std::make_shared<CappedSpawnOnHitEffect>(evolvedSkeletonChildStats(), 8)),
            2, 1);

        // Bats: 122 hp (+50%); every attack heals two pulses of 38, 0.5 s
        // apart, overhealing up to 244 (wiki, level 11; UPSTREAM_REQUESTS.md
        // item 32g).
        addEvolution(126,
            troop(78, "Bats", 2.0f, Archetype::MeleeSquad, 81, SPEED_VERY_FAST, 1.2f, 81, 12, 't')
                .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f}, {0.3f, 0.5f}, {-0.3f, 0.5f} })
                .withFlying().withTargetsAir(),
            troop(78, "Bats", 2.0f, Archetype::MeleeSquad, 122, SPEED_VERY_FAST, 1.2f, 81, 12, 't')
                .withOffsets({ {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f}, {0.3f, 0.5f}, {-0.3f, 0.5f} })
                .withFlying().withTargetsAir()
                .withHealOnHit(38, 244, 5),
            2, 1);

        // Bomber: +25% hp (380). "Bouncy Bomb" bounces twice at full damage,
        // approximated as a 3-target full-damage split.
        addEvolution(127,
            troop(9, "Bomber", 2.0f, Archetype::RangedSquad, 304, SPEED_MEDIUM, 4.5f, 212, 18, 'b').withSplash(1.5f),
            troop(9, "Bomber", 2.0f, Archetype::RangedSquad, 380, SPEED_MEDIUM, 4.5f, 212, 18, 'b').withSplash(1.5f)
                .withSplitTargets(3).withSplitTargetsFullDamage(),
            2, 1);

        // Archers: +1 tile range (6.0). "Power Shot": +50% damage at 4-6 tiles.
        addEvolution(128,
            troop(1, "Archers", 3.0f, Archetype::RangedSquad, 304, SPEED_MEDIUM, 5.0f, 112, 9, 'A')
                .withOffsets({ {0.0f, 0.0f}, {1.0f, 0.0f} }).withTargetsAir(),
            troop(1, "Archers", 3.0f, Archetype::RangedSquad, 304, SPEED_MEDIUM, 6.0f, 112, 9, 'A')
                .withOffsets({ {0.0f, 0.0f}, {1.0f, 0.0f} }).withTargetsAir()
                .withRangeBandBonus(4.0f, 6.0f, 1.5f),
            2, 1);

        // Cannon ("Deploy Barrage"): a one-time burst on deploy (2.5 radius).
        // Its damage is not sourced (the Cannon's hit reused) and its knockback
        // is not modelled.
        addEvolution(129,
            building(25, "Cannon", 3.0f, 824, 'C', 5.5f, 202, 10),
            building(25, "Cannon", 3.0f, 824, 'C', 5.5f, 202, 10)
                .withSpawnEffect(2.5f, 202),
            2, 1);

        // Firecracker: the real center and shrapnel sparks (low damage every
        // 0.25 s for 3 s) and the 15% slow are approximated as a
        // damage-over-time mark on whatever the shot hits. The per-tick damage
        // is not sourced; 3 ticks approximates 0.25 s.
        addEvolution(130,
            troop(64, "Firecracker", 3.0f, Archetype::RangedSquad, 304, SPEED_FAST, 6.0f, 64, 30, '$')
                .withTargetsAir().withSplash(1.5f).withRecoil(1.0f).withSightRange(8.0f),
            troop(64, "Firecracker", 3.0f, Archetype::RangedSquad, 304, SPEED_FAST, 6.0f, 64, 30, '$')
                .withTargetsAir().withSplash(1.5f).withRecoil(1.0f).withSightRange(8.0f)
                .withOnHit(std::make_shared<PoisonOnHit>(16, 30, 3)),
            2, 1);

        // Dart Goblin: the real escalating poison (51 / 115 / 307 per
        // consecutive dart, 1.25 s delay) is a flat 51 per second for 4 s. The
        // mark lives on the victim, so it outlasts the Goblin as in the real
        // card. withTargetsAir on both stat blocks: addEvolution inherits
        // nothing from the base registration.
        addEvolution(131,
            troop(20, "Dart Goblin", 3.0f, Archetype::RangedSquad, 261, SPEED_VERY_FAST, 6.5f, 143, 8, 'd')
                .withTargetsAir().withSightRange(7.0f),
            troop(20, "Dart Goblin", 3.0f, Archetype::RangedSquad, 261, SPEED_VERY_FAST, 6.5f, 143, 8, 'd')
                .withTargetsAir()
                .withOnHit(std::make_shared<PoisonOnHit>(51, 40, 10)).withSightRange(7.0f),
            2, 1);

        // Goblin Barrel: the real second barrel on the mirrored lane is
        // approximated as 6 Goblins at one landing point.
        addEvolution(132,
            spell(109, "Goblin Barrel", 3.0f, 0.5f, 0, 8, '[')
                .withSpellSpawn(std::make_shared<PeriodicSpawnEffect>(goblinBarrelGoblinStats())),
            spell(109, "Goblin Barrel", 3.0f, 0.5f, 0, 8, '[')
                .withSpellSpawn(std::make_shared<PeriodicSpawnEffect>(goblinBarrelGoblinStats()
                    .withOffsets({ {-0.4f,0.0f},{0.4f,0.0f},{0.0f,0.4f},{-0.4f,0.4f},{0.4f,0.4f},{0.0f,-0.4f} }))),
            2, 1);

        // Skeleton Army: 16 skeletons (skeletonArmyEvolvedOffsets) and shadows
        // on death. The General's own shield is not modelled (a squad shares
        // one CardStats).
        addEvolution(133,
            troop(12, "Skeleton Army", 3.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 's')
                .withOffsets(skeletonArmyOffsets()),
            troop(12, "Skeleton Army", 3.0f, Archetype::MeleeSquad, 81, SPEED_FAST, 0.5f, 81, 11, 's')
                .withOffsets(skeletonArmyEvolvedOffsets())
                .withDeathEffect(std::make_shared<SpawnOnDeath>(skeletonArmyShadowStats())),
            2, 1);

        // Skeleton Barrel: +25% hp (665). The real card drops 7 skeletons at
        // 75% hp and again on death; only the death drop is modelled, with 7.
        addEvolution(134,
            troop(89, "Skeleton Barrel", 3.0f, Archetype::MeleeBuildingTargeter, 532, SPEED_FAST, 0.35f, 0, 3, ',')
                .withFlying().withDieAfterFirstHit()
                .withDeathEffect(skeletonBarrelDeath()).withSightRange(7.7f),
            troop(89, "Skeleton Barrel", 3.0f, Archetype::MeleeBuildingTargeter, 665, SPEED_FAST, 0.35f, 0, 3, ',')
                .withFlying().withDieAfterFirstHit()
                .withDeathEffect(skeletonBarrelDeath()).withSightRange(7.7f),
            2, 1);

        // Knight: the real shield (60% less damage while approaching) is a flat
        // permanent 35% reduction (withPassiveDamageReduction).
        addEvolution(135,
            troop(0, "Knight", 3.0f, Archetype::MeleeSquad, 1766, SPEED_MEDIUM, 1.2f, 202, 12, 'K'),
            troop(0, "Knight", 3.0f, Archetype::MeleeSquad, 1766, SPEED_MEDIUM, 1.2f, 202, 12, 'K')
                .withPassiveDamageReduction(0.65f),
            2, 1);

        // Royal Ghost: a longer reveal window (1.8 s vs 0.5 s), and every
        // landed hit spawns 2 Souldiers. The real "only while invisible"
        // condition is not checked; she is cloaked for nearly all her attacks
        // anyway.
        addEvolution(136,
            troop(47, "Royal Ghost", 3.0f, Archetype::MeleeSquad, 1152, SPEED_FAST, 1.2f, 261, 18, 'Q').withSplash(1.0f)
                .withInvisibility(5).withIgnoresRiver(),
            troop(47, "Royal Ghost", 3.0f, Archetype::MeleeSquad, 1152, SPEED_FAST, 1.2f, 261, 18, 'Q').withSplash(1.0f)
                .withInvisibility(18)
                .withOnHitSpawn(std::make_shared<PeriodicSpawnEffect>(royalGhostSouldierStats()
                    .withOffsets({ {-0.4f, 0.0f}, {0.4f, 0.0f} })))
                .withIgnoresRiver(),
            2, 1);

        // Baby Dragon: the real aura (30% enemy slow, 30% ally speed-up) is
        // approximated as an ally damage buff per landed hit; there is no
        // movement-speed buff here.
        addEvolution(137,
            troop(44, "Baby Dragon", 4.0f, Archetype::RangedSquad, 1152, SPEED_FAST, 3.5f, 168, 15, 'y')
                .withFlying().withTargetsAir(),
            troop(44, "Baby Dragon", 4.0f, Archetype::RangedSquad, 1152, SPEED_FAST, 3.5f, 168, 15, 'y')
                .withFlying().withTargetsAir()
                .withAllyBuffAura(4.0f, 1, 1.3f, 20, 1000000),
            2, 1);

        // Furnace ("Hot Spawning"): while attacking, Fire Spirits every 2.4 s
        // instead of 7, spawned to the side, left then right; otherwise the
        // base card. No damage change is sourced. The 1-tile side offset is not
        // sourced. UPSTREAM_REQUESTS.md item 32f.
        addEvolution(138,
            troop(70, "Furnace", 4.0f, Archetype::RangedSquad, 727, SPEED_MEDIUM, 5.5f, 179, 17, '/').withSightRange(6.0f)
                .withTargetsAir()
                .withPeriodicEffect(70, std::make_shared<PeriodicSpawnEffect>(furnaceFireSpiritStats())),
            troop(70, "Furnace", 4.0f, Archetype::RangedSquad, 727, SPEED_MEDIUM, 5.5f, 179, 17, '/').withSightRange(6.0f)
                .withTargetsAir()
                .withPeriodicEffect(70, std::make_shared<PeriodicSpawnEffect>(furnaceFireSpiritStats()))
                .withHotPeriodicInterval(24)
                .withAlternatingSpawnSide(1.0f),
            2, 1);

        // Goblin Cage: the real pull-and-hold is a hook plus a damage-over-time
        // mark; there is no hold mechanism.
        addEvolution(139,
            building(97, "Goblin Cage", 4.0f, 780, '6', 0.0f, 0, 100).withLifetime(200)
                .withDeathEffect(std::make_shared<SpawnOnDeath>(goblinCageBrawlerStats())),
            building(97, "Goblin Cage", 4.0f, 780, '6', 3.0f, 40, 20).withLifetime(200)
                .withHook(3.0f)
                .withOnHit(std::make_shared<PoisonOnHit>(30, 40, 10))
                .withDeathEffect(std::make_shared<SpawnOnDeath>(goblinCageBrawlerStats())),
            2, 1);

        // Musketeer ("Sniper Shot"): +80% damage on empowered shots, via
        // burst-every-3rd, which repeats rather than stopping after three. The
        // special range shape is not modelled.
        addEvolution(140,
            troop(6, "Musketeer", 4.0f, Archetype::RangedSquad, 721, SPEED_MEDIUM, 6.0f, 217, 10, 'U')
                .withTargetsAir().withSightRange(6.0f),
            troop(6, "Musketeer", 4.0f, Archetype::RangedSquad, 721, SPEED_MEDIUM, 6.0f, 217, 10, 'U')
                .withTargetsAir().withBurstAttack(3, 1.8f).withSightRange(6.0f),
            2, 1);

        // Wizard ("Fire Shield"): a shield of 25% of his hp (189). The
        // shield-break explosion is not modelled.
        addEvolution(141,
            troop(11, "Wizard", 5.0f, Archetype::RangedSquad, 755, SPEED_MEDIUM, 5.5f, 305, 14, 'W')
                .withTargetsAir().withSplash(1.5f),
            troop(11, "Wizard", 5.0f, Archetype::RangedSquad, 755, SPEED_MEDIUM, 5.5f, 305, 14, 'W')
                .withTargetsAir().withSplash(1.5f).withShield(189),
            2, 1);

        // Witch: the real heal when a spawned Skeleton dies is approximated as
        // healing on her own hits, capped at 124% hp (1040).
        addEvolution(142,
            troop(71, "Witch", 5.0f, Archetype::RangedSquad, 839, SPEED_MEDIUM, 5.5f, 135, 11, ':')
                .withTargetsAir().withSplash(1.5f)
                .withPeriodicEffect(70, std::make_shared<PeriodicSpawnEffect>(witchSkeletonStats())),
            troop(71, "Witch", 5.0f, Archetype::RangedSquad, 839, SPEED_MEDIUM, 5.5f, 135, 11, ':')
                .withTargetsAir().withSplash(1.5f)
                .withPeriodicEffect(70, std::make_shared<PeriodicSpawnEffect>(witchSkeletonStats()))
                .withHealOnHit(20, 1040),
            2, 1);

        // Royal Giant: every attack also splashes 2.5 tiles. The knockback is
        // not modelled.
        addEvolution(143,
            troop(18, "Royal Giant", 6.0f, Archetype::RangedBuildingTargeter, 3164, SPEED_SLOW, 5.0f, 307, 18, 'Y')
                .withSightRange(7.5f),
            troop(18, "Royal Giant", 6.0f, Archetype::RangedBuildingTargeter, 3164, SPEED_SLOW, 5.0f, 307, 18, 'Y')
                .withSplash(2.5f).withSightRange(7.5f),
            2, 1);

        // Ice Spirit: +0.5 splash radius, applied as an absolute 1.7 on the
        // evolved form (the base card has no splash here). Both forms carry the
        // base 10-tick stun; the real delayed second pulse is not modelled.
        addEvolution(144,
            troop(72, "Ice Spirit", 1.0f, Archetype::RangedSquad, 215, SPEED_VERY_FAST, 2.5f, 110, 10, ';')
                .withTargetsAir()
                .withOnHit(std::make_shared<FreezeOnHit>(10, 0.0f)).withSplash(1.5f)
                .withDieAfterFirstHit(),
            troop(72, "Ice Spirit", 1.0f, Archetype::RangedSquad, 215, SPEED_VERY_FAST, 2.5f, 110, 10, ';')
                .withTargetsAir()
                .withSplash(1.7f)
                .withOnHit(std::make_shared<FreezeOnHit>(10, 0.0f)).withSplash(1.5f)
                .withDieAfterFirstHit(),
            2, 1);

        // Princess: every hit slows (the real card slows every third shot); the
        // lingering slow zone on death is not modelled.
        addEvolution(145,
            troop(61, "Princess", 3.0f, Archetype::RangedSquad, 261, SPEED_MEDIUM, 9.0f, 168, 30, '9')
                .withTargetsAir().withSplash(2.5f).withSightRange(9.5f).withDeployTime(12),
            troop(61, "Princess", 3.0f, Archetype::RangedSquad, 261, SPEED_MEDIUM, 9.0f, 168, 30, '9')
                .withTargetsAir().withSplash(2.5f).withSightRange(9.5f).withDeployTime(12)
                .withOnHit(std::make_shared<FreezeOnHit>(70, 0.7f)),
            2, 1);

        // Hunter ("Netting Trap"): nets the nearest enemy for 3 s every 8 s (3
        // s net + 5 s recharge), via PeriodicFreezeNearestEffect.
        addEvolution(146,
            troop(62, "Hunter", 4.0f, Archetype::RangedSquad, 885, SPEED_MEDIUM, 4.0f, 84, 22, '!')
                .withTargetsAir().withPellets(HUNTER_PELLETS),
            troop(62, "Hunter", 4.0f, Archetype::RangedSquad, 885, SPEED_MEDIUM, 4.0f, 84, 22, '!')
                .withTargetsAir().withPellets(HUNTER_PELLETS)
                .withPeriodicEffect(80, std::make_shared<PeriodicFreezeNearestEffect>(4.0f, 30)),
            2, 1);

        // Valkyrie ("Whirlwind Axe"): identical stats; every landed hit pulls
        // enemy troops within 5.5 tiles toward her, plus low damage to everyone
        // in that radius, Crown Towers included. Pull distance and damage are
        // not sourced; the lingering zone is not modelled.
        addEvolution(147,
            troop(10, "Valkyrie", 4.0f, Archetype::MeleeSquad, 1907, SPEED_MEDIUM, 1.2f, 266, 15, 'V')
                .withSplash(2.0f),
            troop(10, "Valkyrie", 4.0f, Archetype::MeleeSquad, 1907, SPEED_MEDIUM, 1.2f, 266, 15, 'V')
                .withSplash(2.0f)
                .withOnHitPull(5.5f, 2.0f, 50),
            2, 1);

        // P.E.K.K.A. ("Butter-Heal"): the real heal on each kill, up to +66% hp
        // (6242), is a smaller heal on every hit; there is no on-kill hook.
        addEvolution(148,
            troop(13, "P.E.K.K.A.", 7.0f, Archetype::MeleeSquad, 3760, SPEED_SLOW, 1.2f, 842, 18, 'E')
                .withSightRange(5.0f),
            troop(13, "P.E.K.K.A.", 7.0f, Archetype::MeleeSquad, 3760, SPEED_SLOW, 1.2f, 842, 18, 'E')
                .withHealOnHit(40, 6242).withSightRange(5.0f),
            2, 1);

        // Minion Horde ("Dark Guard"): a member hit by a troop or spell turns
        // invisible for 3 s (DarkGuardOnDamageEffect). The real card triggers
        // on the first hit only; this refreshes on every hit.
        addEvolution(149,
            troop(42, "Minion Horde", 5.0f, Archetype::MeleeSquad, 230, SPEED_FAST, 2.5f, 107, 12, 'h')
                .withOffsets({ {-0.6f, -0.3f}, {0.0f, -0.3f}, {0.6f, -0.3f},
                               {-0.6f, 0.3f}, {0.0f, 0.3f}, {0.6f, 0.3f} })
                .withFlying().withTargetsAir(),
            troop(42, "Minion Horde", 5.0f, Archetype::MeleeSquad, 230, SPEED_FAST, 2.5f, 107, 12, 'h')
                .withOffsets({ {-0.6f, -0.3f}, {0.0f, -0.3f}, {0.6f, -0.3f},
                               {-0.6f, 0.3f}, {0.0f, 0.3f}, {0.6f, 0.3f} })
                .withFlying().withTargetsAir()
                .withOnDamageTaken(std::make_shared<DarkGuardOnDamageEffect>(30)),
            2, 1);

        // Royal Recruits: the real charge after the shield breaks (2.5 tiles
        // for 2x) is unconditional here.
        addEvolution(150,
            troop(77, "Royal Recruits", 7.0f, Archetype::MeleeSquad, 547, SPEED_MEDIUM, 1.6f, 133, 13, '@')
                .withOffsets({ {-2.5f, 0.0f}, {-1.5f, 0.0f}, {-0.5f, 0.0f}, {0.5f, 0.0f}, {1.5f, 0.0f}, {2.5f, 0.0f} })
                .withShield(240),
            troop(77, "Royal Recruits", 7.0f, Archetype::MeleeSquad, 547, SPEED_MEDIUM, 1.6f, 133, 13, '@')
                .withOffsets({ {-2.5f, 0.0f}, {-1.5f, 0.0f}, {-0.5f, 0.0f}, {0.5f, 0.0f}, {1.5f, 0.0f}, {2.5f, 0.0f} })
                .withShield(240)
                .withCharge(2.5f, 2.0f),
            2, 1);

        // Electro Dragon ("Infinite Bolts"): the real unbounded chain with
        // degrading effect is a fixed 6 full-damage targets.
        addEvolution(151,
            troop(57, "Electro Dragon", 5.0f, Archetype::MeleeSquad, 1049, SPEED_MEDIUM, 3.5f, 192, 21, '5')
                .withFlying().withTargetsAir()
                .withSplitTargets(3).withSplitTargetsFullDamage()
                .withOnHit(std::make_shared<FreezeOnHit>(5, 0.0f)),
            troop(57, "Electro Dragon", 5.0f, Archetype::MeleeSquad, 1049, SPEED_MEDIUM, 3.5f, 192, 21, '5')
                .withFlying().withTargetsAir()
                .withSplitTargets(6).withSplitTargetsFullDamage()
                .withOnHit(std::make_shared<FreezeOnHit>(5, 0.0f)),
            2, 1);

        // Mortar: attack period 5 s -> 4 s. "Green Siege": a Goblin spawns with
        // every landed shot (onHitSpawnEffect).
        addEvolution(152,
            building(93, "Mortar", 4.0f, 1369, 'R', 11.5f, 266, 47)
                .withMinRange(3.5f).withSightRange(11.5f),
            building(93, "Mortar", 4.0f, 1369, 'R', 11.5f, 266, 40)
                .withMinRange(3.5f).withSightRange(11.5f)
                .withOnHitSpawn(std::make_shared<PeriodicSpawnEffect>(goblinDrillGoblinStats())),
            2, 1);

        // Goblin Drill: the real resurfacing at 66% / 33% hp (leaving a Goblin
        // each time) is approximated as more Goblins on death.
        addEvolution(153,
            building(98, "Goblin Drill", 4.0f, 1313, '7', 0.0f, 0, 100).withLifetime(100)
                .withPeriodicEffect(30, std::make_shared<PeriodicSpawnEffect>(goblinDrillGoblinStats()))
                .withDeployAnywhere()
                .withSpawnEffect(2.0f, 84)
                .withDeathEffect(std::make_shared<SpawnOnDeath>(goblinDrillDeathGoblinStats())),
            building(98, "Goblin Drill", 4.0f, 1313, '7', 0.0f, 0, 100).withLifetime(100)
                .withPeriodicEffect(30, std::make_shared<PeriodicSpawnEffect>(goblinDrillGoblinStats()))
                .withDeployAnywhere()
                .withSpawnEffect(2.0f, 84)
                .withDeathEffect(std::make_shared<SpawnOnDeath>(goblinDrillDeathGoblinStats()
                    .withOffsets({ {-0.4f,0.0f},{0.4f,0.0f},{0.0f,0.4f},{0.0f,-0.4f} }))),
            2, 1);

        // Tesla ("Electro Pulse"): 6-tile radius, low damage and a 0.5 s stun.
        // There is no hiding to emerge from here, so it fires once on deploy.
        addEvolution(154,
            building(26, "Tesla", 4.0f, 1182, 'T', 5.5f, 220, 11).withTargetsAir().withLifetime(250),
            building(26, "Tesla", 4.0f, 1182, 'T', 5.5f, 220, 11).withTargetsAir().withLifetime(250)
                .withSpawnEffect(6.0f, 100, std::make_shared<FreezeOnHit>(5, 0.0f)),
            2, 1);

        // Barbarians: the base 716 hp, the Evolution's hp bonus cut to 0% by
        // the official 4 Aug 2026 notes (was +10%, 760). "Blade Rage": +35%
        // attack speed for 3 s, refreshed on each attack (0.74 ~= 1/1.35); the
        // same notes lengthened it to 5 s, which is not applied here. The
        // movement-speed boost is not modelled.
        addEvolution(155,
            troop(8, "Barbarians", 5.0f, Archetype::MeleeSquad, 716, SPEED_MEDIUM, 0.7f, 192, 14, 'B')
                .withOffsets({ {0.0f, 0.0f}, {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f} }),
            troop(8, "Barbarians", 5.0f, Archetype::MeleeSquad, 716, SPEED_MEDIUM, 0.7f, 192, 14, 'B')
                .withOffsets({ {0.0f, 0.0f}, {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f} })
                .withSelfHasteOnHit(30, 0.74f),
            2, 1);

        // Lumberjack: the ghost (lumberjackGhostStats) composed with the Rage
        // drop.
        addEvolution(156,
            troop(21, "Lumberjack", 4.0f, Archetype::MeleeSquad, 1282, SPEED_VERY_FAST, 0.7f, 256, 8, 'l')
                .withDeathEffect(std::make_shared<AreaBuffOnDeath>(2.5f, 1.75f, 55)),
            troop(21, "Lumberjack", 4.0f, Archetype::MeleeSquad, 1282, SPEED_VERY_FAST, 0.7f, 256, 8, 'l')
                .withDeathEffect(std::make_shared<CompositeDeathEffect>(
                    std::vector<std::shared_ptr<IDeathEffect>>{
                        std::make_shared<AreaBuffOnDeath>(2.5f, 1.75f, 55),
                        std::make_shared<SpawnOnDeath>(lumberjackGhostStats())
                    })),
            2, 1);

        // Executioner ("Axe Smash"): unlocks after 1 cycle. +75% damage within
        // 3.5 tiles (the range-band bonus). The 1.5-tile knockback on the
        // outgoing hit is not modelled.
        addEvolution(157,
            troop(36, "Executioner", 5.0f, Archetype::RangedSquad, 1280, SPEED_MEDIUM, 4.5f, 179, 24, 'x')
                .withTargetsAir().withBoomerang(15).withSplash(1.0f),
            troop(36, "Executioner", 5.0f, Archetype::RangedSquad, 1280, SPEED_MEDIUM, 4.5f, 179, 24, 'x')
                .withTargetsAir().withBoomerang(15).withSplash(1.0f)
                .withRangeBandBonus(0.0f, 3.5f, 1.75f),
            1, 1);

        // Giant Snowball ("Snow Roll"): trades the base push for a 4.5-tile
        // pull, gathering enemies in its path. "Untargetable while trapped" is
        // not modelled.
        addEvolution(158,
            spell(100, "Giant Snowball", 2.0f, 2.5f, 179, 8, '!')
                .withSpellOnHit(std::make_shared<FreezeOnHit>(30, 0.7f))
                .withKnockback(1.8f),
            spell(100, "Giant Snowball", 2.0f, 2.5f, 179, 8, '!')
                .withSpellOnHit(std::make_shared<FreezeOnHit>(40, 0.7f))
                .withKnockback(-4.5f),
            2, 1);

        // Goblin Giant ("Sack-trick"): the real knife Goblins every 2.2 s below
        // 50% hp are an unconditional periodic spawn from deploy; a periodic
        // effect cannot be gated on hp.
        addEvolution(159,
            troop(88, "Goblin Giant", 6.0f, Archetype::MeleeBuildingTargeter, 3110, SPEED_MEDIUM, 1.2f, 176, 15, '`')
                .withSecondaryUnit(goblinGiantSpearGoblinsStats()).withSightRange(7.5f),
            troop(88, "Goblin Giant", 6.0f, Archetype::MeleeBuildingTargeter, 3110, SPEED_MEDIUM, 1.2f, 176, 15, '`')
                .withSecondaryUnit(goblinGiantSpearGoblinsStats()).withSightRange(7.5f)
                .withPeriodicEffect(22, std::make_shared<PeriodicSpawnEffect>(goblinDrillGoblinStats())),
            2, 1);

        // Mega Knight: identical stats. "Mega Uppercut" (hit targets launched 4
        // tiles toward the enemy Crown Tower) is not modelled: there is no
        // "knock the target toward a point" primitive.
        addEvolution(160,
            troop(48, "Mega Knight", 7.0f, Archetype::MeleeSquad, 3993, SPEED_MEDIUM, 1.2f, 268, 17, 'X')
                .withSplash(1.3f).withSpawnEffect(1.3f, 430).withJump(3.5f, 5.0f, 2.0f, 2.2f).withIgnoresRiver(),
            troop(48, "Mega Knight", 7.0f, Archetype::MeleeSquad, 3993, SPEED_MEDIUM, 1.2f, 268, 17, 'X')
                .withSplash(1.3f).withSpawnEffect(1.3f, 430).withJump(3.5f, 5.0f, 2.0f, 2.2f).withIgnoresRiver(),
            2, 1);

        // Battle Ram: the real card's death-spawned Barbarians are themselves
        // evolved; here they get a flat stat buff (the Evolution machinery is
        // keyed to deck cycling). "Head-First Ram": once the charge connects,
        // every later hit keeps double damage (withStickyCharge). The contact
        // damage and knockback to troops along the way are not modelled.
        // Until it evolves it is the ordinary Battle Ram and breaks on its
        // first hit like card 81; the evolved Ram does NOT break, it keeps
        // swinging until destroyed (UPSTREAM_REQUESTS.md item 34).
        // Both forms carry the base card's stats (item 35).
        addEvolution(161,
            troop(81, "Battle Ram", 4.0f, Archetype::MeleeBuildingTargeter, 967, SPEED_MEDIUM, 0.5f, 286, 14, '^')
                .withDeathEffect(std::make_shared<SpawnOnDeath>(battleRamBarbarianStats()))
                .withCharge(3.0f, 2.0f).withChargeSpeed(2.0f).withDieAfterFirstHit().withSightRange(6.5f),
            troop(81, "Battle Ram", 4.0f, Archetype::MeleeBuildingTargeter, 967, SPEED_MEDIUM, 0.5f, 286, 14, '^')
                .withDeathEffect(std::make_shared<SpawnOnDeath>(battleRamEvolvedBarbarianStats()))
                .withCharge(3.0f, 2.0f).withChargeSpeed(2.0f).withStickyCharge().withSightRange(6.5f),
            2, 1);

        // Royal Hogs: identical stats. "Hog Flight" (spawn flying, then fall
        // for 155% damage on the first attack) is not modelled, and how it
        // would stack with the charge bonus is unsourced.
        addEvolution(162,
            troop(82, "Royal Hogs", 5.0f, Archetype::MeleeBuildingTargeter, 837, SPEED_VERY_FAST, 0.75f, 74, 12, '_')
                .withOffsets({ {-1.0f, -0.3f}, {-0.3f, 0.3f}, {0.3f, -0.3f}, {1.0f, 0.3f} })
                .withSightRange(9.5f).withIgnoresRiver(),  // no charge: see id 82
            troop(82, "Royal Hogs", 5.0f, Archetype::MeleeBuildingTargeter, 837, SPEED_VERY_FAST, 0.75f, 74, 12, '_')
                .withOffsets({ {-1.0f, -0.3f}, {-0.3f, 0.3f}, {0.3f, -0.3f}, {1.0f, 0.3f} })
                .withSightRange(9.5f).withIgnoresRiver(),  // no charge: see id 82
            2, 1);

        // Inferno Dragon ("Damage Charge-up"): identical stats to id 56. Losing
        // or having no target holds the ramp stage for a 9 s grace period, so a
        // new target inherits it (a stun still resets at once); and a fourth
        // stage after 20 s of continuous attacking deals double the max. The
        // sources disagree (49 ticks vs 20 s); the seconds figure is used.
        addEvolution(163,
            troop(56, "Inferno Dragon", 4.0f, Archetype::MeleeSquad, 1295, SPEED_MEDIUM, 3.5f, 422, 4, '4')
                .withFlying().withTargetsAir()
                .withDamageRamp(20, 40, 36.0f / 422.0f, 120.0f / 422.0f),
            troop(56, "Inferno Dragon", 4.0f, Archetype::MeleeSquad, 1295, SPEED_MEDIUM, 3.5f, 422, 4, '4')
                .withFlying().withTargetsAir()
                .withDamageRamp(20, 40, 36.0f / 422.0f, 120.0f / 422.0f)
                .withRampStage4(200, 2.0f)
                .withRampGracePeriod(90),
            2, 1);

        // === Mirror ===
        // Registered so it can occupy a deck slot. GameManager::playCard
        // substitutes the last card played (cost + 1), so this entry's own
        // cost, flags and footprint are never read at play time.
        add(spell(164, "Mirror", 3.0f, 0.0f, 0, 0, '='));

        // === Spirit Empress ===
        // Registered at the ground form's cost (3.0) for the hand display and
        // placement; playCard always spawns from SpiritEmpressForms.h instead.
        // Not a Champion: it has no activated ability.
        // client merge_maiden.toml: Hitpoints 438 -> 1121, Damage 125 -> 320, Fast (was 926, 249, 0.85 off-tier) (2026-10-28)
        add(troop(165, "Spirit Empress", 3.0f, Archetype::MeleeSquad, 1121, SPEED_FAST, 1.2f, 320, 12, '<'));

        // === Heroes ===
        // A Hero gives an existing troop a second, ability-carrying form, using
        // the Champion machinery via isHero. Base stats are copied verbatim
        // from the engine's own base-card registration; only the ability is
        // added.

        // Hero Mini P.E.K.K.A. (base: id 5). "Breakfast Boost": a flat one-time
        // hp and damage boost (HeroMiniPekkaBoostEffect).
        add(troop(170, "Hero Mini P.E.K.K.A.", 4.0f, Archetype::MeleeSquad, 1390, SPEED_FAST, 0.8f, 755, 16, 'M')
            .withHeroAbility(1.0f, 0, std::make_shared<HeroMiniPekkaBoostEffect>(210, 1.15f), 1));

        // Hero Musketeer (base: id 6). "Trusty Turret": a short-range turret
        // with a 10 s lifetime (heroMusketeerTurretStats).
        add(troop(168, "Hero Musketeer", 4.0f, Archetype::RangedSquad, 721, SPEED_MEDIUM, 6.0f, 217, 10, 'U')
            .withTargetsAir().withSightRange(6.0f)
            .withHeroAbility(3.0f, 220, std::make_shared<SpawnOnAbility>(heroMusketeerTurretStats())));

        // Hero Goblins (base: id 4). "Banner Brigade" (1 elixir, one use,
        // within 7 s of the last goblin dying): a fresh squad at the death
        // position. withPostDeathAbility, since there is no alive-path ability.
        // The new squad uses plain Goblins stats, so it cannot chain.
        add(troop(172, "Hero Goblins", 2.0f, Archetype::MeleeSquad, 202, SPEED_VERY_FAST, 0.5f, 120, 11, 'g')
            .withOffsets({ {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f} })
            .withPostDeathAbility(1.0f, 70, std::make_shared<PeriodicSpawnEffect>(
                troop(-48, "Goblins", 0.0f, Archetype::MeleeSquad, 202, SPEED_VERY_FAST, 0.5f, 125, 11, 'g')
                    .withOffsets({ {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f} }))));

        // Hero Knight (base: id 0). "Triumphant Taunt" (2 elixir, 25 s
        // cooldown): a shield, and enemies within 6.5 tiles must attack him for
        // 5 s (HeroKnightTauntEffect). The shield amount (~half his hp) is not
        // sourced.
        add(troop(166, "Hero Knight", 3.0f, Archetype::MeleeSquad, 1766, SPEED_MEDIUM, 1.2f, 202, 12, 'K')
            .withHeroAbility(2.0f, 250, std::make_shared<HeroKnightTauntEffect>(880, 50, 6.5f)));

        // Hero Wizard (base: id 11). "Fiery Flight" (1 elixir, 20 s cooldown):
        // flies for 5 s, each landed attack pulsing a damaging, pulling tornado
        // on the target (HeroWizardFieryFlightEffect). The 0.5-tile pull
        // approximates the sourced "~50% pull strength".
        add(troop(167, "Hero Wizard", 5.0f, Archetype::RangedSquad, 755, SPEED_MEDIUM, 5.5f, 281, 14, 'W')
            .withTargetsAir().withSplash(1.5f)
            .withHeroAbility(1.0f, 200, std::make_shared<HeroWizardFieryFlightEffect>(50, 4.0f, 20, 0.5f)));

        // Hero Giant (base: id 2). "Heroic Hurl" (2 elixir, 14 s cooldown):
        // throws the highest-HP enemy troop within 3.0 tiles (not sourced) to
        // the other lane and stuns it 2 s (HeroGiantHurlEffect).
        add(troop(169, "Hero Giant", 5.0f, Archetype::MeleeBuildingTargeter, 3968, SPEED_SLOW, 1.2f, 253, 15, 'G')
            .withSightRange(7.5f)
            .withHeroAbility(2.0f, 140, std::make_shared<HeroGiantHurlEffect>(3.0f, 20)));

        // Hero Mega Minion (base: id 43). "Wounding Warp" (2 elixir, one use,
        // locked for the first 1.5 s): teleports to the lowest-HP enemy
        // anywhere and deals bonus damage (HeroMegaMinionWarpEffect). The bonus
        // damage is not sourced.
        add(troop(173, "Hero Mega Minion", 3.0f, Archetype::MeleeSquad, 837, SPEED_MEDIUM, 1.6f, 312, 15, 'F')
            .withFlying().withTargetsAir()
            .withHeroAbility(2.0f, 0, std::make_shared<HeroMegaMinionWarpEffect>(300), 1)
            .withInitialAbilityCooldown(15));

        // Hero Magic Archer (base: id 63). "Triple Threat" (2 elixir, 25 s
        // cooldown): dashes back 5 tiles, leaves a decoy, and gains a 7 s
        // multi-shot window (HeroMagicArcherTripleThreatEffect).
        add(troop(171, "Hero Magic Archer", 4.0f, Archetype::RangedSquad, 529, SPEED_MEDIUM, 7.0f, 143, 11, '#')
            .withTargetsAir()
            .withSplash(0.25f).withLineSplash(11.0f).withSightRange(7.5f)
            .withHeroAbility(2.0f, 250, std::make_shared<HeroMagicArcherTripleThreatEffect>(
                5.0f, heroMagicArcherDecoyStats(), 70)));

        // Hero Ice Golem (base: id 40, including the slow on its death
        // explosion). "Snowstorm" (2 elixir, 17 s cooldown): three staggered
        // blasts in a 4-tile radius, the third a 1.5 s freeze, with reduced
        // Crown Tower damage (HeroIceGolemSnowstormEffect). Per-blast damage,
        // knockback and slow are not sourced.
        add(troop(175, "Hero Ice Golem", 2.0f, Archetype::MeleeBuildingTargeter, 1315, SPEED_SLOW, 0.75f, 84, 25, 'c')
            .withDeathEffect(std::make_shared<AreaDamageOnDeath>(
                2.0f, 84, std::make_shared<FreezeOnHit>(30, 0.65f))).withSightRange(7.0f)
            .withHeroAbility(2.0f, 170, std::make_shared<HeroIceGolemSnowstormEffect>(4.0f, 80, 1.0f, 20, 0.6f, 15)));

        // Hero Barbarian Barrel (base: id 101). The ability lives on the
        // spawned Barbarian (heroBarbarianBarrelBarbarianStats), since the
        // rolling spell never has a turn on which activating anything would
        // mean something. isHero is set directly so validateDeckSlots and
        // seedSlotState treat id 174 as Hero-eligible; it has no runtime effect
        // on the spell. "Rowdy Reroll" (1 elixir, one use): see
        // HeroBarbarianBarrelRerollEffect.
        {
            // client spells_hero_form.csv BarbLog_hero: the same BarbLogProjectile, 215 (was 232) (2026-10-28)
            CardStats heroBarbarianBarrelSpellStats = spell(174, "Hero Barbarian Barrel", 2.0f, 2.5f, 215, 8, '#')
                .withGroundOnly()
                .withOwnSideOnly()
                .withRollingSweep(4.5f, 2.6f, 0.5f, 0.0f)
                .withSpellSpawn(std::make_shared<PeriodicSpawnEffect>(heroBarbarianBarrelBarbarianStats()));
            heroBarbarianBarrelSpellStats.isHero = true;
            add(heroBarbarianBarrelSpellStats);
        }

    }

public:
    static CardRegistry& getInstance() {
        static CardRegistry instance;
        return instance;
    }

    const CardDefinition* getCard(int id) const {
        auto it = cards.find(id);
        if (it != cards.end()) {
            return &(it->second);
        }
        return nullptr;
    }

    const CardStats* getStats(int id) const {
        auto it = statsById.find(id);
        return it == statsById.end() ? nullptr : &it->second;
    }

    const std::unordered_map<int, CardDefinition>& getAllCards() const {
        return cards;
    }
};

// How many Champions a deck holds. The real game allows at most one; nothing
// here enforces that. For callers that want to check a deck; unused by the
// engine.
inline int countChampions(const std::vector<int>& deck) {
    int count = 0;
    for (int cardId : deck) {
        const CardDefinition* def = CardRegistry::getInstance().getCard(cardId);
        if (def && def->isChampion) count++;
    }
    return count;
}

// As countChampions, for Heroes.
inline int countHeroes(const std::vector<int>& deck) {
    int count = 0;
    for (int cardId : deck) {
        const CardDefinition* def = CardRegistry::getInstance().getCard(cardId);
        if (def && def->isHero) count++;
    }
    return count;
}

// Deck slot legality: slot 0 (Evolution) takes an Evolution or a plain card;
// slot 1 (Heroic) a Champion, a Hero or a plain card; slot 2 (Wild Card) any of
// these; slots 3-7 plain only. Champions and Heroes share slots 1 and 2, as in
// the real game. A real gate: GameManager::reset and setOpponentDeck throw on a
// non-empty result. Returns "" for a legal deck, else the reason.
inline std::string validateDeckSlots(const std::vector<int>& deck) {
    if (deck.size() != 8) {
        return "deck must have exactly 8 cards (got " + std::to_string(deck.size()) + ")";
    }
    for (int i = 0; i < 8; ++i) {
        const CardDefinition* def = CardRegistry::getInstance().getCard(deck[i]);
        if (!def) {
            return "slot " + std::to_string(i) + ": card id " + std::to_string(deck[i]) + " is not a registered card";
        }
        bool specialUnitAllowed = (i == 1 || i == 2); // Heroic / Wild Card: Champion or Hero
        bool evolutionAllowed = (i == 0 || i == 2);   // Evolution slot / Wild Card
        if ((def->isChampion || def->isHero) && !specialUnitAllowed) {
            return "slot " + std::to_string(i) + ": " + def->name +
                " is a Champion/Hero, only allowed in slot 1 (Heroic) or slot 2 (Wild Card)";
        }
        if (def->isEvolution && !evolutionAllowed) {
            return "slot " + std::to_string(i) + ": " + def->name +
                " is an Evolution, only allowed in slot 0 or slot 2 (Wild Card)";
        }
    }
    return "";
}
