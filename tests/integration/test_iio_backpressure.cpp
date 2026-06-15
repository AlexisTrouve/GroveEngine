/**
 * Regression test: IntraIO backpressure / queue bounding.
 *
 * WHY: enforceQueueLimits() existed but was NEVER called by the live code (only the
 * old implementation called it). So a stalled consumer (a module mid-reload, a slow
 * worker, a renderer blocked on vsync) caused the message queue to grow WITHOUT
 * bound — unbounded memory growth ending in bad_alloc/OOM-kill — while the advertised
 * "backpressure" (maxQueueSize / droppedMessageCount / IOHealth.dropping) did nothing.
 *
 * This test floods an instance far past its maxQueueSize without ever pulling, and
 * asserts the queue is BOUNDED and the overflow is COUNTED (observable), not silently
 * accumulated. It locks the fix (deliverMessage must enforce the cap).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IntraIO enforces maxQueueSize on a stalled consumer (backpressure)", "[iio][backpressure]") {
    auto& mgr = IntraIOManager::getInstance();
    auto io = mgr.createInstance("backpressure_test");
    REQUIRE(io != nullptr);

    const size_t cap = 100;
    io->setMaxQueueSize(cap);

    // Deliver 10x the cap WITHOUT ever pulling — simulates a consumer that stopped.
    const int flood = 1000;
    for (int i = 0; i < flood; ++i) {
        auto node = std::make_unique<JsonDataNode>("data", nlohmann::json{{"i", i}});
        io->deliverMessage("test:flood", std::move(node), /*isLowFreq=*/false);
    }

    auto health = io->getHealth();
    INFO("queueSize=" << health.queueSize
         << " maxQueueSize=" << health.maxQueueSize
         << " dropped=" << health.droppedMessageCount);

    // The queue MUST be bounded by maxQueueSize — not grown to ~flood (unbounded).
    REQUIRE(static_cast<size_t>(health.queueSize) <= cap);
    // The overflow MUST be counted (observable), proving drops happened rather than
    // silent unbounded growth.
    REQUIRE(health.droppedMessageCount > 0);

    mgr.removeInstance("backpressure_test");
}

TEST_CASE("IntraIO bounds the low-frequency queue too", "[iio][backpressure]") {
    auto& mgr = IntraIOManager::getInstance();
    auto io = mgr.createInstance("backpressure_lowfreq_test");
    REQUIRE(io != nullptr);

    const size_t cap = 50;
    io->setMaxQueueSize(cap);

    const int flood = 500;
    for (int i = 0; i < flood; ++i) {
        auto node = std::make_unique<JsonDataNode>("data", nlohmann::json{{"i", i}});
        io->deliverMessage("test:lowfreq", std::move(node), /*isLowFreq=*/true);
    }

    auto health = io->getHealth();
    INFO("queueSize=" << health.queueSize << " dropped=" << health.droppedMessageCount);
    // Total queued (high + low) must stay bounded — low-freq flood must not grow forever.
    REQUIRE(static_cast<size_t>(health.queueSize) <= cap);

    mgr.removeInstance("backpressure_lowfreq_test");
}
