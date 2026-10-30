#pragma once
#include "CardEntity.h"
#include "Board.h"
#include "LanePath.h"
#include "OnHitEffect.h"
#include "OnDamageTakenEffect.h"
#include "DeathEffect.h"
#include "PeriodicEffect.h"
#include "AbilityEffect.h"
#include "ClientMechanics.h"
#include "Tick.h"
#include <unordered_map>
#include <memory>
#include <limits>
#include <vector>
#include <algorithm>

class CombatEntity;

// Declared here, defined after the class, because update() calls them from
// inside the class body. applyAreaBuff returns the entities it buffed.
inline std::vector<std::shared_ptr<CombatEntity>> applyAreaBuff(Board& board, const Vector2D& origin, float radius, int excludeId,
    int team, float multiplier, int durationTicks, int maxTargets);
inline void applyAreaHeal(Board& board, const Vector2D& origin, float radius, int excludeId,
    int team, int amount);
// Declared early for the same reason (Mega Knight's jump in update()).
// hitsAir false: a ground-only attacker's splash skips flying units (the Bomb
// Tower's bombs; Building::performAttack passes its own targetsAir).
inline void applySplashDamage(Board& board, const Vector2D& origin, float radius, int excludeId,
    int attackerId, int attackerTeam, int attackerCardId, int dealt, bool hitsAir = true);
// Declared early for Evolved Valkyrie's on-hit pull in update().
inline void applyPullNearby(Board& board, const Vector2D& origin, float radius, float distance,
    int excludeId, int attackerTeam);
// Declared early for Hero Knight's taunt (HeroKnightTauntEffect).
inline void applyTauntNearby(Board& board, const Vector2D& origin, float radius, int ticks,
    int taunterId, int taunterTeam);

class CombatEntity : public CardEntity {
protected:
    float attackRange;
    int damage;
    int attackCooldown;
    float currentCooldown;
    std::vector<std::shared_ptr<IOnHitEffect>> onHitEffects;

    // Ramp bookkeeping: the locked target and how many consecutive ticks on it.
    // Tracked for every entity so the logic lives in one place.
    int currentTargetId = -1;
    int ticksOnTarget = 0;
    // Ticks since the last landed hit. Consulted only with rampGracePeriodTicks
    // > 0 (Inferno Dragon Evolution): a lost or switched target then keeps its
    // ramp stage for up to the grace period instead of resetting at once. A
    // stun still resets immediately.
    int ticksSinceLastHit = 0;

    // How many targets this attack landed on, set just before performAttack()
    // so getCurrentDamage() can divide by it.
    int currentHitCount = 1;

    // Ticks since this unit last started an attack; "never" at spawn. Read by
    // the hot periodic interval (Evolved Furnace spawns faster only while
    // attacking). Not ticksSinceLastHit, which also resets on losing a target.
    static constexpr int TICKS_SINCE_ATTACK_NEVER = 1 << 20;
    int ticksSinceAttack = TICKS_SINCE_ATTACK_NEVER;

    // The freezes in force, each running its own duration (see applyFreeze).
    struct FreezeSlot { int ticks = 0; float factor = 1.0f; };
    static constexpr int FREEZE_SLOT_COUNT = 3;
    FreezeSlot freezeSlots[FREEZE_SLOT_COUNT];

public:
    // The entity this one is locked onto (-1 for none). Read-only view for
    // ClashEnv::getUnits.
    int getCurrentTargetId() const { return currentTargetId; }

    // Freeze lives here, not on Entity: only things that attack or move can be
    // frozen. A stun is factor 0; a slow is 0 < factor < 1 and scales both
    // movement and the attack cooldown.
    //
    // Concurrent freezes are held separately (freezeSlots) and each expires on
    // its own clock; the strongest active one applies. So a slow that lands
    // after an earlier stun has ended is a slow, and a 0.5 s stun during a 3 s
    // slow is 0.5 s of stun and then the rest of the slow (UPSTREAM_REQUESTS.md
    // item 32m: the old single slot kept the strongest factor for the longest
    // duration, and never reset it). freezeTicks and freezeSlow are the view:
    // the longest remaining freeze and the factor in force, recomputed by
    // applyFreeze() and update(). Read them; write through applyFreeze.
    int freezeTicks = 0;
    float freezeSlow = 1.0f;

    // Whether this unit was frozen when the tick began, and at what factor: set
    // at the top of update(), before the freezes tick down, and read by
    // Troop::moveTowards later in the same call, so attacks and movement see
    // the same freeze. Recomputed every update().
    bool frozenThisTick = false;
    float freezeSlowThisTick = 1.0f;

    // Movement-only slow (Poison: enemy troops 15% slower inside it, attack
    // speed untouched). Separate from the freezes, which also slow attacks;
    // Troop::moveTowards applies the stronger of the two.
    int moveSlowTicks = 0;
    float moveSlowFactor = 1.0f;
    float moveSlowThisTick = 1.0f;

    // Ticks left of deploy time (CardStats.h, DEPLOY_TIME_TICKS): on the board,
    // targetable and damageable, but not moving, targeting or attacking.
    //
    // Not freezeTicks: a freeze means "stunned" and resets the ramp and charge,
    // and a new unit has not been stunned. The implicit copy carries it, so
    // snapshots keep the exact remaining deploy time.
    int deployTicksRemaining = 0;

    // First-hit delay (the card's load time, LoadTimes.h): on locking onto a
    // new target the unit winds up this many ticks before its first hit;
    // later hits on the same target follow the attack cooldown. A stun or a
    // retarget restarts the windup. Off for every unit while loadTimesEnabled
    // is false (the engine before 2026-10-23).
    int loadTicks = 0;
    // The fraction of a direct hit's damage dealt to a Crown Tower (SpellTowerDamage.h: the Miner's 25%).
    float crownTowerFraction = 1.0f;
    int windupTargetId = -1;
    int windupTicksLeft = 0;
    static inline bool loadTimesEnabled = true;
    // How the load time is used (2026-10-29, bot-77 sim fidelity):
    //   LOAD_MODEL_PRELOAD (default): the game's model. A unit loads loadTicks
    //     of its attack whenever it is not attacking (walking, deploying,
    //     idle), so a loaded unit's hit lands attackCooldown - loadTicks
    //     after it starts attacking; after a hit it reloads loadTicks, then
    //     winds up again, so continuous hits stay attackCooldown apart.
    //     Sources: the client's logic tables (LoadTime, HitSpeed per unit;
    //     LoadFirstHit set only on Sparky); RoyaleAPI, "The Secret Stats"
    //     (first hit = hit time - load time; Knight 1.2 - 0.7 = 0.5 s);
    //     Supercell's balance notes ("first hit speed", Archers 0.1 -> 0.5 s
    //     in April 2023 = 0.9 - 0.4). A hit that has started keeps its target
    //     (globals LOGIC_PRESERVE_TARGET_IF_HIT_STARTED) unless the target
    //     gets more than HIT_CANCEL_DISTANCE beyond reach
    //     (LOGIC_CANCEL_HIT_FROM_LONG_DISTANCE_RANGE 1500). A stun cancels a
    //     started hit and keeps the load (the cooldown is held, as before).
    //     Cards whose load is not shorter than their hit speed (Inferno Tower
    //     and Dragon, Mighty Miner: the load is their lock-on) keep the old
    //     model. A card with no load entry (spawned bodies, towers) is
    //     unchanged: no windup.
    //   LOAD_MODEL_WINDUP_PER_TARGET: the engine before 2026-10-29, which
    //     waited the full loadTicks on every new target.
    static constexpr int LOAD_MODEL_WINDUP_PER_TARGET = 0;
    static constexpr int LOAD_MODEL_PRELOAD = 1;
    static inline int loadModel = LOAD_MODEL_PRELOAD;
    static constexpr float HIT_CANCEL_DISTANCE = 1.5f;
    // A loaded unit's first hit, in ticks: the client's HitSpeed - LoadTime
    // (LoadTimes.h firstHitTicksForCard; -1 = none, the old model applies).
    int firstHitTicks = -1;
    bool usesPreload() const {
        return loadTimesEnabled && loadModel == LOAD_MODEL_PRELOAD && firstHitTicks >= 0;
    }
    int preloadWindupTicks() const { return firstHitTicks; }
    // After a hit: the rest of the attack cycle, reloading.
    int preloadReloadTicks() const { return std::max(0, attackCooldown - firstHitTicks); }

    // ---- The client attack cycle (Fidelity::reloadFromAttackStart and the switches after it; bot-77
    // docs/client-code-notes.md section 1: combat update 0x71fff599e4, timer advance 0x71fff5ac3c, set target
    // 0x71fff558e0, target removed 0x71fff591f8). Times in ms; the engine steps TICK_MS (50 ms) a tick, as the
    // client does. ----
    // LoadTime for the cycle (LoadTimes.h loadMsForCard; crown towers: GameManager); -1 = not on the cycle.
    int clientLoadMs = -1;
    // Runs while attacking, by TICK_MS x the hit-speed factor a tick; a hit lands each time it crosses a multiple
    // of the hit speed. 0 = not attacking; an attack starts at clientLoadMs - loadMissingMs.
    int attackTimerMs = 0;
    // Load still missing: counts down TICK_MS every tick whatever the unit does; set to the full load when an
    // attack starts.
    int loadMissingMs = 0;
    // An attack is under way (the client's attack timer is non-zero; here a start at 0 counts too).
    bool attacking = false;
    // > 0 while a lost target's swing waits for a new target in reach (ATTACK_FINISH_TIME_MS).
    int finishWindowMs = 0;
    // The swing has started: the attack timer is more than CLIENT_HIT_STARTED_MS into its cycle.
    bool swingStarted = false;
    // LoadFirstHit (Sparky): spawns unloaded, reloads fully after every attack and on every stunned tick.
    bool loadFirstHit = false;
    // Special attack windup (Fidelity::specialLoadTime; SpecialLoadTime, ClientMechanics.h): the hook winds up
    // specialLoadMs on its target before it fires.
    int specialLoadMs = 0;
    int specialLoadLeftMs = 0;
    int specialTargetId = -1;

