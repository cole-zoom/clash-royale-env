#pragma once

// The engine's time base. One engine tick is TICK_MS of game time: 50 ms since 2026-10-29, the game client's logic
// tick (bot-77 docs/client-code-notes.md section 2); it was 100 ms before.
//
// The Python-facing API keeps the 100 ms step: one GameManager::step() / one ClashEnv skip frame is STEP_MS of game
// time, run as TICKS_PER_STEP engine ticks. Every tick count inside the engine (CardStats, entities, effects, the
// GameManager clock) is in engine ticks; every tick count the bindings expose or accept (the match clock, max_ticks,
// deploy_ticks, cooldown_ticks, the elixir-phase ticks, get_card_stats, GameLogger's replay) is in steps, converted
// in ClashEnv.h / bindings.cpp / GameLogger.h. Durations are written in seconds or milliseconds and converted here.
inline constexpr int TICK_MS = 50;
inline constexpr int STEP_MS = 100;
inline constexpr int TICKS_PER_STEP = STEP_MS / TICK_MS;
static_assert(STEP_MS % TICK_MS == 0, "a step is a whole number of ticks");

// A duration in milliseconds as whole engine ticks, rounded to the nearest (halves up): msToTicks(1150) = 23.
constexpr int msToTicks(int ms) { return (ms + TICK_MS / 2) / TICK_MS; }

// A duration in seconds as whole engine ticks, rounded to the nearest: secs(2.5) = 50 at 50 ms.
constexpr int secs(double s) { return static_cast<int>(s * 1000.0 / TICK_MS + 0.5); }

// Between engine ticks and the API's 100 ms steps. ticksToSteps rounds down; ticksToStepsCeil up (a deploy still
// running never reads as done).
constexpr int stepsToTicks(int steps) { return steps * TICKS_PER_STEP; }
constexpr int ticksToSteps(int ticks) { return ticks / TICKS_PER_STEP; }
constexpr int ticksToStepsCeil(int ticks) { return (ticks + TICKS_PER_STEP - 1) / TICKS_PER_STEP; }

// A rate given per second (a speed in tiles a second, ...) as the rate per engine tick: perTick(15.0f) = 0.75 tiles
// a tick at 50 ms.
constexpr float perTick(float perSecond) { return perSecond * static_cast<float>(TICK_MS) / 1000.0f; }
