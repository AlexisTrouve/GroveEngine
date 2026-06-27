// ============================================================================
// test_engine_clock.cpp — objective unit test for grove::EngineClock (pure, headless).
//
// QUOI   : verrouille l'horloge autoritaire de l'engine (grove/EngineClock.h) — la
//   conversion d'un flux de deltas réels (wall-clock) en un temps de simulation à PAS
//   FIXE : ticks, simTime = tick*dt, realTime brut, et le contrôle pause / slow-mo /
//   fast-forward via timeScale.
// POURQUOI : le contrat IO (docs/design/iio-contract.md) exige un temps de sim
//   DÉTERMINISTE + PAUSABLE pendant que l'exécution reste async. Le pas fixe le donne :
//   simTime reproductible et sans dérive flottante, scalable/pausable sans qu'un module
//   le sache. Sans ce test, "fixed timestep + pause/slowmo" n'est qu'une affirmation.
// COMMENT : oracles purs (pattern accumulateur de Gaffer). Marges choisies LARGES (0.1+ dt)
//   pour ne dépendre d'aucune égalité flottante fragile ; les seuls cas à somme exacte
//   utilisent des multiplicateurs exacts en binaire (0.5, 2.0).
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/EngineClock.h>

using namespace grove;
using Catch::Matchers::WithinAbs;

// The canonical 60 Hz step used across the cases. 1/60 is not exactly representable,
// which is exactly why the tests below never rely on (k * DT) landing on a tick boundary
// unless k is a binary-exact factor (0.5, 2.0).
static constexpr double DT = 1.0 / 60.0;

// ============================================================================
// Construction / initial state
// ============================================================================

