// ============================================================================
// test_backpressure_policy.cpp — per-topic backpressure policy (IO contract §9).
//
// WHAT: locks the three inbox policies on the HIGH-freq queue, exercised through the real
//   publish -> route -> deliver path:
//     - DropOldest (default): a full inbox drops the OLDEST first (unchanged historical behavior).
//     - Coalesce (latest-wins): a flooding state topic never piles up — only the freshest survives.
//     - Reject (protect critical): a queued critical command is never collaterally dropped when another
//       topic floods; on a genuine all-Reject overflow the NEWEST is rejected at the door (loud + counted).
//
// WHY: async + resilience forces bounded queues, but ONE global drop rule is wrong (§9). This proves the
//   per-topic policy is honored on the real inbox (the subscriber's queue filled by routeMessage), not just
//   that the enum exists — "no E2E = it doesn't exist".
//
// HOW: a producer + a consumer via the IntraIOManager singleton. The consumer sets its topic policies +
//   a small maxQueueSize, the producer floods WITHOUT the consumer pulling in between, then the consumer
//   pulls and a handler captures the surviving payloads' `v` field. Assert which messages survived.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

namespace {
std::unique_ptr<IDataNode> nodeV(int v) {
    return std::make_unique<JsonDataNode>("d", nlohmann::json{{"v", v}});
}
} // namespace

TEST_CASE("Backpressure - DropOldest (default) keeps the newest, drops the oldest", "[backpressure][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    auto prod = mgr.createInstance("BPprod");
    auto cons = mgr.createInstance("BPcons");
    cons->setMaxQueueSize(3);

    std::vector<int> pulled;
    cons->subscribe("bp:.*", [&](const Message& m) { pulled.push_back(static_cast<int>(m.data->getInt("v", -1))); });

    for (int v = 1; v <= 5; ++v) prod->publish("bp:x", nodeV(v));   // 5 into a cap-3 inbox, no pull between
    REQUIRE(cons->hasMessages() == 3);
    while (cons->hasMessages() > 0) cons->pullAndDispatch();

    REQUIRE(pulled == std::vector<int>{3, 4, 5});                    // oldest (1,2) dropped
    REQUIRE(cons->getHealth().droppedMessageCount == 2);
    REQUIRE(cons->getCoalescedCount() == 0);
    REQUIRE(cons->getRejectedCount() == 0);

    mgr.removeInstance("BPprod");
    mgr.removeInstance("BPcons");
}

TEST_CASE("Backpressure - Coalesce keeps only the freshest of a flooding topic", "[backpressure][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    auto prod = mgr.createInstance("BPCprod");
    auto cons = mgr.createInstance("BPCcons");
    cons->setTopicPolicy("bp:cam", BackpressurePolicy::Coalesce);

    std::vector<int> pulled;
    cons->subscribe("bp:.*", [&](const Message& m) { pulled.push_back(static_cast<int>(m.data->getInt("v", -1))); });

    for (int v = 1; v <= 5; ++v) prod->publish("bp:cam", nodeV(v));  // a state topic flooding, no pull between
    REQUIRE(cons->hasMessages() == 1);                               // never piled up
    REQUIRE(cons->getCoalescedCount() == 4);                         // 4 superseded (by design, not pressure)
    REQUIRE(cons->getHealth().droppedMessageCount == 0);            // NOT a pressure drop

    cons->pullAndDispatch();
    REQUIRE(pulled == std::vector<int>{5});                          // the freshest value

    // Per-topic: a NON-coalesce topic in the same inbox still accumulates independently.
    for (int v = 10; v <= 12; ++v) prod->publish("bp:other", nodeV(v));
    for (int v = 100; v <= 101; ++v) prod->publish("bp:cam", nodeV(v));   // still coalesces -> 1
    REQUIRE(cons->hasMessages() == 4);                              // 3 others + 1 coalesced cam

    mgr.removeInstance("BPCprod");
    mgr.removeInstance("BPCcons");
}

TEST_CASE("Backpressure - Reject protects a queued critical when another topic floods", "[backpressure][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    auto prod = mgr.createInstance("BPRprod");
    auto cons = mgr.createInstance("BPRcons");
    cons->setMaxQueueSize(3);
    cons->setTopicPolicy("bp:crit", BackpressurePolicy::Reject);

    std::vector<int> pulled;
    cons->subscribe("bp:.*", [&](const Message& m) { pulled.push_back(static_cast<int>(m.data->getInt("v", -1))); });

    prod->publish("bp:crit", nodeV(0));                              // one critical command, queued first
    for (int v = 1; v <= 10; ++v) prod->publish("bp:flood", nodeV(v));  // a flood tries to evict it

    REQUIRE(cons->hasMessages() == 3);
    while (cons->hasMessages() > 0) cons->pullAndDispatch();

    // The critical (v=0) is NEVER collaterally dropped — it survives at the front; only floods were evicted.
    REQUIRE(pulled.front() == 0);
    REQUIRE(std::find(pulled.begin(), pulled.end(), 0) != pulled.end());
    REQUIRE(cons->getRejectedCount() == 0);                         // no all-Reject overflow here
    REQUIRE(cons->getHealth().droppedMessageCount == 8);            // 10 floods - 2 kept

    mgr.removeInstance("BPRprod");
    mgr.removeInstance("BPRcons");
}

TEST_CASE("Backpressure - Reject rejects the NEWEST at the door on an all-Reject overflow", "[backpressure][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    auto prod = mgr.createInstance("BPRRprod");
    auto cons = mgr.createInstance("BPRRcons");
    cons->setMaxQueueSize(2);
    cons->setTopicPolicy("bp:crit", BackpressurePolicy::Reject);

    std::vector<int> pulled;
    cons->subscribe("bp:.*", [&](const Message& m) { pulled.push_back(static_cast<int>(m.data->getInt("v", -1))); });

    for (int v = 1; v <= 4; ++v) prod->publish("bp:crit", nodeV(v));  // 4 criticals into a cap-2 all-Reject inbox

    REQUIRE(cons->hasMessages() == 2);
    while (cons->hasMessages() > 0) cons->pullAndDispatch();

    // The FIRST two accepted criticals survive (never evicted); the newer ones are rejected at the door.
    REQUIRE(pulled == std::vector<int>{1, 2});
    REQUIRE(cons->getRejectedCount() == 2);
    REQUIRE(cons->getHealth().droppedMessageCount == 0);            // rejected-at-door is NOT drop-oldest

    mgr.removeInstance("BPRRprod");
    mgr.removeInstance("BPRRcons");
}

TEST_CASE("Backpressure - setTopicPolicy round-trips; default is DropOldest", "[backpressure][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    auto io = mgr.createInstance("BPset");

    REQUIRE(io->getTopicPolicy("anything") == BackpressurePolicy::DropOldest);
    io->setTopicPolicy("t:c", BackpressurePolicy::Coalesce);
    REQUIRE(io->getTopicPolicy("t:c") == BackpressurePolicy::Coalesce);
    io->setTopicPolicy("t:c", BackpressurePolicy::DropOldest);       // back to default -> entry removed
    REQUIRE(io->getTopicPolicy("t:c") == BackpressurePolicy::DropOldest);

    mgr.removeInstance("BPset");
}
