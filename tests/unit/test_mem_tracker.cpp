// ============================================================================
// MemTrackerUnit — locks grove::mem::Tracker leak detection + the OFF macro strip (B2).
//
// QUOI  : drive the Tracker directly (alloc/free/leak report/stats/reset) and confirm the opt-in
//         macro GROVE_MEM_TRACK_ALLOC is a NO-OP in a default (tracking-OFF) build.
//
// POURQUOI: the tracker is the "leak layer ready before you ship"; a silent bug in its accounting
//         (wrong live bytes, a leak not surfacing by tag) would make a real leak hunt lie. The
//         OFF-macro check proves the zero-cost promise (nothing recorded unless explicitly enabled).
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/mem/Tracker.h>

using grove::mem::Tracker;

TEST_CASE("Tracker: surfaces live (leaked) allocations grouped by tag", "[mem][tracker]") {
    Tracker t;
    int a, b, c;   // three distinct addresses to track
    t.onAlloc(&a, 64,  "iio:msg");
    t.onAlloc(&b, 128, "iio:msg");
    t.onAlloc(&c, 256, "sprite:batch");
    t.onFree(&b);   // free one — the other two are "leaks" if this were shutdown

    const auto s = t.stats();
    REQUIRE(s.totalAllocs == 3);
    REQUIRE(s.totalFrees == 1);
    REQUIRE(s.liveCount == 2);            // a + c
    REQUIRE(s.liveBytes == 64 + 256);     // b's 128 was freed

    const auto byTag = t.liveBytesByTag();
    REQUIRE(byTag.at("iio:msg") == 64);        // only a remains under this tag
    REQUIRE(byTag.at("sprite:batch") == 256);

    const auto rep = t.report();
    REQUIRE(rep["grove_mem"]["liveCount"] == 2);
    REQUIRE(rep["grove_mem"]["byTag"]["sprite:batch"]["bytes"] == 256);
    REQUIRE(rep["grove_mem"]["byTag"]["iio:msg"]["count"] == 1);
}

TEST_CASE("Tracker: matched frees leave no leak", "[mem][tracker]") {
    Tracker t;
    int a, b;
    t.onAlloc(&a, 10, "x");
    t.onAlloc(&b, 20, "x");
    t.onFree(&a);
    t.onFree(&b);
    REQUIRE(t.stats().liveCount == 0);
    REQUIRE(t.stats().liveBytes == 0);
    REQUIRE(t.report()["grove_mem"]["byTag"].empty());
}

TEST_CASE("Tracker: fail-soft on null / untracked, and reset clears", "[mem][tracker]") {
    Tracker t;
    t.onAlloc(nullptr, 8, "x");   // null → ignored
    int z;
    t.onFree(&z);                 // untracked free → ignored (no underflow)
    REQUIRE(t.stats().liveCount == 0);
    REQUIRE(t.stats().liveBytes == 0);

    int a;
    t.onAlloc(&a, 8, "x");
    REQUIRE(t.stats().liveCount == 1);
    t.reset();
    REQUIRE(t.stats().liveCount == 0);
    REQUIRE(t.stats().totalAllocs == 0);
}

TEST_CASE("GROVE_MEM_TRACK_ALLOC is a no-op when tracking is OFF (default)", "[mem][tracker][strip]") {
    // Default build: GROVE_MEM_TRACKING undefined → the macro must not touch the global tracker.
    grove::mem::tracker().reset();
    int x;
    GROVE_MEM_TRACK_ALLOC(&x, 999, "should-not-record");
    GROVE_MEM_TRACK_FREE(&x);
    REQUIRE(grove::mem::tracker().stats().totalAllocs == 0);
    REQUIRE(grove::mem::tracker().stats().liveCount == 0);
}
