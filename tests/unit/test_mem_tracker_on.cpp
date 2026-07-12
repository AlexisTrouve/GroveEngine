// ============================================================================
// MemTrackerOnUnit — the OTHER half of the B2 build gate: the macro when tracking is ON.
//
// QUOI  : compile this TU with GROVE_MEM_TRACKING forced on (a local #define before the include) and
//         confirm GROVE_MEM_TRACK_ALLOC / _FREE actually route to the global tracker.
//
// POURQUOI: MemTrackerUnit proves the OFF path (no-op); this proves the ON path in the SAME normal
//         build — no separate build config needed, the flip is done per-TU by the #define. Under a
//         sanitizer the macro stays a no-op BY DESIGN (ASan owns allocation tracking), so the
//         assertions are selected accordingly.
// ============================================================================

#define GROVE_MEM_TRACKING 1   // force the instrumentation ON for this translation unit

#include <catch2/catch_test_macros.hpp>

#include <grove/mem/Tracker.h>

TEST_CASE("GROVE_MEM_TRACK_ALLOC records when tracking is ON", "[mem][tracker][on]") {
    grove::mem::tracker().reset();

    int x, y;
    GROVE_MEM_TRACK_ALLOC(&x, 32, "on:test");
    GROVE_MEM_TRACK_ALLOC(&y, 64, "on:test");

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    // Under a sanitizer the macro is intentionally still a no-op (don't fight ASan's own tracking).
    REQUIRE(grove::mem::tracker().stats().totalAllocs == 0);
#else
    REQUIRE(grove::mem::tracker().stats().totalAllocs == 2);
    REQUIRE(grove::mem::tracker().stats().liveBytes == 96);
    GROVE_MEM_TRACK_FREE(&x);
    REQUIRE(grove::mem::tracker().stats().liveCount == 1);
    REQUIRE(grove::mem::tracker().report()["grove_mem"]["byTag"]["on:test"]["count"] == 1);
#endif

    grove::mem::tracker().reset();   // leave the process-wide tracker clean for any other TU
}
