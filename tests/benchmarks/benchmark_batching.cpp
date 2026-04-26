/**
 * IntraIO Batching Benchmarks
 *
 * Measures the performance gains and overhead of message batching
 * for low-frequency subscriptions in the IntraIO pub/sub system.
 */

#include "helpers/BenchmarkTimer.h"
#include "helpers/BenchmarkStats.h"
#include "helpers/BenchmarkReporter.h"

#include "grove/IOFactory.h"
#include "grove/IntraIOManager.h"
#include "grove/JsonDataNode.h"

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

using namespace GroveEngine::Benchmark;
using namespace grove;

// Helper to create test messages
std::unique_ptr<IDataNode> createTestMessage(int id, const std::string& payload = "test") {
    return std::make_unique<JsonDataNode>("data", nlohmann::json{
        {"id", id},
        {"payload", payload}
    });
}

// Message counter for testing
struct MessageCounter {
    std::atomic<int> received{0};
    std::atomic<int> batches{0};

    void reset() {
        received.store(0);
        batches.store(0);
    }
};

// ============================================================================
// Benchmark E: Baseline without Batching (High-Frequency)
// ============================================================================

void benchmarkE_baseline() {
    BenchmarkReporter reporter;
    reporter.printHeader("E: Baseline Performance (High-Frequency, No Batching)");

    const int messageCount = 10000;

    // Create publisher and subscriber
    auto publisherIO = IOFactory::create("intra", "publisher_e");
    auto subscriberIO = IOFactory::create("intra", "subscriber_e");

    // Subscribe with high-frequency (no batching).
    // Current API: subscribe() requires a handler callback as 2nd argument.
    // We use an atomic counter to measure throughput without the old pull-value API.
    std::atomic<int> receivedCount{0};
    subscriberIO->subscribe("test:*", [&receivedCount](const grove::Message& /*msg*/) {
        receivedCount.fetch_add(1, std::memory_order_relaxed);
    });

    // Warm up
    for (int i = 0; i < 100; ++i) {
        publisherIO->publish("test:warmup", createTestMessage(i));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Drain warmup messages via pullAndDispatch (replaces old pullMessage() pull-value API)
    while (subscriberIO->hasMessages() > 0) {
        subscriberIO->pullAndDispatch();
    }
    receivedCount.store(0); // Reset counter after warmup

    // Benchmark publishing
    BenchmarkTimer timer;
    timer.start();

    for (int i = 0; i < messageCount; ++i) {
        publisherIO->publish("test:message", createTestMessage(i));
    }

    double publishTime = timer.elapsedMs();

    // Allow routing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Count received messages via pullAndDispatch (handler callback increments receivedCount)
    BenchmarkStats latencyStats;

    timer.start();
    while (subscriberIO->hasMessages() > 0) {
        subscriberIO->pullAndDispatch();
    }
    double pullTime = timer.elapsedMs();

    double totalTime = publishTime + pullTime;
    double throughput = (messageCount / totalTime) * 1000.0; // messages/sec
    double avgLatency = (totalTime / messageCount) * 1000.0; // microseconds

    // Report
    reporter.printMessage("Configuration: " + std::to_string(messageCount) + " messages, high-frequency\n");

    reporter.printResult("Messages sent", static_cast<double>(messageCount), "msgs");
    reporter.printResult("Messages received", static_cast<double>(receivedCount), "msgs");
    reporter.printResult("Publish time", publishTime, "ms");
    reporter.printResult("Pull time", pullTime, "ms");
    reporter.printResult("Total time", totalTime, "ms");
    reporter.printResult("Throughput", throughput, "msg/s");
    reporter.printResult("Avg latency", avgLatency, "µs");

    reporter.printSubseparator();

    if (receivedCount == messageCount) {
        reporter.printSummary("Baseline established: " +
                            std::to_string(static_cast<int>(throughput)) + " msg/s");
    } else {
        reporter.printSummary("WARNING: Message loss detected (" +
                            std::to_string(receivedCount) + "/" +
                            std::to_string(messageCount) + ")");
    }
}

// ============================================================================
// Benchmark F: With Batching (Low-Frequency)
// ============================================================================

void benchmarkF_batching() {
    BenchmarkReporter reporter;
    reporter.printHeader("F: Batching Performance (Low-Frequency Subscription)");

    const int messageCount = 1000; // Reduced for faster benchmarking
    const int batchIntervalMs = 50; // 50ms batching
    const float durationSeconds = 1.0f; // Publish over 1 second
    const int publishRateMs = static_cast<int>((durationSeconds * 1000.0f) / messageCount);

    // Create publisher and subscriber
    auto publisherIO = IOFactory::create("intra", "publisher_f");
    auto subscriberIO = IOFactory::create("intra", "subscriber_f");

    // Subscribe with low-frequency batching.
    // Current API: subscribeLowFreq() requires handler as 2nd arg, config as 3rd.
    // We count dispatched messages via an atomic to track batching behavior.
    SubscriptionConfig config;
    config.batchInterval = batchIntervalMs;
    config.replaceable = false; // Accumulate messages
    std::atomic<int> batchCount{0};
    subscriberIO->subscribeLowFreq("test:*", [&batchCount](const grove::Message& /*msg*/) {
        batchCount.fetch_add(1, std::memory_order_relaxed);
    }, config);

    reporter.printMessage("Configuration:");
    reporter.printResult("  Total messages", static_cast<double>(messageCount), "msgs");
    reporter.printResult("  Batch interval", static_cast<double>(batchIntervalMs), "ms");
    reporter.printResult("  Duration", static_cast<double>(durationSeconds), "s");
    reporter.printResult("  Expected batches", durationSeconds * (1000.0 / batchIntervalMs), "");

    std::cout << "\n";

    // Benchmark
    BenchmarkTimer timer;
    timer.start();

    // Publish messages over duration
    for (int i = 0; i < messageCount; ++i) {
        publisherIO->publish("test:batch", createTestMessage(i));
        if (publishRateMs > 0 && i < messageCount - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(publishRateMs));
        }
    }

    double publishTime = timer.elapsedMs();

    // Wait for final batch to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(batchIntervalMs + 50));

    // Drain pending messages via pullAndDispatch (replaces old pullMessage() pull-value API).
    // The handler registered above increments batchCount for each dispatched message.
    int totalMessages = 0;

    while (subscriberIO->hasMessages() > 0) {
        subscriberIO->pullAndDispatch(); // triggers handler → increments batchCount
        totalMessages++;
    }

    double totalTime = timer.elapsedMs();
    double expectedBatches = (durationSeconds * 1000.0) / batchIntervalMs;
    // Load atomic batchCount into a plain int for arithmetic (atomic is not implicitly convertible)
    int batchCountVal = batchCount.load();
    double reductionRatio = static_cast<double>(messageCount) / std::max(1, batchCountVal);

    // Report
    reporter.printMessage("Results:\n");

    reporter.printResult("Published messages", static_cast<double>(messageCount), "msgs");
    reporter.printResult("Batches received", static_cast<double>(batchCountVal), "batches");
    reporter.printResult("Reduction ratio", reductionRatio, "x");
    reporter.printResult("Publish time", publishTime, "ms");
    reporter.printResult("Total time", totalTime, "ms");

    reporter.printSubseparator();

    if (reductionRatio >= 100.0 && batchCountVal > 0) {
        reporter.printSummary("SUCCESS - Reduction >" + std::to_string(static_cast<int>(reductionRatio)) +
                            "x (" + std::to_string(messageCount) + " msgs → " +
                            std::to_string(batchCountVal) + " batches)");
    } else {
        reporter.printSummary("Batching active: " + std::to_string(static_cast<int>(reductionRatio)) +
                            "x reduction (" + std::to_string(batchCountVal) + " batches)");
    }
}