    // Whether this unit runs the client attack cycle. Tower overrides it (Fidelity::kingFirstShotDelay /
    // princessFirstShotDelay).
    virtual bool clientCycleOn() const {
        return Fidelity::reloadFromAttackStart && clientLoadMs >= 0 && usesPreload();
    }
    // The pending-damage rule's "attacker with a projectile" (the client's data has a Projectile): any unit with a
    // client projectile speed, and the crown towers.
    bool hasClientProjectile() const { return clientShotSpeed > 0.0f || isTower(); }

    // Damage-over-time mark (Dart Goblin / Firecracker Evolutions; see
    // applyDot, PoisonOnHit). Ticks down independently of freeze and curse.
    int dotDamagePerTick = 0;
    int dotTicksRemaining = 0;
    int dotTickInterval = 0;
    int dotTicksUntilNextDamage = 0;

    // Whether findTarget() may pick a flyer. Only read on `this`, unlike
    // isFlying.
    bool targetsAir = false;

    // The unit's own projectile (Fidelity::clientProjectiles): its client Speed in engine tiles per tick (0 = the
    // engine's fixed speeds) and whether it homes. Set by CardFactories::applyCardMetadata / GameManager (towers).
    float clientShotSpeed = 0.0f;
    bool clientShotHoming = true;
    float shotSpeed(float legacy) const {
        return (Fidelity::clientProjectiles && clientShotSpeed > 0.0f) ? clientShotSpeed : legacy;
    }
    bool shotHoming() const { return !Fidelity::clientProjectiles || clientShotHoming; }

    // Fired once by onDeath() (e.g. Golem spawning Golemites).
    std::shared_ptr<IDeathEffect> deathEffect;

    // Ramping damage (Inferno Tower): the longer the lock on the same target,
    // the more damage: rampStartFraction until rampMidTick, rampMidFraction
    // until rampFullTick, then full. Reset by a target switch, losing the
    // target, or a stun. rampFullTick == 0 disables it.
    int rampMidTick = 0;
    int rampFullTick = 0;
    float rampStartFraction = 1.0f;
    float rampMidFraction = 1.0f;

    // Fourth ramp stage beyond rampFullTick (Inferno Dragon Evolution only). 0
    // disables it.
    int rampStage4Tick = 0;
    float rampStage4Fraction = 1.0f;

    // Grace period before a lost or switched target resets the ramp (see
    // ticksSinceLastHit). 0 resets at once.
    int rampGracePeriodTicks = 0;

    // Split-target attacks (Electro Wizard): hits up to this many of the
    // closest enemies, each for damage / (targets actually hit).
    int maxSplitTargets = 1;
    // Temporary split-target window (Hero Magic Archer's Triple Threat), an
    // approximation of extra arrows via the split machinery.
    // baseMaxSplitTargets is restored when the window closes.
    int temporarySplitTargetsTicksRemaining = 0;
    int baseMaxSplitTargets = 1;

    // Electro Dragon's chain: every chained target takes full damage rather
    // than a share.
    bool splitTargetsFullDamage = false;

    // Splash (Wizard, Bowler, Valkyrie...): every regular attack also hits
    // every other enemy within this radius of the primary target, ground and
    // air alike, regardless of targetsAir. A free function (applySplashDamage)
    // because Projectile needs it too.
    float splashRadius = 0.0f;

    // Shield (Guards, Royal Recruits, Dark Prince, Cannon Cart): a second hp
    // pool that absorbs damage from any source first. Does not regenerate.
    int shieldHp = 0;
    // Timed shield expiry (Hero Knight's taunt: the shield lasts 5 s even if
    // not depleted). 0 leaves shieldHp permanent.
    int shieldExpiresTicksRemaining = 0;

    // Forced retarget (Hero Knight's taunt): while > 0, update() attacks
    // forcedTargetEntityId instead of its normal target (see applyTauntNearby).
    int forcedTargetEntityId = -1;
    int forcedTargetTicksRemaining = 0;

    // Charge (Prince, Battle Ram, Ram Rider, Royal Hogs, Bandit): after moving
    // chargeThreshold tiles without landing a hit, the next attack deals
    // chargeMultiplier x damage, and progress resets either way. 0 disables it.
    float chargeThreshold = 0.0f;
    float chargeMultiplier = 1.0f;
    float chargeProgress = 0.0f;
    // While charging (chargeProgress has reached chargeThreshold), a troop
    // moves at chargeSpeedMultiplier x its speed (Troop::moveTowards).
    float chargeSpeedMultiplier = 1.0f;

    bool isCharging() const { return chargeThreshold > 0.0f && chargeProgress >= chargeThreshold; }

    // Sticky charge (Evolved Battle Ram): once reached, the charge bonus
    // applies to every hit for the rest of its life. chargeHasStuck is
    // internal.
    bool chargeIsSticky = false;
    bool chargeHasStuck = false;

    // Parry (Ronin): every parryIntervalTicks, the next incoming hit is
    // negated. The real card also reflects damage and parries only ground
    // melee; neither is modelled (takeDamage has no attacker, and there is no
    // melee/ranged distinction). Ready at spawn.
    int parryIntervalTicks = 0;
    int parryTicksUntilReady = 0;

    // Hook (Fisherman): a target beyond attackRange but within hookRange is
    // pulled to just inside melee range. Consumes the cooldown; the damage
    // lands on a later hit.
    float hookRange = 0.0f;

    // Invisibility (Royal Ghost, Suspicious Bush): untargetable except briefly
    // after landing a hit.
    bool startsInvisible = false;
    int revealTicksAfterAttack = 0;
    int visibleTicksRemaining = 0;

    // Periodic effects while alive (Witch, Night Witch, Furnace, huts,
    // Tombstone, Goblin Drill): fires periodicEffect every
    // periodicIntervalTicks. 0 disables it.
    std::shared_ptr<IPeriodicEffect> periodicEffect;
    int periodicIntervalTicks = 0;
    int periodicTicksUntilNext = 0;
    // Evolved Furnace: the faster interval used only while attacking (0 = none),
    // and the alternating sideways spawn offset (0 = on the entity).
    int periodicHotIntervalTicks = 0;
    float periodicSideOffset = 0.0f;
    int periodicSpawnCount = 0;

    // Temporary damage buff (Rage, Rune Giant's enchant). Real Rage also speeds
    // movement and attacks; only damage is modelled, since speed lives on
    // Troop.
    float buffDamageMultiplier = 1.0f;
    int buffTicksRemaining = 0;

    // Damage-taken debuff (Mother Witch's curse), applied in takeDamage()
    // before shield and parry.
    float curseDamageTakenMultiplier = 1.0f;
    int curseTicksRemaining = 0;
    // Latch: Mother Witch's hog spawn is armed once (see CursedHogOnHit); the
    // curse's duration still refreshes.
    bool curseDeathSpawnAttached = false;
    // Latch: the Royal Chef serves each ally once (see RoyalChefBuffEffect).
    bool royalChefServed = false;

    // Ally auras on landed attacks: Rune Giant's every-Nth buff, Battle
    // Healer's heal. Independent opt-ins.
    float auraRadius = 0.0f;
    int auraMaxTargets = 1000000; // everyone in radius unless capped
    int auraEveryNAttacks = 0;
    int attacksSinceAura = 0;
    float auraBuffMultiplier = 1.0f;
    int auraBuffDurationTicks = 0;
    int healAllyAmount = 0;

    // Every Nth landed hit deals burstDamageMultiplier x damage (Dagger
    // Duchess, some Evolutions). currentHitIsBurst is set just before
    // performAttack and read by getCurrentDamage().
    int burstEveryNAttacks = 0;
    float burstDamageMultiplier = 1.0f;
    int attacksSinceBurst = 0;
    bool currentHitIsBurst = false;

    // Attack combo (Monk: 140 / 140 / 422): attack k deals
    // attackSequenceDamage[k % n] in place of `damage`. The counter carries
    // across targets and is never reset. The last attack of the combo knocks
    // its target back attackSequencePushback tiles (Monk's MeleePushback3;
    // buildings are never moved). Empty disables it.
    std::vector<int> attackSequenceDamage;
    int attackSequenceIndex = 0;
    float attackSequencePushback = 0.0f;

    // Fired when this entity actually takes damage; the receiving-end
    // counterpart of onHitEffects.
    std::shared_ptr<IOnDamageTakenEffect> onDamageTakenEffect;

    // Self-heal on landing a hit (Evolved Bats); healOnHitMaxHp is an absolute
    // cap. With a second-pulse delay, the same amount heals again that many
    // ticks later (pendingHealPulseTicks counts it down).
    int healOnHitAmount = 0;
    int healOnHitMaxHp = 0;
    int healOnHitSecondPulseDelayTicks = 0;
    int pendingHealPulseTicks = 0;

    // Self-spawn on landing a hit (Evolved Skeletons); the cap lives in
    // CappedSpawnOnHitEffect.
    std::shared_ptr<IPeriodicEffect> onHitSpawnEffect;

    // Pull nearby enemies on landing a hit (Evolved Valkyrie). Buildings are
    // never moved (pullToward), but onHitPullDamage reaches them, towers
    // included, as sourced.
    float onHitPullRadius = 0.0f;
    float onHitPullDistance = 0.0f;
    int onHitPullDamage = 0;

    // Kamikaze (Wall Breakers, the Spirits): dies after its one hit. A ranged
    // unit dies when the shot is launched, slightly before the real on-arrival
    // timing.
    bool dieAfterFirstHit = false;

    // Dash invulnerability (Bandit). With no separate dash state, immune once
    // past half the charge distance: a longer window than the real ~0.8 s.
    bool chargeGrantsInvulnerability = false;
    // Mid-dash (Bandit): the invulnerable second half of the charge. Also when the unit leaves body collision
    // (Fidelity::clientCollision: the client's dash state 3 neither pushes nor is pushed).
    bool isDashing() const {
        return chargeGrantsInvulnerability && chargeThreshold > 0.0f && chargeProgress >= chargeThreshold * 0.5f;
    }

    // A stun resets, not just slows, the attack cooldown (Sparky restarts her
    // wind-up after a Zap).
    bool resetCooldownOnFreeze = false;

    // Recoil after attacking (Firecracker); see pushAway.
    float recoilDistance = 0.0f;

