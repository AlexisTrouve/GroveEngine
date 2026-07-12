// ============================================================================
// EngineProfileE2E — DebugEngine::step() is instrumented with profiler zones (FO1).
//
// QUOI  : drive the real engine and read the global profiler — step() records "engine:step"
//         (whole frame) + "engine:iopump" (the IIO pump phase) once per step in a debug build, and
//         nothing in a shipping build (GROVE_DEBUG=OFF, the zone macro compiled out).
//
// POURQUOI: proves the profiler is wired into the engine's real per-frame phases (so you get a
//         frame-time breakdown out of the box, not just a standalone timer) AND that a shipping
//         build pays zero cost. The profiler() singleton is an inline function → the SAME instance
//         in grove_impl (where step() records) and this TU (where we read), under static linking.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/DebugEngine.h>
#include <grove/profile/ProfileZone.h>

#include <spdlog/spdlog.h>

TEST_CASE("DebugEngine::step() records profiler zones (debug) / stripped (shipping)",
          "[profile][engine][config]") {
    spdlog::drop("DebugEngine");                 // order-independent logger registration
    grove::profile::profiler().reset();

    grove::DebugEngine engine;
    engine.initialize();
    engine.step(0.016f);
    engine.step(0.016f);

    const auto step = grove::profile::profiler().zone("engine:step");
    const auto pump = grove::profile::profiler().zone("engine:iopump");

#if GROVE_DEBUG
    REQUIRE(step.count == 2);                    // one "engine:step" zone per step()
    REQUIRE(pump.count == 2);                    // pumpModuleIO runs every step
    REQUIRE(step.totalSeconds >= 0.0);
    // The step zone encloses the pump sub-phase, so it took at least as long.
    REQUIRE(step.totalSeconds >= pump.totalSeconds);
#else
    REQUIRE(step.count == 0);                    // zones compiled out in a shipping build
    REQUIRE(pump.count == 0);
#endif

    grove::profile::profiler().reset();          // leave the process-wide profiler clean
}