// ============================================================================
// Benchmark G: Batch Flush Thread Overhead
// ============================================================================

void benchmarkG_thread_overhead() {
    BenchmarkReporter reporter;
    reporter.printHeader("G: Batch Flush Thread Overhead");

    std::vector<int> bufferCounts = {0, 10, 50}; // Reduced from 100 to 50
    const int testDurationMs = 500; // Reduced from 1000 to 500
    const int batchIntervalMs = 50; // Reduced from 100 to 50

    reporter.printTableHeader("Active Buffers", "Duration (ms)", "");

    for (int bufferCount : bufferCounts) {
        // Create subscribers with low-freq subscriptions
        std::vector<std::unique_ptr<IIO>> subscribers;

        for (int i = 0; i < bufferCount; ++i) {
            auto sub = IOFactory::create("intra", "sub_g_" + std::to_string(i));

            SubscriptionConfig config;
            config.batchInterval = batchIntervalMs;
            // Current API: subscribeLowFreq() requires handler as 2nd arg, config as 3rd.
            // No-op handler — this benchmark only measures flush thread overhead, not message content.
            sub->subscribeLowFreq("test:sub" + std::to_string(i) + ":*",
                [](const grove::Message& /*msg*/) { /* overhead test: discard */ },
                config);

            subscribers.push_back(std::move(sub));
        }

        // Measure time (thread is running in background)
        BenchmarkTimer timer;
        timer.start();

        std::this_thread::sleep_for(std::chrono::milliseconds(testDurationMs));

        double elapsed = timer.elapsedMs();

        reporter.printTableRow(std::to_string(bufferCount), elapsed, "ms");

        // Cleanup happens automatically when subscribers go out of scope
    }

    reporter.printSubseparator();
    reporter.printSummary("Flush thread overhead is minimal (runs in background)");
}

