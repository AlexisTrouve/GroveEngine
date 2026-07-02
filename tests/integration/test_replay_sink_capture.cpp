// ============================================================================
// test_replay_sink_capture.cpp — E2E for the structured replay sink (IO contract §8, part 3).
//
// WHAT: locks that the IntraIOManager, when its replay sink is enabled, CAPTURES the stamped
//   control-plane stream through the real publish -> routeMessage path — one ReplayEvent (envelope
//   + topic) per routed message — and that the two §8 query views work end to end:
//     - bySource(id)  -> the per-module view
//     - timeline()    -> the canonical (tick, lamport) order
//   plus: off by default (opt-in, no capture when disabled) and bounded (drop-oldest) under the real route.
//
// WHY: "no E2E = it doesn't exist". The pure ring is unit-locked (ReplaySinkUnit); this proves the TAP
//   is wired — that routeMessage actually feeds the sink with the completed envelope (source/seq/lamport
//   /tick/simTime), so the replay log reflects real traffic, not a hand-fed struct.
//
// HOW: raw IntraIO instances through the IntraIOManager singleton (publish routes immediately for the
//   high-freq control plane, so the sink records at publish time, before any pull). The sink is a singleton
//   member, so each case enables (which clears) at start and disables at end to stay independent.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>
#include <grove/ReplaySink.h>

#include <cstdint>
#include <memory>
#include <string>

using namespace grove;
using Catch::Matchers::WithinAbs;

namespace {
std::unique_ptr<IDataNode> emptyNode() {
    return std::make_unique<JsonDataNode>("d", nlohmann::json::object());
}
} // namespace

TEST_CASE("ReplaySink E2E - captures the routed stream with the completed envelope", "[replay][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    mgr.enableReplaySink(64);                       // fresh capture session (clears)
    mgr.setSimTime(/*tick=*/1234, /*simTime=*/1234.0 / 60.0);

    auto a = mgr.createInstance("RA");
    auto b = mgr.createInstance("RB");
    b->subscribe("r:topic", [](const Message&) {}); // a subscriber so the message actually routes

    a->publish("r:topic", emptyNode());
    a->publish("r:topic", emptyNode());

    // Recorded at route (publish) time — no pull needed for the sink to see it.
    const auto ev = mgr.replaySink().snapshot();
    REQUIRE(ev.size() == 2);
    REQUIRE(ev[0].topic == "r:topic");
    REQUIRE(ev[0].env.source == "RA");             // envelope completed by the transport
    REQUIRE(ev[0].env.seq == 1u);
    REQUIRE(ev[1].env.seq == 2u);
    REQUIRE(ev[0].env.lamport < ev[1].env.lamport);
    REQUIRE(ev[0].env.tick == 1234u);              // stamped from the engine snapshot
    REQUIRE_THAT(ev[0].env.simTime, WithinAbs(1234.0 / 60.0, 1e-9));

    mgr.removeInstance("RA");
    mgr.removeInstance("RB");
    mgr.disableReplaySink();
}

TEST_CASE("ReplaySink E2E - the two views: per-source filter + canonical timeline", "[replay][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    mgr.enableReplaySink(64);
    mgr.setSimTime(10, 10.0 / 60.0);

    auto a = mgr.createInstance("SA");
    auto b = mgr.createInstance("SB");
    // Cross-subscribe so both publishers' messages route (and B's reply is lamport-after A's).
    a->subscribe("s:fromB", [](const Message&) {});
    b->subscribe("s:fromA", [&](const Message&) { b->publish("s:fromB", emptyNode()); });

    a->publish("s:fromA", emptyNode());
    while (b->hasMessages() > 0) b->pullAndDispatch();   // B receives -> publishes s:fromB
    while (a->hasMessages() > 0) a->pullAndDispatch();

    // Per-module view.
    const auto fromA = mgr.replaySink().bySource("SA");
    const auto fromB = mgr.replaySink().bySource("SB");
    REQUIRE(fromA.size() == 1);
    REQUIRE(fromA[0].topic == "s:fromA");
    REQUIRE(fromB.size() == 1);
    REQUIRE(fromB[0].topic == "s:fromB");

    // Canonical timeline: A's publish causally precedes B's reply (lamport receive rule), so it sorts first.
    const auto tl = mgr.replaySink().timeline();
    REQUIRE(tl.size() == 2);
    REQUIRE(tl[0].env.source == "SA");
    REQUIRE(tl[1].env.source == "SB");
    REQUIRE(tl[0].env.lamport < tl[1].env.lamport);

    mgr.removeInstance("SA");
    mgr.removeInstance("SB");
    mgr.disableReplaySink();
}

TEST_CASE("ReplaySink E2E - disabled = opt-in: no capture when off", "[replay][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    mgr.disableReplaySink();                        // ensure OFF
    const uint64_t before = mgr.replaySink().capturedCount();

    auto a = mgr.createInstance("OA");
    auto b = mgr.createInstance("OB");
    b->subscribe("o:topic", [](const Message&) {});
    a->publish("o:topic", emptyNode());
    a->publish("o:topic", emptyNode());

    REQUIRE(mgr.replaySink().capturedCount() == before);  // routing happened, but the sink recorded nothing

    mgr.removeInstance("OA");
    mgr.removeInstance("OB");
}

TEST_CASE("ReplaySink E2E - bounded ring drops oldest under the real route", "[replay][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    mgr.enableReplaySink(2);                        // tiny ring
    mgr.setSimTime(0, 0.0);

    auto a = mgr.createInstance("BA");
    auto b = mgr.createInstance("BB");
    b->subscribe("b:topic", [](const Message&) {});
    for (int i = 0; i < 4; ++i) a->publish("b:topic", emptyNode());

    REQUIRE(mgr.replaySink().size() == 2);
    REQUIRE(mgr.replaySink().capturedCount() == 4);
    REQUIRE(mgr.replaySink().droppedCount() == 2);
    const auto ev = mgr.replaySink().snapshot();
    REQUIRE(ev.front().env.seq == 3u);             // the two oldest (seq 1,2) were overwritten
    REQUIRE(ev.back().env.seq == 4u);

    mgr.removeInstance("BA");
    mgr.removeInstance("BB");
    mgr.disableReplaySink();
}
