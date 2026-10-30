#pragma once

// Process-wide switches for the client-mechanics fixes of 2026-10-29 (bot-77 sim fidelity, P2). Each is on by
// default unless noted; switching one off restores the engine's earlier behaviour, so a fix can be measured alone and
// compared against lab battles (bindings: set_<name> / get_<name> in src/bindings.cpp). Values come from the client's
// logic tables (ClientMechanics.h, LoadTimes.h, generated) and globals.csv; the rules from the client's code
// (bot-77 docs/client-code-notes.md, addresses of the 2026-10-29 build).
struct Fidelity {
    // Pending-damage skip (CURRENT_TARGET_IGNORES_PENDING_DAMAGE, KeepTargetWithPendingDamage): a unit choosing a new
    // target skips one that damage already in flight will kill; it keeps shooting its current target regardless.
    // Was `targetQueueing` (binding set_target_queueing, kept as an alias): the client's USE_TARGET_QUEUEING is a
    // queue of forced-target requests, not this rule (find target 0x71fff56878, queued requests 0x71fff55e80).
    static inline bool pendingDamageSkip = true;
    // The client's form of that skip: only an attacker with a projectile skips, and only for damage landing within
    // CLIENT_PENDING_DAMAGE_WINDOW_MS (600, LOGIC_PENDING_DAMAGE_IGNORE_IF_DURATION_LESS). Off: every attacker skips,
    // whenever the damage lands (the 41fe8ad rule).
    static inline bool pendingDamageClientRule = true;
    // ALLOW_AOE_ATTACKS_WITHOUT_TARGET (TRUE): an area shot (splash, piercing line, Firecracker's burst) whose
    // target dies in flight still lands where the target was.
    static inline bool aoeLandsWithoutTarget = true;
    // EXTRA_SIGHT_RANGE_TO_CROWN_TOWERS (2000): troops see crown towers 2 tiles further than their SightRange.
    static inline bool crownTowerSightBonus = true;
    // Each unit's projectile Speed and Homing from the client (was 1.5 tiles per 100 ms for troops, 2.0 for towers,
    // every shot homing; buildings hit at once). Non-homing area shots land where the target stood at launch.
    static inline bool clientProjectiles = true;
    // Each unit's CollisionRadius and Mass from the client (was 0.4 for every troop, 1.0 for every building, 1.5 /
    // 2.0 for Princess / King; equal pushes): attack reach, body spacing, who pushes whom.
    static inline bool clientBodies = true;
    // The King shoots CLIENT_KING_ACTIVATION_MS (3.3 s) after its trigger (damage or a Princess Tower lost), not at
    // once (king_tower.toml: WaitForKingTowerActivation -> ActionDuration).
    static inline bool kingActivationDelay = true;