TEST_CASE("EngineClock - starts at tick 0 with all times zero and scale 1", "[engineclock][unit]") {
    EngineClock clock;  // default dt = 1/60
    REQUIRE(clock.tick() == 0u);
    REQUIRE_THAT(clock.simTime(),  WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(clock.realTime(), WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(clock.dt(),       WithinAbs(DT, 1e-12));
    REQUIRE_THAT(clock.timeScale(), WithinAbs(1.0, 1e-12));
    REQUIRE_FALSE(clock.paused());
}

// ============================================================================
// The accumulator: how real time turns into fixed sim steps
// ============================================================================

TEST_CASE("EngineClock - a real delta of exactly dt produces exactly one tick", "[engineclock][unit]") {
    EngineClock clock(DT);
    REQUIRE(clock.advance(DT) == 1);
    REQUIRE(clock.tick() == 1u);
    REQUIRE_THAT(clock.simTime(),  WithinAbs(DT, 1e-12));
    REQUIRE_THAT(clock.realTime(), WithinAbs(DT, 1e-12));
}

TEST_CASE("EngineClock - sub-dt deltas accumulate; the tick fires when the sum crosses dt", "[engineclock][unit]") {
    EngineClock clock(DT);
    REQUIRE(clock.advance(DT * 0.4) == 0);   // 0.4 dt banked
    REQUIRE(clock.advance(DT * 0.4) == 0);   // 0.8 dt banked
    REQUIRE(clock.tick() == 0u);
    REQUIRE(clock.advance(DT * 0.4) == 1);   // 1.2 dt -> 1 step, 0.2 dt carried
    REQUIRE(clock.tick() == 1u);
    // The carried 0.2 dt means less than a full dt is needed for the next tick.
    REQUIRE(clock.advance(DT * 0.7) == 0);   // 0.2 + 0.7 = 0.9 dt, still short
    REQUIRE(clock.advance(DT * 0.2) == 1);   // 0.9 + 0.2 = 1.1 dt -> tick 2
    REQUIRE(clock.tick() == 2u);
}

TEST_CASE("EngineClock - one large delta emits multiple fixed steps and carries the remainder", "[engineclock][unit]") {
    EngineClock clock(DT);
    REQUIRE(clock.advance(DT * 2.5) == 2);   // 2.5 dt -> 2 steps, 0.5 carried
    REQUIRE(clock.tick() == 2u);
    REQUIRE_THAT(clock.simTime(), WithinAbs(2.0 * DT, 1e-12));
    // The carried half-step plus a generous 0.6 dt completes a third tick.
    REQUIRE(clock.advance(DT * 0.6) == 1);   // 0.5 + 0.6 = 1.1 dt -> tick 3
    REQUIRE(clock.tick() == 3u);
}

// ============================================================================
// realTime vs simTime — the two never conflate
// ============================================================================

TEST_CASE("EngineClock - pause freezes sim time and ticks but realTime keeps flowing", "[engineclock][unit]") {
    EngineClock clock(DT);
    clock.advance(DT * 2.0);                 // tick 2
    REQUIRE(clock.tick() == 2u);

    clock.pause();
    REQUIRE(clock.paused());
    REQUIRE_THAT(clock.timeScale(), WithinAbs(0.0, 1e-12));

    REQUIRE(clock.advance(DT * 5.0) == 0);   // five real frames while paused
    REQUIRE(clock.tick() == 2u);                                  // sim frozen
    REQUIRE_THAT(clock.simTime(),  WithinAbs(2.0 * DT, 1e-12));
    REQUIRE_THAT(clock.realTime(), WithinAbs(7.0 * DT, 1e-9));    // 2 + 5 real frames
}

TEST_CASE("EngineClock - slow motion (scale 0.5) needs twice the real time per tick", "[engineclock][unit]") {
    EngineClock clock(DT);
    clock.setTimeScale(0.5);                 // 0.5 is binary-exact -> 0.5dt + 0.5dt == dt
    REQUIRE(clock.advance(DT) == 0);         // one real dt buys half a sim step
    REQUIRE(clock.tick() == 0u);
    REQUIRE(clock.advance(DT) == 1);         // a second real dt completes the step
    REQUIRE(clock.tick() == 1u);
    REQUIRE_THAT(clock.realTime(), WithinAbs(2.0 * DT, 1e-9));    // two real frames
    REQUIRE_THAT(clock.simTime(),  WithinAbs(DT, 1e-12));         // one sim step
}

TEST_CASE("EngineClock - fast forward (scale 2) emits two ticks per real dt", "[engineclock][unit]") {
    EngineClock clock(DT);
    clock.setTimeScale(2.0);                 // 2.0 is binary-exact -> 1 real dt = 2 sim dt
    REQUIRE(clock.advance(DT) == 2);
    REQUIRE(clock.tick() == 2u);
    REQUIRE_THAT(clock.realTime(), WithinAbs(DT, 1e-9));
    REQUIRE_THAT(clock.simTime(),  WithinAbs(2.0 * DT, 1e-12));
}

// ============================================================================
// Time control: pause/resume memory, scale clamping
// ============================================================================

TEST_CASE("EngineClock - resume restores the scale that was active before pause", "[engineclock][unit]") {
    EngineClock clock(DT);
    clock.setTimeScale(0.5);
    clock.pause();
    REQUIRE_THAT(clock.timeScale(), WithinAbs(0.0, 1e-12));
    clock.resume();
    REQUIRE_THAT(clock.timeScale(), WithinAbs(0.5, 1e-12));       // slow-mo restored, not reset to 1
    REQUIRE_FALSE(clock.paused());
}

TEST_CASE("EngineClock - setTimeScale clamps negatives to zero (no reverse time)", "[engineclock][unit]") {
    EngineClock clock(DT);
    clock.setTimeScale(-3.0);
    REQUIRE_THAT(clock.timeScale(), WithinAbs(0.0, 1e-12));
    REQUIRE(clock.advance(DT * 5.0) == 0);                        // behaves as paused
}

// ============================================================================
// Robustness: spiral-of-death guard, negative deltas
// ============================================================================

TEST_CASE("EngineClock - a huge frame is clamped to maxStepsPerFrame and accrues no debt", "[engineclock][unit]") {
    EngineClock clock(DT, /*maxStepsPerFrame=*/4);
    // 100 dt of real time in one frame: the sim must neither run 100 steps nor bank the
    // unrun time as a debt that makes later frames sprint to catch up.
    REQUIRE(clock.advance(DT * 100.0) == 4);                      // clamped to the cap
    REQUIRE(clock.tick() == 4u);
    REQUIRE(clock.advance(DT) == 1);                              // next frame: exactly one tick, no sprint
    REQUIRE(clock.tick() == 5u);
}

TEST_CASE("EngineClock - a negative real delta is clamped to zero (time never runs backwards)", "[engineclock][unit]") {
    EngineClock clock(DT);
    clock.advance(DT * 1.5);
    const uint64_t tickBefore = clock.tick();
    const double   realBefore = clock.realTime();
    REQUIRE(clock.advance(-DT * 10.0) == 0);
    REQUIRE(clock.tick() == tickBefore);
    REQUIRE_THAT(clock.realTime(), WithinAbs(realBefore, 1e-12));  // realTime did not regress
}

// ============================================================================
// Invariants & snapshot
// ============================================================================

TEST_CASE("EngineClock - simTime equals tick*dt exactly and sample() is a consistent snapshot", "[engineclock][unit]") {
    EngineClock clock(DT);
    clock.advance(DT * 0.9);
    clock.advance(DT * 0.9);
    clock.setTimeScale(2.0);
    clock.advance(DT * 1.1);
    clock.setTimeScale(1.0);
    clock.advance(DT * 0.5);

    const ClockSample s = clock.sample();
    REQUIRE(s.tick == clock.tick());
    REQUIRE_THAT(s.simTime,   WithinAbs(static_cast<double>(s.tick) * DT, 1e-12));  // derived, no drift
    REQUIRE_THAT(s.simTime,   WithinAbs(clock.simTime(), 1e-12));
    REQUIRE_THAT(s.dt,        WithinAbs(DT, 1e-12));
    REQUIRE_THAT(s.realTime,  WithinAbs(clock.realTime(), 1e-12));
    REQUIRE_THAT(s.timeScale, WithinAbs(clock.timeScale(), 1e-12));
}

TEST_CASE("EngineClock - reset returns to the initial state and behaves like new after", "[engineclock][unit]") {
    EngineClock clock(DT);
    clock.setTimeScale(0.25);
    clock.advance(DT * 10.0);
    clock.reset();
    REQUIRE(clock.tick() == 0u);
    REQUIRE_THAT(clock.simTime(),   WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(clock.realTime(),  WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(clock.timeScale(), WithinAbs(1.0, 1e-12));
    REQUIRE_FALSE(clock.paused());
    REQUIRE(clock.advance(DT) == 1);
    REQUIRE(clock.tick() == 1u);
}
