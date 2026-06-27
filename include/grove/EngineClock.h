#pragma once

// ============================================================================
// grove::EngineClock — the engine's single authoritative time source (header-only, pure).
//
// WHAT : Turns a stream of real (wall-clock) frame deltas into a FIXED-TIMESTEP
//   simulation clock. Each call to advance(realDelta) banks the (scaled) real time and
//   emits 0..N fixed `dt` sim steps, exposing one snapshot of time —
//   (tick, simTime, dt, realTime, timeScale) — that is the ONLY time a module reads.
//
// WHY  : The IO contract (docs/design/iio-contract.md) says sim time must be
//   DETERMINISTIC + PAUSABLE while execution stays async. A fixed timestep delivers that:
//   simTime = tick * dt is reproducible and free of accumulated float drift, and a single
//   timeScale lets the whole sim pause / slow-mo / fast-forward without any module knowing.
//   Three clocks are kept strictly separate and never conflated:
//     - tick / simTime : the SIM plane — pausable, scalable, deterministic.
//     - realTime       : wall clock — never paused/scaled; for profiling, timeouts, staleness.
//     - (Lamport lives elsewhere — causal order of messages, not time; see the envelope.)
//   Today IEngine::step(float dt) exists but dt never reaches modules; this class is the
//   missing provider the engine samples once per tick and hands to modules (Part 1c).
//
// HOW  : The classic accumulator pattern (Gaffer, "Fix Your Timestep"):
//     accumulator += realDelta * timeScale;
//     while (accumulator >= dt && steps < maxStepsPerFrame) { accumulator -= dt; ++tick; }
//   simTime is DERIVED as tick * dt (never summed) so it cannot drift. A spiral-of-death
//   guard caps steps per frame and DROPS the unrun remainder, so a long hitch makes sim
//   time slip rather than the sim sprinting to catch up for many frames afterward. realTime
//   is the raw, unscaled sum of deltas. Pure: no OS clock, no I/O, no allocation, no
//   threading — fully headless and oracle-testable (see tests/unit/test_engine_clock.cpp).
// ============================================================================

#include <cstdint>

namespace grove {

// An immutable snapshot of engine time, as a module sees it. Trivially copyable.
struct ClockSample {
    uint64_t tick      = 0;     // monotonic fixed-step counter; 0 at startup
    double   simTime   = 0.0;   // simulated seconds = tick * dt (pausable / scalable)
    double   dt        = 0.0;   // the fixed timestep in seconds (constant for a clock)
    double   realTime  = 0.0;   // wall-clock seconds since start (never paused / scaled)
    double   timeScale = 1.0;   // 1 = normal, 0 = paused, 0.5 = slow-mo, 2 = fast-forward
};

class EngineClock {
public:
    // fixedDt: the simulation step in seconds (e.g. 1/60). maxStepsPerFrame: the
    // spiral-of-death cap — the most sim steps one advance() call may emit. Both are
    // sanitized (a non-positive value falls back to a safe default) so a bad config
    // can't wedge the clock into an infinite or zero-progress loop.
    explicit EngineClock(double fixedDt = 1.0 / 60.0, int maxStepsPerFrame = 8)
        : m_dt(fixedDt > 0.0 ? fixedDt : 1.0 / 60.0),
          m_maxSteps(maxStepsPerFrame > 0 ? maxStepsPerFrame : 1) {}

    // Feed one real frame delta (wall seconds). Banks the scaled time, emits as many fixed
    // steps as fit (clamped to maxStepsPerFrame), and advances realTime by the raw delta.
    // Returns the number of sim steps taken this call (0..maxStepsPerFrame).
    int advance(double realDelta) {
        // Guard against clock skew / a non-monotonic source handing us a negative delta:
        // engine time never runs backwards.
        if (realDelta < 0.0) realDelta = 0.0;

        // Wall clock: raw, independent of pause/scale — profiling and timeouts must keep
        // ticking even when the simulation is frozen.
        m_realTime += realDelta;

        // Sim budget: scaled by timeScale (0 while paused => nothing banked => no steps).
        m_accumulator += realDelta * m_timeScale;

        int steps = 0;
        while (m_accumulator >= m_dt && steps < m_maxSteps) {
            m_accumulator -= m_dt;
            ++m_tick;
            ++steps;
        }

        // Spiral-of-death guard: if we hit the cap with time still owed, drop it. We must
        // not bank a debt we can never repay — a long hitch would otherwise make the sim
        // sprint for many frames. simTime simply slips; the world stays roughly real-time.
        if (steps == m_maxSteps && m_accumulator >= m_dt) {
            m_accumulator = 0.0;
        }
        return steps;
    }

    // --- Time control (SIM plane only; realTime is never affected) ---------------

    // Set the sim speed multiplier. Negatives are clamped to 0 (no reverse time).
    void   setTimeScale(double scale) { m_timeScale = scale > 0.0 ? scale : 0.0; }
    double timeScale() const { return m_timeScale; }

    // Pause / resume. pause() remembers the current (non-zero) scale so resume() restores
    // it — pausing a slow-mo and resuming keeps the slow-mo rather than snapping back to 1.
    void pause() {
        if (m_timeScale > 0.0) m_savedScale = m_timeScale;
        m_timeScale = 0.0;
    }
    void resume() { m_timeScale = m_savedScale > 0.0 ? m_savedScale : 1.0; }
    bool paused() const { return m_timeScale <= 0.0; }

    // --- Current state -----------------------------------------------------------

    uint64_t tick()     const { return m_tick; }
    double   simTime()  const { return static_cast<double>(m_tick) * m_dt; }  // derived: no drift
    double   dt()       const { return m_dt; }
    double   realTime() const { return m_realTime; }

    // One consistent snapshot — the value handed to modules each tick.
    ClockSample sample() const {
        return ClockSample{ m_tick, simTime(), m_dt, m_realTime, m_timeScale };
    }

    // Back to the initial runtime state (tick 0, times 0, scale 1). dt / maxSteps are kept
    // — they are configuration, not state.
    void reset() {
        m_tick        = 0;
        m_realTime    = 0.0;
        m_accumulator = 0.0;
        m_timeScale   = 1.0;
        m_savedScale  = 1.0;
    }

private:
    double m_dt;          // fixed timestep in seconds (set once; never mutated after ctor)
    int    m_maxSteps;    // spiral-of-death cap (set once; never mutated after ctor)

    uint64_t m_tick        = 0;
    double   m_realTime    = 0.0;
    double   m_accumulator = 0.0;   // unspent (scaled) sim time; < dt after advance() unless capped
    double   m_timeScale   = 1.0;
    double   m_savedScale  = 1.0;   // scale to restore on resume()
};

} // namespace grove