    // Minimum range / blind spot (Mortar). The real attacker idles against a
    // target inside it; here such a target is simply not valid, so the attacker
    // picks the next legal one.
    float minAttackRange = 0.0f;

    // Sight / aggro range (distinct from attackRange; see findTarget()): how
    // far this unit can detect an enemy to go fight instead of heading for a
    // tower. Sourced per card; 5.5 is the source's standard for cards without
    // their own value. Set per card for buildings too, not derived from
    // attackRange.
    float sightRange = 5.5f;

    // HP-threshold transform (Cannon Cart: at <= 50% hp it grounds itself and
    // self-destructs after a fixed lifespan). The fraction is of
    // transformCheckMaxHp, the spawn hp. Grounding reuses applyFreeze(ticks,
    // 0).
    float transformAtHpFraction = 0.0f;
    int transformCheckMaxHp = 0;
    int transformLifetimeTicks = 0;
    bool transformBecomesStationary = false;
    bool hasTransformed = false;
    int transformTicksRemaining = 0;

    // Transform into a different unit (Goblin Demolisher): an entity's C++ type
    // cannot change in place, so it dies at the threshold and
    // transformDeathEffect spawns the new form.
    bool transformKillsSelf = false;
    // A separate slot from `deathEffect`: a hit that kills outright, skipping
    // the threshold, must not spawn the transformed form.
    std::shared_ptr<IDeathEffect> transformDeathEffect;

    // Periodic jump (Mega Knight): a ground target between jumpMinRange and
    // jumpMaxRange is closed on instantly with a boosted, splashing hit.
    float jumpMinRange = 0.0f;
    float jumpMaxRange = 0.0f;
    float jumpDamageMultiplier = 1.0f;
    float jumpSplashRadius = 0.0f;

    // Piercing line (Bowler, Magic Archer): hits everyone within splashRadius
    // (reused as the half-width) of the line from this attacker toward the
    // target, out to lineSplashRange.
    bool lineSplash = false;
    float lineSplashRange = 0.0f;

    // Range falloff (Hunter). Not a sourced mechanic: the real card fires 10
    // randomly spread pellets and no curve is published. A deterministic linear
    // scale approximates "weaker at range", keeping combat reproducible.
    bool rangeFalloff = false;
    float rangeFalloffMinFraction = 1.0f; // damage fraction at max range

    // Bonus damage within a distance band (Archers' Power Shot: +50% at 4-6
    // tiles; Executioner's Axe Smash: +75% at <= 3.5), from lastAttackDistance.
    // rangeBandMaxDist == 0 disables it.
    float rangeBandMinDist = 0.0f;
    float rangeBandMaxDist = 0.0f;
    float rangeBandDamageMultiplier = 1.0f;

    // Distance to the target at the last attack, for getCurrentDamage(), which
    // has no target of its own.
    float lastAttackDistance = 0.0f;

    // Champion marker. Gates no combat behaviour; lets GameManager find the
    // deployed Champion.
    bool isChampion = false;
    // Hero marker; Champion-slot code checks `isChampion || isHero`, so Heroes
    // share the Champion path.
    bool isHero = false;
    // Elixir charged on each activation, on top of the card's deploy cost. 0
    // with a null abilityEffect means no ability.
    float abilityElixirCost = 0.0f;
    // Cooldown between activations. Starts at 0 (ready at deploy) and resets to
    // abilityCooldownTicks on each activation.
    int abilityCooldownTicks = 0;
    int abilityCooldownRemaining = 0;
    // What the ability does (e.g. MightyMinerEscapeEffect in core/). nullptr
    // makes activateAbility() a no-op.
    std::shared_ptr<IAbilityEffect> abilityEffect;
    // -1 is unlimited; otherwise a hard cap on activations (Boss Bandit: 2).
    int abilityUsesRemaining = -1;
    // The deck card whose slot owns this ability when it is not cardId (Hero
    // Barbarian Barrel's Barbarian: 174). 0 = cardId. See
    // CardStats::abilitySlotCardId and GameManager::trackSlotEntities.
    int abilitySlotCardId = 0;

    // Soul collection (Skeleton King): counts deaths within radius, ally or
    // enemy, via onNearbyDeath, up to maxSouls.
    float soulCollectionRadius = 0.0f;
    int soulCount = 0;
    int maxSouls = 0;

    // Timed invisibility plus attack-speed buff (Archer Queen's Cloaking Cape,
    // Boss Bandit's Getaway Grenade). Their movement-speed change is not
    // modelled.
    int temporaryInvisibilityTicksRemaining = 0;
    float temporaryHitSpeedMultiplier = 1.0f;

    // Self-haste on each landed hit, refreshed while attacking (Evolved
    // Barbarians' Blade Rage: +35% attack speed for 3 s). Separate from the
    // ability haste above, which carries invisibility. Movement speed not
    // modelled.
    int selfHasteDurationTicks = 0;
    float selfHasteCooldownMultiplier = 1.0f;
    int selfHasteTicksRemaining = 0;

    // Temporary flight (Hero Wizard's Fiery Flight): isFlying is read live
    // everywhere, so this timer only reverts it. ignoresRiver is fixed at spawn
    // and not toggled.
    int temporaryFlightTicksRemaining = 0;
    // On-hit tornado pulse during the flight window, centred on the TARGET
    // (unlike Evolved Valkyrie's pull, centred on self).
    int flightPulseTicksRemaining = 0;
    float flightPulseRadius = 0.0f;
    int flightPulseDamage = 0;
    float flightPulsePullDistance = 0.0f;

    // Hit-speed ramp while locked on the same target (Little Prince): the
    // cooldown shrinks, rather than the damage growing. 0 disables it.
    int hitSpeedRampMidTick = 0;
    int hitSpeedRampFullTick = 0;
    float hitSpeedRampMidFraction = 1.0f;
    float hitSpeedRampFullFraction = 1.0f;

    CombatEntity(int id, float x, float y, int hp, int team, char symbol,
        float attackRange, int damage, int attackCooldown)
        : CardEntity(id, x, y, hp, team, symbol),
        attackRange(attackRange), damage(damage),
        attackCooldown(attackCooldown), currentCooldown(0.0f) {}

    bool isTargetable() const override {
        return (!startsInvisible || visibleTicksRemaining > 0) && temporaryInvisibilityTicksRemaining <= 0;
    }
    bool isAreaHittable() const override { return true; }  // invisibility hides from targeting, not from area damage

    // Soul collection: any death within radius, ally or enemy.
    void onNearbyDeath(Board&, const Vector2D& deathPosition, int) override {
        if (soulCollectionRadius <= 0.0f || soulCount >= maxSouls) return;
        if (position.distanceTo(deathPosition) <= soulCollectionRadius) {
            soulCount++;
        }
    }

    void applyBuff(float multiplier, int ticks) {
        buffDamageMultiplier = multiplier;
        buffTicksRemaining = ticks;
    }

    void applyCurse(float damageTakenMultiplier, int ticks) {
        curseDamageTakenMultiplier = damageTakenMultiplier;
        curseTicksRemaining = ticks;
    }

    // Fires abilityEffect and restarts the cooldown if configured, off cooldown
    // and not exhausted. Elixir is GameManager's concern and must be checked
    // and deducted before calling. Returns whether it fired.
    bool activateAbility(Board& board) {
        if (!abilityEffect || abilityCooldownRemaining > 0) return false;
        if (abilityUsesRemaining == 0) return false; // exhausted
        abilityEffect->apply(board, *this);
        abilityCooldownRemaining = abilityCooldownTicks;
        if (abilityUsesRemaining > 0) abilityUsesRemaining--;
        return true;
    }

    // What every Clone copy shares (the leaf classes' clone()): a fresh id, 1
    // hp, and no claim on a Champion/Hero slot, so a cloned Champion or Hero
    // never gets its ability (GameManager::trackSlotEntities).
    void becomeCloneCopy(int newId) {
        id = newId;
        hp = 1;
        abilitySlotCardId = 0;
    }

    // Deploy delay before the first shot (X-Bow's slow lock-on). Called once
    // after spawn by CardFactories; the one seam onto the protected cooldown.
    void seedCooldown(int ticks) {
        currentCooldown = static_cast<float>(ticks);
    }

    // Parry is checked before shield, so a parried hit costs no shield. The
    // shield absorbs first and only the excess reaches hp.
    void takeDamage(int amount) override {
        if (isDashing()) {
            return; // mid-dash: immune
        }
        if (curseTicksRemaining > 0) {
            amount = static_cast<int>(amount * curseDamageTakenMultiplier);
        }
        if (parryIntervalTicks > 0 && parryTicksUntilReady <= 0) {
            parryTicksUntilReady = parryIntervalTicks;
            return; // negated
        }
        if (shieldHp > 0) {
            int absorbed = (shieldHp < amount) ? shieldHp : amount;
            shieldHp -= absorbed;
            amount -= absorbed;
        }
        if (amount > 0) {
            Entity::takeDamage(amount);
            if (onDamageTakenEffect) onDamageTakenEffect->apply(*this);
        }
    }

    // The latest application replaces the mark, like applyBuff / applyCurse.
    void applyDot(int damagePerTick, int totalTicks, int tickInterval) {
        dotDamagePerTick = damagePerTick;
        dotTicksRemaining = totalTicks;
        dotTickInterval = tickInterval;
        dotTicksUntilNextDamage = tickInterval;
    }

