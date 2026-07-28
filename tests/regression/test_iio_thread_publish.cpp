/**
 * Regression: publishing from a WORKER THREAD must not corrupt the heap at thread exit.
 *
 * WHAT  : one IntraIO instance, one spawned thread, ONE published message, join. Then keep running.
 *
 * WHY   : this is the whole of IOSystemStress's 0xC0000374 (STATUS_HEAP_CORRUPTION), reduced. It cost
 *         a long hunt because it presented as a ~3% flake in a 6-phase stress test; isolated it is
 *         DETERMINISTIC — 20/20 crashes before the fix, 0/20 after. The cause was a `thread_local`
 *         with a NON-TRIVIAL destructor in publish(): the destructor runs during thread teardown and
 *         blows the heap. See docs/design/iosystemstress-heap-corruption-handoff.md.
 *
 *         Every module system in this engine (THREADED, THREAD_POOL) publishes from worker threads,
 *         so the broken path was the engine's normal way of working, not an exotic one.
 *
 * HOW   : the assertion is SURVIVAL — heap corruption kills the process outright (Windows fails fast,
 *         which is why no handler ever caught it), so reaching the end IS the proof.
 *
 * ⚠️ HONEST LIMIT — this test is NOT the crash gate, and must not be sold as one. Measured: it passes
 *    on the UNFIXED engine too. The fault reproduces deterministically only in a standalone binary
 *    built straight from the engine sources (17/20 crashes with the project's own -O3 -DNDEBUG), and
 *    not when linked as a ctest against GroveEngine::impl — the link configuration changes how the
 *    thread_local is laid out and torn down. The real gate is the reduced driver
 *    `tests/repro/tsan_iio_concurrency.cpp`, whose exact recipe is in the handoff.
 *
 *    What this test DOES guard, and why it earns its place: that publishing from worker threads keeps
 *    working, and that the causedBy correlation the thread_local exists FOR still works. A "fix" that
 *    simply deleted the per-thread state would stop the crash and silently break causal correlation —
 *    this catches that.
 */

#include <catch2/catch_test_macros.hpp>

#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

using namespace grove;

TEST_CASE("Publishing from a worker thread survives that thread's exit", "[regression][iio][thread]") {
    auto& mgr = IntraIOManager::getInstance();
    // Mirror the standalone repro's shape exactly: a second, never-subscribed instance exists
    // alongside the publisher (that is what the reduced driver does, and shape matters here).
    auto idle = mgr.createInstance("tp_idle");
    auto io   = mgr.createInstance("tp_worker");

    // ONE message is enough: the fault is in thread teardown, not in volume. (Measured: 1 message
    // crashed as reliably as 500 — which is what pointed the hunt away from the routing path.)
    std::thread worker([&] {
        auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{{"id", 1}});
        io->publish("thread:test", std::move(data));
    });
    worker.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    while (idle->hasMessages() > 0) { try { idle->pullAndDispatch(); } catch (...) {} }

    // Reaching here at all is the regression check. The rest guards against a "fix" that works by
    // quietly breaking publishing.
    SUCCEED("worker thread exited without corrupting the heap");

    // MANY threads in sequence: each one's teardown must be clean, not just the first. The count is
    // deliberately high — in an optimised project build the fault surfaced only a few percent of the
    // time per stress run, so a handful of threads would be a coin flip, not a gate.
    for (int i = 0; i < 400; ++i) {
        std::thread t([&, i] {
            auto d = std::make_unique<JsonDataNode>("data", nlohmann::json{{"id", i}});
            io->publish("thread:test", std::move(d));
        });
        t.join();
    }

    // And the cause-id correlation the thread_local exists FOR still works: a message published from
    // inside a handler is stamped with the id of the message that triggered it. A fix that dropped the
    // per-thread state entirely would pass the crash check and silently break this.
    auto producer = mgr.createInstance("tp_producer");
    auto relay    = mgr.createInstance("tp_relay");
    std::atomic<int> relayed{0};
    std::string seenCause;

    relay->subscribe("tp:in", [&](const Message& m) {
        (void)m;
        auto out = std::make_unique<JsonDataNode>("out", nlohmann::json{{"v", 1}});
        relay->publish("tp:out", std::move(out));   // published from INSIDE a handler
        relayed++;
    });
    auto sink = mgr.createInstance("tp_sink");
    sink->subscribe("tp:out", [&](const Message& m) { seenCause = m.env.causedBy; });

    producer->publish("tp:in", std::make_unique<JsonDataNode>("in", nlohmann::json{{"v", 0}}));
    for (int i = 0; i < 50 && relayed.load() == 0; ++i) {
        while (relay->hasMessages() > 0) relay->pullAndDispatch();
        while (sink->hasMessages() > 0)  sink->pullAndDispatch();
    }
    while (sink->hasMessages() > 0) sink->pullAndDispatch();

    CHECK(relayed.load() > 0);
    CHECK_FALSE(seenCause.empty());   // the in-handler publish carried a cause id
}