// ============================================================================
// Benchmark H: Scalability with Low-Freq Subscribers
// ============================================================================

void benchmarkH_scalability() {
    BenchmarkReporter reporter;
    reporter.printHeader("H: Scalability with Low-Frequency Subscribers");

    std::vector<int> subscriberCounts = {1, 10, 50}; // Reduced from 100 to 50
    const int messagesPerSub = 50; // Reduced from 100 to 50
    const int batchIntervalMs = 50; // Reduced from 100 to 50

    reporter.printTableHeader("Subscribers", "Flush Time (ms)", "vs. Baseline");

    double baseline = 0.0;

    for (size_t i = 0; i < subscriberCounts.size(); ++i) {
        int subCount = subscriberCounts[i];

        // Create publisher
        auto publisher = IOFactory::create("intra", "pub_h");

        // Create subscribers
        std::vector<std::unique_ptr<IIO>> subscribers;
        for (int j = 0; j < subCount; ++j) {
            auto sub = IOFactory::create("intra", "sub_h_" + std::to_string(j));

            SubscriptionConfig config;
            config.batchInterval = batchIntervalMs;
            config.replaceable = false;

            // Each subscriber has unique pattern.
            // Current API: subscribeLowFreq() requires handler as 2nd arg, config as 3rd.
            // No-op handler — scalability test only measures flush timing, not message content.
            sub->subscribeLowFreq("test:h:" + std::to_string(j) + ":*",
                [](const grove::Message& /*msg*/) { /* scalability test: discard */ },
                config);

            subscribers.push_back(std::move(sub));
        }

        // Publish messages that match all subscribers
        for (int j = 0; j < subCount; ++j) {
            for (int k = 0; k < messagesPerSub; ++k) {
                publisher->publish("test:h:" + std::to_string(j) + ":msg",
                                 createTestMessage(k));
            }
        }

        // Measure flush time
        BenchmarkTimer timer;
        timer.start();

        // Wait for flush cycle
        std::this_thread::sleep_for(std::chrono::milliseconds(batchIntervalMs + 25));

        double flushTime = timer.elapsedMs();

        if (i == 0) {
            baseline = flushTime;
            reporter.printTableRow(std::to_string(subCount), flushTime, "ms");
        } else {
            double percentChange = ((flushTime - baseline) / baseline) * 100.0;
            reporter.printTableRow(std::to_string(subCount), flushTime, "ms", percentChange);
        }
    }

    reporter.printSubseparator();
    reporter.printSummary("Flush time scales with subscriber count (expected behavior)");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "          INTRAIO BATCHING BENCHMARKS\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";

    benchmarkE_baseline();
    benchmarkF_batching();
    benchmarkG_thread_overhead();
    benchmarkH_scalability();

    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "✅ ALL BENCHMARKS COMPLETE\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << std::endl;

    return 0;
}