    // A new freeze never leaves the target better off, and never outlives
    // itself. One of the same strength extends that slot; a different strength
    // takes a slot of its own and runs its own duration. With every slot taken,
    // it displaces the weakest held freeze if it is stronger, else it is
    // dropped (three concurrent strengths are enough for every card).
    void applyFreeze(int ticks, float slowFactor) {
        if (ticks <= 0) return;
        FreezeSlot* slot = nullptr;
        for (auto& s : freezeSlots) {
            if (s.ticks > 0 && s.factor == slowFactor) { slot = &s; break; }
        }
        if (!slot) {
            for (auto& s : freezeSlots) {
                if (s.ticks <= 0) { slot = &s; break; }
            }
        }
        if (!slot) {
            FreezeSlot* weakest = &freezeSlots[0];
            for (auto& s : freezeSlots) {
                if (s.factor > weakest->factor || (s.factor == weakest->factor && s.ticks < weakest->ticks)) weakest = &s;
            }
            if (slowFactor < weakest->factor) {
                slot = weakest;
                slot->ticks = 0;
            }
        }
        if (slot) {
            if (slot->ticks > 0) {
                slot->ticks = std::max(slot->ticks, ticks);
            } else {
                slot->ticks = ticks;
                slot->factor = slowFactor;
            }
            refreshFreezeView();
        }
        if (resetCooldownOnFreeze) {
            // Sparky: a stun empties her load (globals LOGIC_LOAD_FIRST_HIT_RESET_TIMER_WHEN_ZAPPED).
            currentCooldown = static_cast<float>(usesPreload() ? preloadReloadTicks() : attackCooldown);
            windupTargetId = -1;
            windupTicksLeft = 0;
        }
    }

    // Movement only; the stronger of two overlapping move-slows applies, and it
    // ends with its own duration.
    void applyMoveSlow(int ticks, float factor) {
        if (ticks <= 0) return;
        if (moveSlowTicks <= 0 || factor < moveSlowFactor) {
            moveSlowFactor = factor;
            moveSlowTicks = ticks;
        } else if (factor == moveSlowFactor) {
            moveSlowTicks = std::max(moveSlowTicks, ticks);
        }
    }

    // freezeTicks / freezeSlow from the slots: the longest remaining freeze and
    // the strongest factor in force (1.0 when nothing is).
    void refreshFreezeView() {
        freezeTicks = 0;
        freezeSlow = 1.0f;
        for (const auto& s : freezeSlots) {
            if (s.ticks <= 0) continue;
            freezeTicks = std::max(freezeTicks, s.ticks);
            freezeSlow = std::min(freezeSlow, s.factor);
        }
    }

    // Composes extra behaviour onto every successful attack.
    void addOnHitEffect(std::shared_ptr<IOnHitEffect> effect) {
        onHitEffects.push_back(std::move(effect));
    }

    void update(Board& board) override {
        // Deploy time: only the decrement happens here. The bail-out is below
        // the status timers, because a deploying unit is inert, not
        // time-stopped: poison, cooldowns and buffs keep ticking.
        const bool deploying = deployTicksRemaining > 0;
        if (deploying) deployTicksRemaining--;

        // Captured before the decrement, so the last tick of a freeze still
        // counts as frozen, at its own strength, for the ramp reset, the
        // cooldown and movement alike.
        bool wasFrozen = freezeTicks > 0;
        const float slowThisTick = freezeSlow;
        // A true stun (factor 0) acts on nothing this tick: no attack, jump or
        // hook, even with the cooldown already at 0. A slow is also "frozen"
        // here and keeps attacking, only slower.
        const bool stunned = wasFrozen && slowThisTick <= 0.0f;
        // Published for Troop::moveTowards, which runs after the decrement.
        frozenThisTick = wasFrozen;
        freezeSlowThisTick = slowThisTick;
        moveSlowThisTick = (moveSlowTicks > 0) ? moveSlowFactor : 1.0f;
        if (moveSlowTicks > 0 && --moveSlowTicks == 0) moveSlowFactor = 1.0f;
        ticksSinceLastHit++; // zeroed below when a hit lands
        // The client cycle's load: TICK_MS a tick, unscaled, whatever the unit is doing.
        if (loadMissingMs > 0) loadMissingMs = std::max(0, loadMissingMs - TICK_MS);
        if (ticksSinceAttack < TICKS_SINCE_ATTACK_NEVER) ticksSinceAttack++; // zeroed when an attack starts

        if (transformAtHpFraction > 0.0f && !hasTransformed && transformCheckMaxHp > 0
            && static_cast<float>(hp) / static_cast<float>(transformCheckMaxHp) <= transformAtHpFraction) {
            hasTransformed = true;
            if (transformKillsSelf) {
                hp = 0;
                if (transformDeathEffect) transformDeathEffect->apply(board, position, team);
                // Dead this tick: skip targeting, movement and attack.
                // cleanDeadEntities fires nothing more, since deathEffect is
                // unset on this card.
                return;
            }
            transformTicksRemaining = transformLifetimeTicks;
            if (transformBecomesStationary) applyFreeze(transformLifetimeTicks, 0.0f);
        }
        if (hasTransformed && transformLifetimeTicks > 0) {
            transformTicksRemaining--;
            if (transformTicksRemaining <= 0) hp = 0;
        }

        if (wasFrozen) {
            for (auto& s : freezeSlots) {
                if (s.ticks > 0) s.ticks--;
            }
            refreshFreezeView();
            if (currentCooldown > 0.0f) {
                currentCooldown -= slowThisTick;
            }
        } else {
            if (currentCooldown > 0.0f) {
                currentCooldown -= 1.0f;
            }
        }

        // Evolved Bats' second heal pulse, half a second after the hit.
        if (pendingHealPulseTicks > 0 && --pendingHealPulseTicks == 0 && hp > 0 && hp < healOnHitMaxHp) {
            hp = std::min(hp + healOnHitAmount, healOnHitMaxHp);
        }

        if (currentCooldown < 0.0f) currentCooldown = 0.0f;

        if (parryIntervalTicks > 0 && parryTicksUntilReady > 0) parryTicksUntilReady--;
        if (startsInvisible && visibleTicksRemaining > 0) visibleTicksRemaining--;
        if (buffTicksRemaining > 0) buffTicksRemaining--;
        if (curseTicksRemaining > 0) curseTicksRemaining--;
        if (abilityCooldownRemaining > 0) abilityCooldownRemaining--;
        if (temporaryInvisibilityTicksRemaining > 0) temporaryInvisibilityTicksRemaining--;
        if (selfHasteTicksRemaining > 0) selfHasteTicksRemaining--;
        if (temporaryFlightTicksRemaining > 0) {
            temporaryFlightTicksRemaining--;
            if (temporaryFlightTicksRemaining == 0) isFlying = false;
        }
        if (flightPulseTicksRemaining > 0) flightPulseTicksRemaining--;
        if (temporarySplitTargetsTicksRemaining > 0) {
            temporarySplitTargetsTicksRemaining--;
            if (temporarySplitTargetsTicksRemaining == 0) maxSplitTargets = baseMaxSplitTargets;
        }
        if (forcedTargetTicksRemaining > 0) forcedTargetTicksRemaining--;
        if (shieldExpiresTicksRemaining > 0) {
            shieldExpiresTicksRemaining--;
            if (shieldExpiresTicksRemaining == 0) shieldHp = 0;
        }

        // The damage-over-time mark, through takeDamage so shield, parry and
        // curse apply.
        if (dotTicksRemaining > 0) {
            dotTicksRemaining--;
            dotTicksUntilNextDamage--;
            if (dotTicksUntilNextDamage <= 0 && dotDamagePerTick > 0) {
                takeDamage(dotDamagePerTick);
                dotTicksUntilNextDamage = dotTickInterval;
            }
        }

        // Above: bookkeeping that runs while deploying. Below: action, which a
        // deploying unit takes none of.
        if (deploying) return;

        if (periodicIntervalTicks > 0) {
            // Evolved Furnace's Hot Spawning: the fast interval runs only while
            // attacking (an attack started within one attack cycle), and going
            // into combat pulls a long pending timer in to it.
            const bool hot = periodicHotIntervalTicks > 0 && ticksSinceAttack <= attackCooldown;
            if (hot && periodicTicksUntilNext > periodicHotIntervalTicks) {
                periodicTicksUntilNext = periodicHotIntervalTicks;
            }
            periodicTicksUntilNext--;
            if (periodicTicksUntilNext <= 0) {
                if (periodicEffect) periodicEffect->apply(board, periodicSpawnPoint(board), team);
                periodicSpawnCount++;
                periodicTicksUntilNext = hot ? periodicHotIntervalTicks : periodicIntervalTicks;
            }
        }

        if (clientCycleOn()) {
            updateClientCycle(board, wasFrozen, stunned, slowThisTick);
            clampPosition(board);
            return;
        }

        // Targeting. A taunt overrides everything. Otherwise the lock is
        // trusted only while the locked target is within attack range; out of
        // range (still approaching, knocked back, pulled away) there is no
        // lock, and findTarget() re-scans every tick for whatever is closest in
        // sight. A stun breaks the lock outright.
        std::shared_ptr<Entity> target;
        if (forcedTargetTicksRemaining > 0) {
            for (const auto& e : board.getEntities()) {
                if (e->id == forcedTargetEntityId && e->isAlive() && e->isTargetable()) { target = e; break; }
            }
        }
        if (!target) {
            target = wasFrozen ? nullptr : resolveCurrentTarget(board);
            // A hit that has started keeps its target a little past reach
            // (LOAD_MODEL_PRELOAD; see loadModel).
            const float keep = (usesPreload() && target && windupTargetId == target->id && windupTicksLeft > 0)
                ? HIT_CANCEL_DISTANCE : 0.0f;
            if (target && position.distanceTo(target->position) > effectiveRangeTo(target) + keep) {
                target = nullptr;
            }
            if (!target) {
                target = findTarget(board);
            }
        }

        if (target) {
            if (wasFrozen) {
                // A stun resets unconditionally, grace period or not.
                currentTargetId = target->id;
                ticksOnTarget = 0;
                ticksSinceLastHit = 0;
            } else if (target->id != currentTargetId) {
                bool withinGrace = rampGracePeriodTicks > 0 && ticksSinceLastHit < rampGracePeriodTicks;
                currentTargetId = target->id;
                if (!withinGrace) ticksOnTarget = 0;
                // else keep the ramp stage across the switch; this tick neither
                // resets nor increments it.
            } else {
                ticksOnTarget++;
            }

            float dist = position.distanceTo(target->position);
            float effectiveAttackRange = effectiveRangeTo(target);

            const bool hitStarted = usesPreload() && windupTargetId == target->id && windupTicksLeft > 0;
            const int hookStepMs = stunned ? 0 : static_cast<int>(std::lround(static_cast<float>(TICK_MS) * slowThisTick));
            if (specialTargetId >= 0 && specialTargetId != target->id) { specialTargetId = -1; specialLoadLeftMs = 0; }
            if (specialTargetId == target->id && specialLoadLeftMs > 0) {
                hookStep(target, dist, effectiveAttackRange, hookStepMs);  // the hook is winding up: stand
            } else if (dist <= effectiveAttackRange || (hitStarted && dist <= effectiveAttackRange + HIT_CANCEL_DISTANCE)) {
                bool windingUp = false;
                if (usesPreload()) {
                    // LOAD_MODEL_PRELOAD: an attack starts once loaded (cooldown
                    // 0) and in reach, and lands preloadWindupTicks() later.
                    if (stunned) {
                        windupTargetId = -1;
                        windupTicksLeft = 0;
                    } else {
                        if (windupTargetId == target->id && windupTicksLeft > 0) {
                            // lands on the tick the windup runs out
                            windupTicksLeft--;
                            windingUp = windupTicksLeft > 0;
                        } else if (currentCooldown == 0.0f) {
                            // loaded and in reach: the attack starts now (a
                            // zero windup lands this tick)
                            windupTargetId = target->id;
                            windupTicksLeft = preloadWindupTicks();
                            windingUp = windupTicksLeft > 0;
                        } else {
                            windupTargetId = -1;
                            windingUp = true;  // still reloading
                        }
                    }
                } else if (loadTimesEnabled && loadTicks > 0) {
                    // LOAD_MODEL_WINDUP_PER_TARGET (and the lock-on cards): a
                    // unit in range of a target it has not wound up on waits
                    // loadTicks; a stun restarts it.
                    if (stunned) {
                        windupTargetId = -1;
                    } else if (windupTargetId != target->id) {
                        windupTargetId = target->id;
                        windupTicksLeft = loadTicks;
                    }
                    if (windupTargetId == target->id && windupTicksLeft > 0) {
                        windupTicksLeft--;
                        windingUp = true;
                    }
                }
                // Not while stunned: a stun holds the cooldown still, and a
                // unit that was already ready must not swing through it.
                if (!stunned && !windingUp && currentCooldown == 0.0f) {
                    performHit(board, target, dist);
                    // LOAD_MODEL_PRELOAD: reload, then the next windup.
                    currentCooldown = static_cast<float>(usesPreload() ? preloadReloadTicks() : attackCooldown);
                    // Little Prince's hit-speed ramp; ticksOnTarget already
                    // includes this hit.
                    if (hitSpeedRampFullTick > 0) currentCooldown *= hitSpeedRampFraction();
                    // Temporary ability haste (Cloaking Cape, Getaway Grenade).
                    if (temporaryInvisibilityTicksRemaining > 0) currentCooldown *= temporaryHitSpeedMultiplier;
                    // Blade Rage: haste the cooldown just set, so continued hits keep the haste alive.
                    if (selfHasteDurationTicks > 0) currentCooldown *= selfHasteCooldownMultiplier;
                }
            } else if (jumpStep(board, target, dist, effectiveAttackRange, stunned)) {
            } else if (hookStep(target, dist, effectiveAttackRange, hookStepMs)) {
            } else {
                if (usesPreload()) { windupTargetId = -1; windupTicksLeft = 0; }  // out of reach: no hit in progress
                Vector2D beforeMove = position;
                // Lane-aware approach: a King objective is walked to up this
                // unit's own lane; any other target is its own position
                // (LanePath::approachPoint).
                moveTowards(board, LanePath::approachPoint(board, team, position, target));
                if (chargeThreshold > 0.0f) chargeProgress += beforeMove.distanceTo(position);
            }
        } else if (wasFrozen || rampGracePeriodTicks <= 0 || ticksSinceLastHit >= rampGracePeriodTicks) {
            // No target: reset fully if frozen this tick, with no grace period
            // configured, or once it has run out.
            currentTargetId = -1;
            ticksOnTarget = 0;
            ticksSinceLastHit = 0;
        }
        // else: within the grace period with no target, hold the ramp stage for
        // a new target.

        clampPosition(board);
    }


