/**
 * Unit Tests: grove::ReplaySink — the structured replay log (IO contract §8, part 3).
 *
 * WHAT  : Locks the pure sink brick — opt-in recording, the drop-oldest bounded ring, and the two §8 query
 *         views (per-source filter + the merge-sorted canonical timeline). No engine/IIO routing here: the
 *         sink is a pure consumer of stamped Envelopes, so it tests headless.
 *
 * WHY    : This is the immediate payoff of the whole IO contract — record the stamped stream, replay it
 *         sorted, see the order of a non-deterministic heisenbug. The sink must (a) cost nothing when off,
 *         (b) never grow unbounded (drop-oldest, keep the most recent window), (c) reconstruct a deterministic
 *         order from out-of-order arrivals, and (d) be thread-safe (many workers route concurrently).
 *
 * HOW    : Catch2. Build Envelopes by hand, record them, assert the ring semantics + the query orderings. A
 *         concurrency case hammers record() from N threads and checks the counts are exact (no lost/double).
 */

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "grove/ReplaySink.h"

using namespace grove;

// Build a stamped envelope compactly.
static Envelope env(const std::string& source, uint64_t seq, uint64_t lamport, uint64_t tick, double simTime = 0.0) {
    Envelope e;
    e.source = source; e.seq = seq; e.lamport = lamport; e.tick = tick; e.simTime = simTime;
    return e;
}

TEST_CASE("ReplaySink - disabled by default: record is a no-op", "[replay][unit]") {
    ReplaySink sink;
    REQUIRE_FALSE(sink.enabled());
    sink.record(env("a", 1, 1, 0), "topic:x");   // dropped on the floor (opt-in)
    REQUIRE(sink.size() == 0);
    REQUIRE(sink.capturedCount() == 0);
    REQUIRE(sink.snapshot().empty());
}

TEST_CASE("ReplaySink - records stamped events in insertion order under capacity", "[replay][unit]") {
    ReplaySink sink;
    sink.enable(8);
    REQUIRE(sink.enabled());
    sink.record(env("a", 1, 5, 10), "t:a");
    sink.record(env("b", 1, 6, 10), "t:b");
    sink.record(env("a", 2, 7, 11), "t:c");

    const auto s = sink.snapshot();
    REQUIRE(s.size() == 3);
    REQUIRE(sink.capturedCount() == 3);
    REQUIRE(sink.droppedCount() == 0);
    // Insertion order + envelope integrity.
    REQUIRE(s[0].topic == "t:a");
    REQUIRE(s[0].env.source == "a");
    REQUIRE(s[0].env.lamport == 5);
    REQUIRE(s[2].topic == "t:c");
    REQUIRE(s[2].env.tick == 11);
}

TEST_CASE("ReplaySink - a full ring drops the OLDEST (sliding window of the last N)", "[replay][unit]") {
    ReplaySink sink;
    sink.enable(4);
    for (uint64_t i = 1; i <= 6; ++i) sink.record(env("a", i, i, 0), "t:" + std::to_string(i));

    REQUIRE(sink.size() == 4);
    REQUIRE(sink.capturedCount() == 6);
    REQUIRE(sink.droppedCount() == 2);            // the two oldest (seq 1,2) were overwritten
    const auto s = sink.snapshot();
    REQUIRE(s.size() == 4);
    REQUIRE(s.front().env.seq == 3);              // oldest survivor
    REQUIRE(s.back().env.seq == 6);               // newest
    REQUIRE(s.front().topic == "t:3");
}

TEST_CASE("ReplaySink - bySource gives the per-module view", "[replay][unit]") {
    ReplaySink sink;
    sink.enable(16);
    sink.record(env("input", 1, 1, 0), "input:mouse");
    sink.record(env("ui", 1, 2, 0), "ui:click");
    sink.record(env("input", 2, 3, 0), "input:key");

    const auto in = sink.bySource("input");
    REQUIRE(in.size() == 2);
    REQUIRE(in[0].topic == "input:mouse");
    REQUIRE(in[1].topic == "input:key");
    REQUIRE(sink.bySource("ui").size() == 1);
    REQUIRE(sink.bySource("nobody").empty());
}

TEST_CASE("ReplaySink - timeline merge-sorts into the canonical (tick, lamport) order", "[replay][unit]") {
    ReplaySink sink;
    sink.enable(16);
    // Recorded OUT of causal order (as a non-deterministic engine would deliver them).
    sink.record(env("b", 1, 9, 5), "t:late-lamport");
    sink.record(env("a", 1, 2, 5), "t:early-lamport-same-tick");
    sink.record(env("a", 2, 1, 4), "t:earlier-tick");
    sink.record(env("c", 1, 2, 5), "t:tie-broken-by-source");  // same (tick,lamport) as a's — source 'c' > 'a'

    const auto tl = sink.timeline();
    REQUIRE(tl.size() == 4);
    // tick 4 first.
    REQUIRE(tl[0].env.tick == 4);
    REQUIRE(tl[0].topic == "t:earlier-tick");
    // then tick 5: lamport 2 (source a) before lamport 2 (source c) before lamport 9 (source b).
    REQUIRE(tl[1].env.source == "a");
    REQUIRE(tl[1].env.lamport == 2);
    REQUIRE(tl[2].env.source == "c");             // (tick,lamport) tie broken by source
    REQUIRE(tl[2].env.lamport == 2);
    REQUIRE(tl[3].env.lamport == 9);
    // Snapshot (insertion order) is UNCHANGED — timeline sorts a copy.
    REQUIRE(sink.snapshot()[0].topic == "t:late-lamport");
}

TEST_CASE("ReplaySink - disable stops recording; enable clears for a fresh session", "[replay][unit]") {
    ReplaySink sink;
    sink.enable(8);
    sink.record(env("a", 1, 1, 0), "t:1");
    sink.disable();
    REQUIRE_FALSE(sink.enabled());
    sink.record(env("a", 2, 2, 0), "t:2");        // ignored while disabled
    REQUIRE(sink.size() == 1);                     // contents preserved for post-mortem
    REQUIRE(sink.snapshot()[0].topic == "t:1");

    sink.enable(8);                                // fresh session -> cleared
    REQUIRE(sink.size() == 0);
    REQUIRE(sink.capturedCount() == 0);
}

TEST_CASE("ReplaySink - concurrent record() is thread-safe (exact counts, no loss/double)", "[replay][unit]") {
    ReplaySink sink;
    constexpr int kThreads = 8;
    constexpr int kPer = 2000;
    sink.enable(1024);  // smaller than the total -> exercises overflow under contention

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&sink, t] {
            for (int i = 0; i < kPer; ++i) {
                sink.record(env("src" + std::to_string(t), static_cast<uint64_t>(i), static_cast<uint64_t>(i), 0), "t:concurrent");
            }
        });
    }
    for (auto& th : threads) th.join();

    // Every offered message is accounted for exactly once: kept (size) + dropped == captured == total.
    const uint64_t total = static_cast<uint64_t>(kThreads) * kPer;
    REQUIRE(sink.capturedCount() == total);
    REQUIRE(sink.size() + sink.droppedCount() == total);
    REQUIRE(sink.size() == sink.capacity());  // far more offered than capacity -> ring stays full
}