    // ---- The attack cycle as the client's code runs it (combat update 0x71fff599e4, timer advance 0x71fff5ac3c) ----
    // Two timers: an attack timer that runs while attacking (a hit each time it crosses a multiple of HitSpeed), and
    // the missing load, which counts down every tick whatever the unit does. An attack starts LoadTime minus the
    // missing load into its cycle and resets the load to full at that start (not at the hit). Applies to units with a
    // client load entry (LoadTimes.h loadMsForCard >= 0) under CombatEntity's preload model; off = that preload model
    // (reload counted from the hit).
    static inline bool reloadFromAttackStart = true;
    // A stun (hit-speed factor 0) pauses the swing and resumes it, while the unit drops its target and re-acquires the
    // nearest each tick (0x71fff11980; a re-acquired target out of reach resets the swing); a pure slow keeps its
    // target. Sparky (LoadFirstHit) is reset to unloaded. Off: a stun (or slow) drops the target and resets the swing.
    // Needs reloadFromAttackStart.
    static inline bool stunPausesSwing = true;
    // ATTACK_FINISH_TIME_MS (250): after the target dies, a new target in reach within the window keeps the cycle;
    // otherwise the swing is lost (the load keeps counting). Off: the swing is lost at once unless a target is in
    // reach the same tick. Needs reloadFromAttackStart.
    static inline bool attackFinishWindow = true;
    // A swing that has started (attack timer more than 50 ms into its cycle) runs to its hit wherever the target goes
    // (pushed, knocked back, walking off); the hit is cancelled if the target is more than 1.5 tiles past reach. An
    // unstarted swing whose target leaves reach (+0.025, or +0.5 for a projectile unit whose hit has started) is
    // dropped. Off: a started swing is kept only while the target is within reach + 1.5. Needs reloadFromAttackStart.
    static inline bool startedSwingFinishes = true;
    // SpecialLoadTime: a special attack winds up this long before it fires (Fisherman's hook 1.3 s; the engine's hook
    // fired at once).
    static inline bool specialLoadTime = true;
    // Targeting as find target 0x71fff56878 does it: sight edge to edge (SightRange + own radius + the candidate's
    // radius, + 2 tiles to crown towers), no troop-on-troop bonus and no floor at attack reach; the nearest by centre
    // distance wins. Off: the fitted rule (centre-to-centre sight + TROOP_SIGHT_BONUS 1.75 between troops, floored
    // at reach; candidates ranked by centre distance minus the tower's fitted targeting radius 1.5 / 2.0).
    static inline bool clientTargeting = true;
    // The King's first shot comes King LoadTime 500 ms after it acquires a target (HitSpeed 1000 - 500, spawned
    // loaded), then every HitSpeed, through the client attack cycle. Off: it fires on acquiring.
    static inline bool kingFirstShotDelay = true;
    // The Princess Tower's first arrow comes HitSpeed (800 ms) after it acquires from idle (no LoadTime). OFF by
    // default: Cole's "fires right away" against the code; lab L18 decides.
    static inline bool princessFirstShotDelay = false;
    // Attack reach to a crown tower is measured to its tile footprint (Princess 3x3: 1.5, King 4x4: 2.0, the same
    // radii as targeting) instead of its CollisionRadius (1.0 / 1.4). Lab night 1: a Giant (L04) and a Hog (L18) hit
    // the tower 1.0 s and 0.2 s before the engine's, at matching positions, which the 0.5-tile difference explains
    // at their speeds. With it the Hog's six tower hits land at the replay's times to the 50 ms frame, the Giant's first
    // 0.4 s late (was 1.0), and the lab's sim - real death times average -0.01 s (was -0.10, mean |d| 0.24 vs 0.33).
    // Held-back S2: positions and winner unchanged; the sim's princess-tower damage +8% (already above the replays',
    // where card and tower levels differ from the engine's).
    static inline bool towerReachFootprint = true;
    // Elixir at the client's rate (battle_timelines Default: 28 s / 14 s / 9.3 s a bar, i.e. 2.8 s an elixir at 1x)
    // instead of 0.035 per 100 ms (2.857 s). OFF by default: perception's opponent-elixir tracker assumes the engine's
    // rate; for Cole to decide.
    static inline bool clientElixirRate = false;

    // ---- Unit collision as the client's code runs it (separation 0x71fff6058c, move step 0x71fff60bd4; notes §5) ----
    // Each troop pushes only itself, in entity order, before its own step, and the next troop sees where it went.
    // Per neighbour on its layer: overlap clamped to 0.3, times the neighbour's Mass / its own (capped 0.299), + 0.001;
    // averaged over contacts and capped at 0.15 a 50 ms tick (one engine tick, Tick.h), applied together with the
    // troop's own step (Board::clientMoveStep, clientSelfPush). Units deploying, dashing, winding up a special, or jumping the river (JumpEnabled
    // troops over river cells, at JumpSpeed) neither push nor are pushed. A push may carry a centre into the first
    // river cell, where moves clamp per cell and a 0.256 nudge a tick takes it back to the bank (Board::clientRiverMove).
    // Buildings keep the engine's static push-out. OFF by default: off = Board::resolveCollisions (pairwise split
    // m2 / (m1 + m2) after every unit has moved, uncapped, with a sideways nudge; the bank snap).
    static inline bool clientCollision = false;

    // ---- Pathing around obstacles (the client routes over tiles: PATHFINDING_* in globals.csv, BUILDING_COST 50 to
    // a tile's 8, so it walks around towers rather than into them; the engine walks straight and is pushed out) ----
    // A troop already within a bridge's width walks across on its own line (its x) instead of to the bridge's centre
    // point; two Giants (body 0.75) fit side by side on the 2-tile bridge. Off: every crossing aims at the centre, so two units meeting on
    // a bridge are pulled back onto one line and push each other to a stop (Cole's play page, 2026-10-30: two Giants
    // and Mini P.E.K.K.As stuck on a bridge, "normally they get passed eachother with some difficulty").
    static inline bool bridgeOwnLine = true;
    // A troop walking into a building or tower, or an enemy ground body, that isn't where it's going slides along it
    // at its full speed (the tangent on the side it was already heading; its own right when dead centre), so a Giant
    // gets past an enemy Giant it ignores. Off: the static push-out leaves only the sideways part of each
    // step, so a Giant dropped behind its King Tower crawls along it for ~5 s (Cole's play page, 2026-10-30).
    static inline bool obstacleSlide = true;
    // The pocket: with an enemy Princess Tower down, troops and buildings may be placed on that lane's half of their
    // side, from the river to GameManager::POCKET_DEPTH (4) tiles past it (Cole, 2026-10-30). Off: own half only.
    static inline bool pocketDeploy = true;
};