    // One attack landing (both attack models): the damage (performAttack: at once, or a projectile), the counters
    // and every on-attack effect. The cooldown / cycle is the caller's.
    void performHit(Board& board, const std::shared_ptr<Entity>& target, float dist) {
        // Effects are applied by performAttack, since when they
        // fire depends on when the damage lands (at once for a
        // direct hit, on arrival for a projectile).
        lastAttackDistance = dist;
        ticksSinceAttack = 0;
        // Reset here, not on projectile arrival: the only
        // grace-period card (Inferno Dragon Evolution) attacks
        // instantly.
        ticksSinceLastHit = 0;
        if (burstEveryNAttacks > 0) {
            attacksSinceBurst++;
            currentHitIsBurst = (attacksSinceBurst >= burstEveryNAttacks);
            if (currentHitIsBurst) attacksSinceBurst = 0;
        } else {
            currentHitIsBurst = false;
        }
        if (maxSplitTargets <= 1) {
            currentHitCount = 1;
            performAttack(board, target);
        } else {
            auto targets = findSplitTargets(board, maxSplitTargets);
            currentHitCount = static_cast<int>(targets.size());
            for (const auto& t : targets) {
                performAttack(board, t);
            }
        }
        if (!attackSequenceDamage.empty()) {
            const int n = static_cast<int>(attackSequenceDamage.size());
            if (attackSequenceIndex == n - 1 && attackSequencePushback > 0.0f && target->isAlive()) {
                pushAway(*target, position, attackSequencePushback);
                target->clampPosition(board);
            }
            attackSequenceIndex = (attackSequenceIndex + 1) % n;
        }
        if (chargeThreshold > 0.0f) {
            if (chargeIsSticky && chargeProgress >= chargeThreshold) chargeHasStuck = true;
            if (!chargeHasStuck) chargeProgress = 0.0f;
        }
        if (startsInvisible) visibleTicksRemaining = revealTicksAfterAttack;
        // Blade Rage: refresh the window (the haste itself is on the cooldown / the cycle's step).
        if (selfHasteDurationTicks > 0) selfHasteTicksRemaining = selfHasteDurationTicks;
        // Ally auras (Rune Giant's buff, Battle Healer's heal).
        if (healAllyAmount > 0) {
            applyAreaHeal(board, position, auraRadius, id, team, healAllyAmount);
        }
        if (auraEveryNAttacks > 0) {
            attacksSinceAura++;
            if (attacksSinceAura >= auraEveryNAttacks) {
                attacksSinceAura = 0;
                applyAreaBuff(board, position, auraRadius, id, team,
                    auraBuffMultiplier, auraBuffDurationTicks, auraMaxTargets);
            }
        }
        if (recoilDistance > 0.0f) pushAway(*this, target->position, recoilDistance);
        // Evolved Bats: heals past starting hp, up to the absolute
        // healOnHitMaxHp; the second pulse follows in update().
        if (healOnHitAmount > 0 && hp < healOnHitMaxHp) {
            hp = std::min(hp + healOnHitAmount, healOnHitMaxHp);
        }
        if (healOnHitAmount > 0 && healOnHitSecondPulseDelayTicks > 0) {
            pendingHealPulseTicks = healOnHitSecondPulseDelayTicks;
        }
        // Evolved Skeletons: spawn on hit; the cap is inside
        // CappedSpawnOnHitEffect.
        if (onHitSpawnEffect) onHitSpawnEffect->apply(board, position, team);
        // Whirlwind pull (Evolved Valkyrie): the pull damage
        // reaches everyone in radius, including this attack's
        // target and buildings; only the physical pull exempts
        // buildings.
        if (onHitPullRadius > 0.0f) {
            if (onHitPullDamage > 0) {
                applySplashDamage(board, position, onHitPullRadius, -1, id, team, cardId, onHitPullDamage);
            }
            applyPullNearby(board, position, onHitPullRadius, onHitPullDistance, id, team);
        }
        // Fiery Flight's tornado, centred on the target and
        // additive: the primary target is not exempt.
        if (flightPulseTicksRemaining > 0 && flightPulseRadius > 0.0f) {
            applySplashDamage(board, target->position, flightPulseRadius, -1, id, team, cardId, flightPulseDamage);
            applyPullNearby(board, target->position, flightPulseRadius, flightPulsePullDistance, -1, team);
        }
        if (dieAfterFirstHit) hp = 0;
    }

    // Jump (Mega Knight): a ground target between jumpMinRange and jumpMaxRange is closed on: just inside attack
    // range, with a boosted, splashing hit at once. Returns whether it jumped.
    bool jumpStep(Board& board, const std::shared_ptr<Entity>& target, float dist, float reach, bool stunned) {
        if (stunned || jumpMaxRange <= 0.0f || dist < jumpMinRange || dist > jumpMaxRange || currentCooldown != 0.0f)
            return false;
        ticksSinceAttack = 0;
        pullToward(*this, target->position, dist - reach + 0.1f);
        int jumpDamage = static_cast<int>(getCurrentDamage() * jumpDamageMultiplier);
        target->takeDamage(jumpDamage);
        board.statsEvents.notifyDamageDealt(
            { id, team, cardId, target->id, target->cardId, target->team, jumpDamage, board.currentTick, target->isTower() });
        applySplashDamage(board, target->position, jumpSplashRadius, target->id, id, team, cardId, jumpDamage);
        currentCooldown = static_cast<float>(attackCooldown);
        return true;
    }

    // Hook (Fisherman): a target beyond attack reach but within hookRange is pulled to just inside melee range; the
    // damage lands on a later hit (a no-op on a building). Under Fidelity::specialLoadTime the hook first winds up
    // its SpecialLoadTime (1.3 s) on that target, by stepMs a tick (0 while stunned), the unit standing; once started
    // it runs wherever the target goes until the target changes (combat update 0x71fff599e4: special mode, +0x41 /
    // +0x44). Returns whether the hook acted this tick (winding up or firing).
    bool hookStep(const std::shared_ptr<Entity>& target, float dist, float reach, int stepMs) {
        const bool winding = specialTargetId == target->id && specialLoadLeftMs > 0;
        if (!winding) {
            if (stepMs <= 0 || hookRange <= 0.0f || dist <= reach || dist > hookRange || currentCooldown != 0.0f)
                return false;
            if (Fidelity::specialLoadTime && specialLoadMs > 0) {
                specialTargetId = target->id;
                specialLoadLeftMs = specialLoadMs;
            }
        }
        if (specialLoadLeftMs > 0) {
            specialLoadLeftMs -= stepMs;
            if (specialLoadLeftMs > 0) return true;  // still winding up
        }
        specialTargetId = -1;
        specialLoadLeftMs = 0;
        pullToward(*target, position, dist - reach + 0.1f);
        currentCooldown = static_cast<float>(attackCooldown);
        return true;
    }

