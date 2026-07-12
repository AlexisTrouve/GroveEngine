// ============================================================================
// ProfileZoneUnit — locks the scoped profiler + its GROVE_DEBUG strip (B3).
//
// QUOI  : the Profiler accumulator (add/zone/report/reset) is config-independent; the
//         GROVE_PROFILE_ZONE macro is config-aware — it times a scope in a debug build and compiles
//         to nothing in a shipping build (GROVE_DEBUG=OFF).
//
// POURQUOI: proves both that the measurement is correct AND that a shipped binary pays zero cost.
//         Same two-config discipline as BuildConfigUnit / DebugGateE2E (compiled under both).
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/profile/ProfileZone.h>

using grove::profile::profiler;

TEST_CASE("Profiler: accumulates seconds + count per zone", "[profile]") {
    profiler().reset();
    profiler().add("a", 0.5);
    profiler().add("a", 0.25);
    profiler().add("b", 1.0);

    REQUIRE(profiler().zone("a").count == 2);
    REQUIRE(profiler().zone("a").totalSeconds == 0.75);   // 0.5 + 0.25, exact in binary
    REQUIRE(profiler().zone("b").count == 1);
    REQUIRE(profiler().zone("missing").count == 0);        // never seen -> clean default

    const auto rep = profiler().report();
    REQUIRE(rep["grove_profile"]["a"]["count"] == 2);
    REQUIRE(rep["grove_profile"]["b"]["seconds"] == 1.0);

    profiler().reset();
    REQUIRE(profiler().snapshot().empty());
}

TEST_CASE("GROVE_PROFILE_ZONE times a scope (debug) / is stripped (shipping)", "[profile][config]") {
    profiler().reset();
    {
        GROVE_PROFILE_ZONE("work");
        // A bit of un-elidable work so the timed span is real.
        volatile double acc = 0.0;
        for (int i = 0; i < 200000; ++i) acc += i * 0.5;
        (void)acc;
    }
    const auto z = profiler().zone("work");
#if GROVE_DEBUG
    REQUIRE(z.count == 1);                 // the zone fired and recorded
    REQUIRE(z.totalSeconds >= 0.0);        // timed (a real, non-negative span)
#else
    REQUIRE(z.count == 0);                 // macro compiled out — nothing recorded
#endif
    profiler().reset();
}

TEST_CASE("GROVE_PROFILE_ZONE nests (debug)", "[profile][config]") {
    profiler().reset();
    {
        GROVE_PROFILE_ZONE("outer");
        {
            GROVE_PROFILE_ZONE("inner");
            volatile double acc = 0.0;
            for (int i = 0; i < 100000; ++i) acc += i;
            (void)acc;
        }
        volatile double acc = 0.0;
        for (int i = 0; i < 100000; ++i) acc += i;
        (void)acc;
    }
#if GROVE_DEBUG
    // Both zones recorded; the outer scope contains the inner so it took at least as long.
    REQUIRE(profiler().zone("outer").count == 1);
    REQUIRE(profiler().zone("inner").count == 1);
    REQUIRE(profiler().zone("outer").totalSeconds >= profiler().zone("inner").totalSeconds);
#else
    REQUIRE(profiler().zone("outer").count == 0);
    REQUIRE(profiler().zone("inner").count == 0);
#endif
    profiler().reset();
}
