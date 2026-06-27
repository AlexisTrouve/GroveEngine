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
        io->deliverMessage("test:flood", std::move(node), /*isLowFreq=*/false, /*env=*/{});
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

TEST_CASE("matcher consistency: a topic routed by the manager is never swallowed", "[iio][routing]") {
    // The manager's TopicTree treats ".*" as a TERMINAL wildcard (matches the rest of
    // the topic; any suffix written after ".*" is dropped). So pattern "a:.*:z" routes
    // ANY "a:..." topic. The IntraIO-side regex used to honor the ":z" suffix, so it
    // under-matched in pullAndDispatch() and SWALLOWED the routed message (handler never
    // fired). compileTopicPattern now mirrors TopicTree's terminal-".*". This locks it.
    auto& mgr = IntraIOManager::getInstance();
    auto pub = mgr.createInstance("swallow_pub");
    auto sub = mgr.createInstance("swallow_sub");

    int received = 0;
    sub->subscribe("a:.*:z", [&](const Message&) { received++; });

    auto d = std::make_unique<JsonDataNode>("d", nlohmann::json{{"v", 1}});
    pub->publish("a:1:b", std::move(d));  // TopicTree routes this ("a:" + terminal ".*")
    while (sub->hasMessages() > 0) sub->pullAndDispatch();

    REQUIRE(received == 1);  // pre-fix: 0 — swallowed by the stricter IntraIO regex

    mgr.removeInstance("swallow_pub");
    mgr.removeInstance("swallow_sub");
}

TEST_CASE("matcher consistency: single-* matches one segment, .* matches the rest (end-to-end)", "[iio][routing]") {
    // Locks the wildcard ROUTING contract through the real manager + IntraIO path
    // (audit #7). Two matchers actually decide delivery and must agree:
    //   - TopicTree (routing gatekeeper): '*' = exactly one segment, '.*' = terminal (rest).
    //   - the manager's patternMatches lambda (freq lookup): same semantics.
    // The per-instance IntraIO regex is only a post-routing filter; it can drop but never
    // add deliveries, so it can't widen '*' beyond what TopicTree routed. This test pins
    // the observable end-to-end behavior so a future change to any matcher is caught.
    auto& mgr = IntraIOManager::getInstance();
    auto pub        = mgr.createInstance("wild_pub");
    auto subSingle  = mgr.createInstance("wild_single");
    auto subMulti   = mgr.createInstance("wild_multi");

    int single = 0, multi = 0;
    subSingle->subscribe("a:*:c", [&](const Message&) { single++; });  // one segment between a and c
    subMulti->subscribe("a:.*",   [&](const Message&) { multi++; });   // rest of the topic

    auto pubTopic = [&](const std::string& topic) {
        auto d = std::make_unique<JsonDataNode>("d", nlohmann::json{{"v", 1}});
        pub->publish(topic, std::move(d));
        while (subSingle->hasMessages() > 0) subSingle->pullAndDispatch();
        while (subMulti->hasMessages()  > 0) subMulti->pullAndDispatch();
    };

    pubTopic("a:1:c");    // single middle segment -> single-* matches; .* matches
    pubTopic("a:1:2:c");  // TWO middle segments   -> single-* must NOT match; .* matches
    pubTopic("a:9");      // no :c                 -> single-* must NOT match; .* matches

    INFO("single=" << single << " multi=" << multi);
    REQUIRE(single == 1);  // 'a:*:c' is one segment only — never crosses ':'
    REQUIRE(multi  == 3);  // 'a:.*' is terminal — matches every 'a:...' topic

    mgr.removeInstance("wild_pub");
    mgr.removeInstance("wild_single");
    mgr.removeInstance("wild_multi");
}

TEST_CASE("reload hygiene: clearInstanceSubscriptions stops manager routing", "[iio][reload]") {
    // At hot-reload, ModuleLoader::unload() used to clear ONLY the IntraIO-side handler
    // vectors. The manager (TopicTree) still routed to the instance, so publishes landed
    // in a queue nobody drains — phantom delivery + a slow stale-entry leak. The fix adds
    // IntraIOManager::clearInstanceSubscriptions() to wipe the manager-side routing while
    // keeping the instance alive for re-subscription. This locks that behavior.
    auto& mgr = IntraIOManager::getInstance();
    auto pub = mgr.createInstance("route_pub");
    auto sub = mgr.createInstance("route_sub");

    int received = 0;
    sub->subscribe("route:test", [&](const Message&) { received++; });

    auto publishOne = [&](int v) {
        auto d = std::make_unique<JsonDataNode>("d", nlohmann::json{{"x", v}});
        pub->publish("route:test", std::move(d));
    };

    // Baseline: routing works.
    publishOne(1);
    while (sub->hasMessages() > 0) sub->pullAndDispatch();
    REQUIRE(received == 1);

    // Clearing only the IntraIO-side handlers (the old unload behavior) leaves the
    // manager routing intact → the message is still delivered into sub's queue.
    sub->clearAllSubscriptions();
    publishOne(2);
    INFO("phantom-queued after handler-only clear: " << sub->hasMessages());
    REQUIRE(sub->hasMessages() > 0);  // demonstrates the phantom routing

    // The fix: also clear manager-side routing. Now publishes do NOT reach the instance.
    sub->clearAllMessages();
    mgr.clearInstanceSubscriptions("route_sub");
    publishOne(3);
    REQUIRE(sub->hasMessages() == 0);  // no routing after full clear

    mgr.removeInstance("route_pub");
    mgr.removeInstance("route_sub");
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
        io->deliverMessage("test:lowfreq", std::move(node), /*isLowFreq=*/true, /*env=*/{});
    }

    auto health = io->getHealth();
    INFO("queueSize=" << health.queueSize << " dropped=" << health.droppedMessageCount);
    // Total queued (high + low) must stay bounded — low-freq flood must not grow forever.
    REQUIRE(static_cast<size_t>(health.queueSize) <= cap);

    mgr.removeInstance("backpressure_lowfreq_test");
}