    // ---- The client attack cycle (clientCycleOn; FidelitySwitches.h, bot-77 docs/client-code-notes.md s. 1) ----
    void resetSwing() {
        attacking = false;
        attackTimerMs = 0;
        swingStarted = false;
        finishWindowMs = 0;
    }

    // Whether the current target stays locked (no rescan): within reach + 0.025 (LOGIC_RANGE_EXTENSION_TO_KEEP_
    // TARGET), or + 0.5 for a projectile unit whose hit has started (LOGIC_PRESERVE_TARGET_IF_HIT_STARTED). Without
    // Fidelity::startedSwingFinishes: the preload model's rule, reach + 1.5 while the hit has started.
    bool clientKeepsTarget(const std::shared_ptr<Entity>& t) const {
        const float d = position.distanceTo(t->position), reach = effectiveRangeTo(t);
        if (!Fidelity::startedSwingFinishes) return d <= reach + (swingStarted ? HIT_CANCEL_DISTANCE : 0.0f);
        if (d <= reach + CLIENT_RANGE_EXTENSION_TO_KEEP_TARGET / 1000.0f) return true;
        return swingStarted && hasClientProjectile() && d <= reach + CLIENT_PRESERVE_TARGET_IF_HIT_STARTED_RANGE / 1000.0f;
    }

    // The attack timer's step this tick: TICK_MS x the hit-speed factor (a freeze's slow, 0 when stunned; the haste
    // of the Cloaking Cape / Getaway Grenade, Blade Rage, Little Prince's ramp, which the preload model applies to
    // its cooldown instead).
    int clientCycleStepMs(bool wasFrozen, bool stunned, float slowThisTick) const {
        if (stunned) return 0;
        float rate = wasFrozen ? slowThisTick : 1.0f;
        if (hitSpeedRampFullTick > 0) rate /= hitSpeedRampFraction();
        if (temporaryInvisibilityTicksRemaining > 0 && temporaryHitSpeedMultiplier > 0.0f) rate /= temporaryHitSpeedMultiplier;
        if (selfHasteTicksRemaining > 0 && selfHasteCooldownMultiplier > 0.0f) rate /= selfHasteCooldownMultiplier;
        return std::max(0, static_cast<int>(std::lround(static_cast<float>(TICK_MS) * rate)));
    }

    // One tick of targeting and attacking under the client's cycle (replaces the preload model's part of update()).
    void updateClientCycle(Board& board, bool wasFrozen, bool stunned, float slowThisTick) {
        const int hitSpeedMs = std::max(1, attackCooldown) * TICK_MS;
        const bool pauses = Fidelity::stunPausesSwing;
        // Sparky (LoadFirstHit, LOGIC_LOAD_FIRST_HIT_RESET_TIMER_WHEN_ZAPPED): each stunned tick empties her load.
        if (stunned && loadFirstHit) {
            resetSwing();
            loadMissingMs = std::max(0, clientLoadMs);
        }
        // A stun drops the target every tick (0x71fff11980) and the nearest is re-acquired. Without stunPausesSwing:
        // any freeze drops it, and a stun resets the swing (the preload model's full rewindup).
        const bool dropTarget = pauses ? stunned : wasFrozen;
        if (!pauses && stunned) resetSwing();

        std::shared_ptr<Entity> target;
        bool standInWindow = false;
        if (forcedTargetTicksRemaining > 0) {
            for (const auto& e : board.getEntities()) {
                if (e->id == forcedTargetEntityId && e->isAlive() && e->isTargetable()) { target = e; break; }
            }
        }
        const int prevTargetId = currentTargetId;
        if (!target) {
            std::shared_ptr<Entity> cur = dropTarget ? nullptr : resolveCurrentTarget(board);
            bool loseSwing = false;
            if (!dropTarget && !cur && prevTargetId >= 0 && attacking && finishWindowMs == 0) {
                // The target died or vanished mid-cycle (target removed 0x71fff591f8).
                if (Fidelity::attackFinishWindow) finishWindowMs = 1;
                else loseSwing = true;
            }
            if (cur && clientKeepsTarget(cur)) {
                target = cur;
            } else {
                auto cand = findTarget(board);
                const bool candInReach = cand && position.distanceTo(cand->position) <= effectiveRangeTo(cand);
                if (finishWindowMs > 0) {
                    if (candInReach) {
                        target = cand;  // keeps the cycle
                        finishWindowMs = 0;
                    } else {
                        finishWindowMs += TICK_MS;
                        if (finishWindowMs >= CLIENT_ATTACK_FINISH_TIME_MS) {
                            resetSwing();  // the swing is lost; the load keeps counting
                            target = cand;
                        } else {
                            standInWindow = true;  // waits for a target in reach
                        }
                    }
                } else {
                    if (loseSwing && !candInReach) resetSwing();
                    target = cand;
                }
            }
            // Set target (0x71fff558e0): a new target out of reach resets the swing, one in reach keeps it. While
            // stunned every tick's target is a new one.
            if (target && attacking && (dropTarget || target->id != prevTargetId)
                && position.distanceTo(target->position) > effectiveRangeTo(target)) {
                resetSwing();
            }
        }

        if (!target) {
            if (wasFrozen || rampGracePeriodTicks <= 0 || ticksSinceLastHit >= rampGracePeriodTicks) {
                currentTargetId = -1;
                ticksOnTarget = 0;
                ticksSinceLastHit = 0;
            }
            (void)standInWindow;
            return;
        }

        // Ramp bookkeeping, as in the preload model.
        if (wasFrozen) {
            currentTargetId = target->id;
            ticksOnTarget = 0;
            ticksSinceLastHit = 0;
        } else if (target->id != currentTargetId) {
            bool withinGrace = rampGracePeriodTicks > 0 && ticksSinceLastHit < rampGracePeriodTicks;
            currentTargetId = target->id;
            if (!withinGrace) ticksOnTarget = 0;
        } else {
            ticksOnTarget++;
        }

        const float dist = position.distanceTo(target->position);
        const float reach = effectiveRangeTo(target);
        const int stepMs = clientCycleStepMs(wasFrozen, stunned, slowThisTick);
        const bool advance = dist <= reach
            || (Fidelity::startedSwingFinishes ? swingStarted : (swingStarted && dist <= reach + HIT_CANCEL_DISTANCE));

        if (specialTargetId >= 0 && specialTargetId != target->id) { specialTargetId = -1; specialLoadLeftMs = 0; }
        if (specialTargetId == target->id && specialLoadLeftMs > 0) {
            hookStep(target, dist, reach, stepMs);  // the hook is winding up: stand
        } else if (advance) {
            // Timer advance (0x71fff5ac3c): nothing while the step is 0 (stunned: the swing pauses). An attack starts
            // LoadTime minus the missing load into its cycle, and the load is reset to full at that start.
            if (stepMs > 0) {
                // The client steps the new attack's timer in the update that starts it (50 ms), so its first hit
                // comes HitSpeed - LoadTime - 50 ms after that update. The engine keeps the preload model's on-time
                // (HitSpeed - LoadTime after the start tick), chosen when it ran 100 ms ticks (it could only land the
                // hit 100 ms early or on time) and supported by the 10-29 replay check of the Hog's first tower hit.
                // At 50 ms the client's form is expressible (step the timer in the start tick too); not changed with
                // the tick (2026-10-29), so the tick change alone moves no first hit.
                // A seeded hold (X-Bow's 3.5 s deploy delay, seedCooldown) or a jump / hook's cooldown delays the
                // start, as in the preload model. Not Sparky's spawn seed: her unloaded start is loadMissingMs here.
                const bool starting = !attacking;
                if (starting && currentCooldown > 0.0f && !loadFirstHit) {
                    // not yet
                } else if (starting) {
                    attacking = true;
                    attackTimerMs = std::max(0, clientLoadMs - loadMissingMs);
                    loadMissingMs = std::max(0, clientLoadMs);
                }
                const int before = attackTimerMs;
                if (!starting && attacking) attackTimerMs += stepMs;
                swingStarted = (attackTimerMs % hitSpeedMs) > CLIENT_HIT_STARTED_MS;
                if (attackTimerMs / hitSpeedMs > before / hitSpeedMs) {
                    // A hit; cancelled when the target is too far past reach (LOGIC_CANCEL_HIT_FROM_LONG_DISTANCE).
                    if (dist <= reach + CLIENT_CANCEL_HIT_DISTANCE / 1000.0f) performHit(board, target, dist);
                    if (loadFirstHit) {  // LOGIC_LOAD_FIRST_HIT_RESET_TIMER_AFTER_ATTACK
                        resetSwing();
                        loadMissingMs = std::max(0, clientLoadMs);
                    }
                }
            }
            // attacking: no movement
        } else if (jumpStep(board, target, dist, reach, stunned)) {
        } else if (hookStep(target, dist, reach, stepMs)) {
        } else {
            resetSwing();  // out of reach before the swing started: dropped, walk
            Vector2D beforeMove = position;
            moveTowards(board, LanePath::approachPoint(board, team, position, target));
            if (chargeThreshold > 0.0f) chargeProgress += beforeMove.distanceTo(position);
        }
    }

    float hitSpeedRampFraction() const {
        return (ticksOnTarget >= hitSpeedRampFullTick) ? hitSpeedRampFullFraction
            : (ticksOnTarget >= hitSpeedRampMidTick) ? hitSpeedRampMidFraction
            : 1.0f;
    }

    void onDeath(Board& board) override {
        if (deathEffect) deathEffect->apply(board, position, team);
    }

    // The radius an entity occupies for range math; a troop uses
    // Entity::IMPLICIT_TROOP_RADIUS. Public and static because AreaSpell's
    // rolling sweep needs it too.
    static float effectiveRadiusOf(const Entity& e) {
        return bodyRadiusOf(e);  // Entity.h: the client's per-unit radius under Fidelity::clientBodies
    }

protected:
    // Entity-typed: targeting needs no CombatEntity surface; applyOnHitEffects
    // narrows where needed.
    bool isValidTarget(const std::shared_ptr<Entity>& entity) const {
        return entity && entity->team != this->team && entity->isAlive() && entity->isTargetable()
            && entity->id != this->id && (!entity->isFlying || targetsAir)
            && (minAttackRange <= 0.0f || position.distanceTo(entity->position) >= minAttackRange);
    }

    float ownEffectiveRadius() const { return effectiveRadiusOf(*this); }

    static int shieldOf(const Entity& e) {
        const auto* ce = dynamic_cast<const CombatEntity*>(&e);
        return ce ? ce->shieldHp : 0;
    }

    // Where the next periodic spawn lands: on this entity, or periodicSideOffset
    // to one side, alternating left then right (Evolved Furnace). Left is -x for
    // team 0, which faces +y, and +x for team 1. Kept on the board.
    Vector2D periodicSpawnPoint(const Board& board) const {
        if (periodicSideOffset <= 0.0f) return position;
        const float facing = (team == 0) ? 1.0f : -1.0f;
        const float side = (periodicSpawnCount % 2 == 0) ? -1.0f : 1.0f; // left first
        Vector2D p = position;
        p.x += side * facing * periodicSideOffset;
        const float maxX = static_cast<float>(board.getWidth() - 1);
        p.x = std::max(0.0f, std::min(p.x, maxX));
        return p;
    }

    float effectiveRangeTo(const std::shared_ptr<Entity>& target) const {
        return attackRange + ownEffectiveRadius() + effectiveRadiusOf(*target);
    }

    // Sight to `target`; see effectiveSightWith.
    float effectiveSightTo(const std::shared_ptr<Entity>& target) const {
        return effectiveSightWith(ownEffectiveRadius(), *target);
    }

    // Sight is strict centre-to-centre: a Hog Rider's 9.5 means 9.5 tiles
    // centre to centre, as the published ranges do. Radii answer a hitbox
    // question, not a vision one.
    //
    // One floor: attack reach is surface-to-surface (effectiveRangeTo), so a
    // unit whose reach exceeds its sight could hit what it cannot acquire and
    // would stand idle. Sight is therefore floored at the unit's own attack
    // reach. The floor binds only where attackRange is close to sightRange
    // (towers, Musketeer-likes); for long-sight cards sightRange wins outright.
    // Without it, two Musketeers would stop out of each other's sight.
    //
    // Takes the attacker's radius as a parameter so findTarget computes it
    // once.
    // Calibration switch (default off = the strict rule above): sight measured
    // edge to edge, sightRange + both radii, as the engine did before
    // 2026-08-28. Process-wide; for fitting the engine to recorded matches.
public:
    static inline bool sightEdgeToEdge = false;
protected:

    // Troop-on-troop acquisition reaches TROOP_SIGHT_BONUS further than the
    // catalogued sight: measured on 363 kite events in 486 pro replays (bot-77
    // scripts/measure_kites.py, 2026-10-28), pulls start at a median 5.2 tiles
    // with a quarter beyond 6.3; one acquisition distance fits best at
    // sight + 1.75 (most melee 5.5 -> 7.25: 72.5%, held-back 83.7%) against
    // 60.6% / 76.7% at the bare 5.5. Only a troop seeing a troop: buildings,
    // towers and building targets keep the catalogued value (their pulls were
    // fitted separately, see Tower::getTargetingRadius). GAMEPLAY-AFFECTING.
    static constexpr float TROOP_SIGHT_BONUS = 1.75f;

    float effectiveSightWith(float myRadius, const Entity& target) const {
        if (Fidelity::clientTargeting) {
            // Find target 0x71fff56878: SightRange + own radius (ADD_CHARACTER_RANGE_TO_RADIUS) + the candidate's
            // radius, + EXTRA_SIGHT_RANGE_TO_CROWN_TOWERS to a crown tower; tested on centre distance. No troop bonus
            // (TROOP_SIGHT_BONUS was a fit to this) and no floor at attack reach.
            float edge = sightRange + myRadius + effectiveRadiusOf(target);
            if (Fidelity::crownTowerSightBonus && !isBuilding() && target.isTower())
                edge += CLIENT_EXTRA_SIGHT_RANGE_TO_CROWN_TOWERS / 1000.0f;
            return edge;
        }
        float sight = sightRange + ((!isBuilding() && !target.isBuilding()) ? TROOP_SIGHT_BONUS : 0.0f);
        // EXTRA_SIGHT_RANGE_TO_CROWN_TOWERS (globals.csv 2000): a troop sees a crown tower 2 tiles further.
        if (Fidelity::crownTowerSightBonus && !isBuilding() && target.isTower())
            sight += CLIENT_EXTRA_SIGHT_RANGE_TO_CROWN_TOWERS / 1000.0f;
        const float attackReach = attackRange + myRadius + effectiveRadiusOf(target);
        if (sightEdgeToEdge) {
            const float edge = sight + myRadius + effectiveRadiusOf(target);
            return attackReach > edge ? attackReach : edge;
        }
        // Only for a card whose raw sightRange covers its raw attackRange,
        // which the catalogue guarantees. Otherwise the unit stays blind past
        // its sight, as test_combat_entity.cpp pins.
        if (sightRange >= attackRange && attackReach > sight) return attackReach;
        return sight;
    }

    // Re-validates the locked target by id rather than re-scanning for the
    // closest. nullptr if there is no lock or the target is no longer legal.
    std::shared_ptr<Entity> resolveCurrentTarget(Board& board) const {
        if (currentTargetId < 0) return nullptr;
        for (const auto& entity : board.getEntities()) {
            if (entity->id == currentTargetId) {
                return isValidTarget(entity) ? entity : nullptr;
            }
        }
        return nullptr;
    }

    // The closest valid enemy in sight, towers competing on equal terms; with
    // nothing in sight, this unit's lane objective. With update()'s "lock only
    // within attack range" rule this is the real game's model: locked on once
    // fighting, re-evaluating while chasing, heading for its lane's tower when
    // blind.
    virtual std::shared_ptr<Entity> findTarget(Board& board) const {
        std::shared_ptr<Entity> closestInSight = nullptr;
        float minSightDistance = std::numeric_limits<float>::max();
        std::shared_ptr<Entity> closestTower = nullptr;
        float minTowerDistance = std::numeric_limits<float>::max();

        const float myRadius = ownEffectiveRadius();   // loop invariant
        const std::unordered_map<int, int> pending = pendingDamage(board);
        for (const auto& entity : board.getEntities()) {
            if (!isValidTarget(entity)) continue;
            if (!pending.empty() && !entity->isTower()) {
                const auto it = pending.find(entity->id);
                if (it != pending.end() && it->second >= entity->hp + shieldOf(*entity)) continue;
            }
            float dist = position.distanceTo(entity->position);
            if (entity->isTower() && dist < minTowerDistance) {
                minTowerDistance = dist;
                closestTower = entity;
            }
            // The tower tracking above serves only the fallback. Ranked by
            // footprint distance (Entity::getTargetingRadius); the sight gate
            // uses centre distance. Fidelity::clientTargeting: plain centre
            // distance for both.
            const float rank = targetRank(*entity, dist);
            if (rank <= effectiveSightWith(myRadius, *entity) && rank < minSightDistance) {
                minSightDistance = rank;
                closestInSight = entity;
            }
        }
        if (closestInSight) return closestInSight;

        // Blind: walk this unit's own lane objective, not the nearest tower
        // (LanePath.h). Falls back to the nearest tower when the lane objective
        // is not a legal target for this attacker (a Mortar's blind spot,
        // air/ground).
        auto laneTarget = LanePath::laneObjective(board, team, position);
        if (laneTarget && isValidTarget(laneTarget)) return laneTarget;
        return closestTower;
    }

    // Ranking distance of a candidate: centre distance under Fidelity::clientTargeting (find target 0x71fff56878:
    // squared centre distance after the priority class), else centre distance minus its fitted targeting radius
    // (Tower::getTargetingRadius 1.5 / 2.0, fitted to building pulls on 2026-10-28). The sight gate compares the same
    // number with effectiveSightWith.
    static float targetRank(const Entity& candidate, float centreDist) {
        return Fidelity::clientTargeting ? centreDist : centreDist - candidate.getTargetingRadius();
    }

    // The pending-damage skip (Fidelity::pendingDamageSkip): damage already in flight from this unit's team, per
    // target. A new target that it will kill is passed over; the current target is kept regardless
    // (CURRENT_TARGET_IGNORES_PENDING_DAMAGE), which update() does by not calling findTarget while locked in range.
    // Fidelity::pendingDamageClientRule: only an attacker with a projectile skips, and only damage landing within
    // CLIENT_PENDING_DAMAGE_WINDOW_MS (LOGIC_PENDING_DAMAGE_IGNORE_IF_DURATION_LESS) counts.
    std::unordered_map<int, int> pendingDamage(Board& board) const {
        std::unordered_map<int, int> pending;
        if (!Fidelity::pendingDamageSkip) return pending;
        if (Fidelity::pendingDamageClientRule && !hasClientProjectile()) return pending;
        for (const auto& e : board.getEntities()) {
            const int tid = e->incomingTargetId();
            if (tid < 0 || !e->isAlive() || e->team != team) continue;
            if (Fidelity::pendingDamageClientRule && e->incomingEtaMs() >= CLIENT_PENDING_DAMAGE_WINDOW_MS) continue;
            pending[tid] += e->incomingDamage();
        }
        return pending;
    }

    // For maxSplitTargets > 1 (Electro Wizard): the N closest valid targets.
    std::vector<std::shared_ptr<Entity>> findSplitTargets(Board& board, int maxCount) const {
        std::vector<std::shared_ptr<Entity>> candidates;
        for (const auto& entity : board.getEntities()) {
            if (isValidTarget(entity)) {
                candidates.push_back(entity);
            }
        }
        std::sort(candidates.begin(), candidates.end(),
            [this](const std::shared_ptr<Entity>& a, const std::shared_ptr<Entity>& b) {
                return position.distanceTo(a->position) < position.distanceTo(b->position);
            });
        if (static_cast<int>(candidates.size()) > maxCount) candidates.resize(maxCount);
        return candidates;
    }

// Read-only views of protected combat stats, for the observation encoder's
// attribute channels.
public:
    float getAttackRange() const { return attackRange; }
    int getAttackCooldown() const { return attackCooldown; }
    // The registry's damage, before the ramp, charge and buff state
    // getCurrentDamage applies (tools/audit/card_table.cpp).
    int getBaseDamage() const { return damage; }

    // Ticks engaged with the current target, for the deepCopy divergence tests
    // (perception/UPSTREAM_REQUESTS.md item 17). Its only other proxy,
    // getDamagePerTick(), collapses it into ramp buckets, so a within-bucket
    // desync would be invisible.
    int getTicksOnTarget() const { return ticksOnTarget; }

    // Damage per tick, the comparable quantity, including ramp, charge and buff
    // state.
    float getDamagePerTick() const {
        int cooldown = attackCooldown > 0 ? attackCooldown : 1;
        return static_cast<float>(getCurrentDamage()) / static_cast<float>(cooldown);
    }

protected:

    int getCurrentDamage() const {
        int base = attackSequenceDamage.empty() ? damage : attackSequenceDamage[attackSequenceIndex];
        if (rampFullTick > 0) {
            float fraction = (rampStage4Tick > 0 && ticksOnTarget >= rampStage4Tick) ? rampStage4Fraction
                : (ticksOnTarget >= rampFullTick) ? 1.0f
                : (ticksOnTarget >= rampMidTick) ? rampMidFraction
                : rampStartFraction;
            base = static_cast<int>(damage * fraction);
        }
        if (chargeThreshold > 0.0f && (chargeProgress >= chargeThreshold || chargeHasStuck)) {
            base = static_cast<int>(base * chargeMultiplier);
        }
        if (buffTicksRemaining > 0) {
            base = static_cast<int>(base * buffDamageMultiplier);
        }
        if (rangeFalloff && attackRange > 0.0f) {
            float distFraction = std::min(lastAttackDistance / attackRange, 1.0f);
            float rangeFactor = 1.0f - (1.0f - rangeFalloffMinFraction) * distFraction;
            base = static_cast<int>(base * rangeFactor);
        }
        if (rangeBandMaxDist > 0.0f && lastAttackDistance >= rangeBandMinDist && lastAttackDistance <= rangeBandMaxDist) {
            base = static_cast<int>(base * rangeBandDamageMultiplier);
        }
        if (currentHitIsBurst) {
            base = static_cast<int>(base * burstDamageMultiplier);
        }
        return (currentHitCount > 1 && !splitTargetsFullDamage) ? base / currentHitCount : base;
    }

    virtual void performAttack(Board& board, std::shared_ptr<Entity> target) = 0;

    // Called by direct-damage performAttack overrides as the damage lands;
    // ranged attacks hand onHitEffects to the Projectile instead. On-hit
    // effects apply only to a CombatEntity.
    void applyOnHitEffects(const std::shared_ptr<Entity>& target) const {
        if (onHitEffects.empty()) return;
        auto combatTarget = std::dynamic_pointer_cast<CombatEntity>(target);
        if (!combatTarget) return; // nothing to apply on-hit effects to
        for (const auto& effect : onHitEffects) {
            effect->apply(combatTarget);
        }
    }

    virtual void moveTowards(Board& board, const Vector2D& dest) {
        // Default: stationary.
        (void)board;
        (void)dest;
    }
};

// Splash: `dealt` to every other valid enemy within `radius` of `origin`, each
// with its own DamageDealtEvent. A free function because Projectile, not a
// CombatEntity, needs it too. No-op for radius <= 0.
inline void applySplashDamage(Board& board, const Vector2D& origin, float radius, int excludeId,
        int attackerId, int attackerTeam, int attackerCardId, int dealt, bool hitsAir) {
    if (radius <= 0.0f) return;
    for (const auto& entity : board.getEntities()) {
        if (entity->id == excludeId) continue; // already hit as the primary target
        if (entity->team == attackerTeam || !entity->isAlive() || !entity->isAreaHittable()) continue;
        if (!hitsAir && entity->isFlying) continue;
        if (origin.distanceTo(entity->position) > radius) continue;
        entity->takeDamage(dealt);
        board.statsEvents.notifyDamageDealt(
            { attackerId, attackerTeam, attackerCardId, entity->id, entity->cardId, entity->team, dealt, board.currentTick, entity->isTower() });
    }
}

// On-hit area pull (Evolved Valkyrie's Whirlwind Axe) toward `origin`.
// Buildings are never moved (pullToward). No-op for radius or distance <= 0.
inline void applyPullNearby(Board& board, const Vector2D& origin, float radius, float distance,
        int excludeId, int attackerTeam) {
    if (radius <= 0.0f || distance <= 0.0f) return;
    for (const auto& entity : board.getEntities()) {
        if (entity->id == excludeId) continue;
        if (entity->team == attackerTeam || !entity->isAlive() || !entity->isTargetable()) continue;
        if (origin.distanceTo(entity->position) > radius) continue;
        pullToward(*entity, origin, distance);
    }
}

// Taunt (Hero Knight): forces every valid enemy within `radius` onto
// `taunterId` for `ticks`. No-op for radius or ticks <= 0.
inline void applyTauntNearby(Board& board, const Vector2D& origin, float radius, int ticks,
        int taunterId, int taunterTeam) {
    if (radius <= 0.0f || ticks <= 0) return;
    for (const auto& entity : board.getEntities()) {
        if (entity->team == taunterTeam || !entity->isAlive() || !entity->isTargetable()) continue;
        if (origin.distanceTo(entity->position) > radius) continue;
        if (auto ce = std::dynamic_pointer_cast<CombatEntity>(entity)) {
            ce->forcedTargetEntityId = taunterId;
            ce->forcedTargetTicksRemaining = ticks;
        }
    }
}

// Piercing line (Bowler, Magic Archer): `dealt` to every valid enemy within
// `halfWidth` of the segment from `origin` (where the attacker fired) toward
// `aimPoint`, out to `range`. No-op for range or halfWidth <= 0.
inline void applyLineSplashDamage(Board& board, const Vector2D& origin, const Vector2D& aimPoint,
        float range, float halfWidth, int excludeId, int attackerId, int attackerTeam, int attackerCardId, int dealt) {
    if (range <= 0.0f || halfWidth <= 0.0f) return;
    float dx = aimPoint.x - origin.x;
    float dy = aimPoint.y - origin.y;
    float lineLen = std::sqrt(dx * dx + dy * dy);
    if (lineLen <= 0.01f) return; // no direction
    float ux = dx / lineLen, uy = dy / lineLen; // unit direction, origin -> aimPoint

    for (const auto& entity : board.getEntities()) {
        if (entity->id == excludeId) continue; // already hit as the primary target
        if (entity->team == attackerTeam || !entity->isAlive() || !entity->isAreaHittable()) continue;

        // Project onto the line, clamped to [0, range], then take the
        // perpendicular distance.
        float proj = (entity->position.x - origin.x) * ux + (entity->position.y - origin.y) * uy;
        if (proj < 0.0f) proj = 0.0f;
        if (proj > range) proj = range;
        float closestX = origin.x + ux * proj;
        float closestY = origin.y + uy * proj;
        float ddx = entity->position.x - closestX;
        float ddy = entity->position.y - closestY;
        if (std::sqrt(ddx * ddx + ddy * ddy) > halfWidth) continue;

        entity->takeDamage(dealt);
        board.statsEvents.notifyDamageDealt(
            { attackerId, attackerTeam, attackerCardId, entity->id, entity->cardId, entity->team, dealt, board.currentTick, entity->isTower() });
    }
}

// Damage buff on up to maxTargets of the closest same-team entities within
// radius (Rune Giant's enchant, Lumberjack's potion, Rage), excluding the
// source. No-op for radius or maxTargets <= 0.
inline std::vector<std::shared_ptr<CombatEntity>> applyAreaBuff(Board& board, const Vector2D& origin, float radius, int excludeId,
        int team, float multiplier, int durationTicks, int maxTargets) {
    if (radius <= 0.0f || maxTargets <= 0) return {};
    std::vector<std::shared_ptr<CombatEntity>> candidates;
    for (const auto& entity : board.getEntities()) {
        if (entity->id == excludeId) continue;
        if (entity->team != team || !entity->isAlive()) continue;
        if (origin.distanceTo(entity->position) > radius) continue;
        if (auto combatEntity = std::dynamic_pointer_cast<CombatEntity>(entity)) {
            candidates.push_back(combatEntity);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
        [&origin](const std::shared_ptr<CombatEntity>& a, const std::shared_ptr<CombatEntity>& b) {
            return origin.distanceTo(a->position) < origin.distanceTo(b->position);
        });
    if (static_cast<int>(candidates.size()) > maxTargets) candidates.resize(maxTargets);
    for (const auto& c : candidates) c->applyBuff(multiplier, durationTicks);
    return candidates;
}

// Heal every same-team troop within radius by `amount` (Battle Healer),
// excluding the source. Buildings and towers are not healed (the client's
// friendly_troop_no_buildings filter), and neither is a healer
// (healAllyAmount > 0): a Battle Healer ignores Battle Healer heals (its
// IgnoreBuff), its own and another's. There is no generic max hp to cap
// against, so it can overheal. No-op for radius or amount <= 0.
inline void applyAreaHeal(Board& board, const Vector2D& origin, float radius, int excludeId,
        int team, int amount) {
    if (radius <= 0.0f || amount <= 0) return;
    for (const auto& entity : board.getEntities()) {
        if (entity->id == excludeId) continue;
        if (entity->team != team || !entity->isAlive() || entity->isBuilding()) continue;
        if (auto ce = dynamic_cast<const CombatEntity*>(entity.get()); ce && ce->healAllyAmount > 0) continue;
        if (origin.distanceTo(entity->position) > radius) continue;
        entity->hp += amount;
    }
}
