/**
 * DataNode Read-Only API Benchmarks
 *
 * Compares getChild() (copy) vs getChildReadOnly() (zero-copy)
 * Demonstrates performance benefits of read-only access for concurrent reads
 */

#include "helpers/BenchmarkTimer.h"
#include "helpers/BenchmarkStats.h"
#include "helpers/BenchmarkReporter.h"

#include "grove/JsonDataNode.h"

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>

using namespace GroveEngine::Benchmark;
using namespace grove;

// Helper to create a test tree
std::unique_ptr<JsonDataNode> createTestTree(int depth = 1) {
    auto root = std::make_unique<JsonDataNode>("root", nlohmann::json{
        {"root_value", 123}
    });

    if (depth >= 1) {
        auto player = std::make_unique<JsonDataNode>("player", nlohmann::json{
            {"player_id", 456}
        });

        if (depth >= 2) {
            auto stats = std::make_unique<JsonDataNode>("stats", nlohmann::json{
                {"level", 10}
            });

            if (depth >= 3) {
                auto health = std::make_unique<JsonDataNode>("health", nlohmann::json{
                    {"current", 100},
                    {"max", 100}
                });
                stats->setChild("health", std::move(health));
            }

            player->setChild("stats", std::move(stats));
        }

        root->setChild("player", std::move(player));
    }

    return root;
}

// Helper to create deep tree
std::unique_ptr<JsonDataNode> createDeepTree(int levels) {
    auto root = std::make_unique<JsonDataNode>("root", nlohmann::json{{"level", 0}});

    JsonDataNode* current = root.get();
    for (int i = 1; i < levels; ++i) {
        auto child = std::make_unique<JsonDataNode>("l" + std::to_string(i),
                                                     nlohmann::json{{"level", i}});
        JsonDataNode* childPtr = child.get();
        current->setChild("l" + std::to_string(i), std::move(child));
        current = childPtr;
    }

    return root;
}

// ============================================================================
// Benchmark I: getChild() Baseline (with copy)
// ============================================================================

void benchmarkI_getChild_baseline() {
    BenchmarkReporter reporter;
    reporter.printHeader("I: getChild() Baseline (Copy Semantics)");

    const int iterations = 10000;

    // Create test tree
    auto tree = createTestTree(3); // root → player → stats → health

    // Warm up
    for (int i = 0; i < 100; ++i) {
        auto child = tree->getChild("player");
        if (child) {
            tree->setChild("player", std::move(child)); // Put it back
        }
    }

    // Benchmark
    BenchmarkTimer timer;
    BenchmarkStats stats;

    for (int i = 0; i < iterations; ++i) {
        timer.start();
        auto child = tree->getChild("player");
        stats.addSample(timer.elapsedUs());

        // Put it back for next iteration
        if (child) {
            tree->setChild("player", std::move(child));
        }
    }

    // Report
    reporter.printMessage("Configuration: " + std::to_string(iterations) +
                         " iterations, tree depth=3\n");

    reporter.printResult("Mean time", stats.mean(), "µs");
    reporter.printResult("Median time", stats.median(), "µs");
    reporter.printResult("P95", stats.p95(), "µs");
    reporter.printResult("Min", stats.min(), "µs");
    reporter.printResult("Max", stats.max(), "µs");

    reporter.printSubseparator();
    reporter.printSummary("Baseline established for getChild() with ownership transfer");
}

// ============================================================================
// Benchmark J: getChildReadOnly() Zero-Copy
// ============================================================================

void benchmarkJ_getChildReadOnly() {
    BenchmarkReporter reporter;
    reporter.printHeader("J: getChildReadOnly() Zero-Copy Access");

    const int iterations = 10000;

    // Create test tree
    auto tree = createTestTree(3);

    // Warm up
    for (int i = 0; i < 100; ++i) {
        volatile auto child = tree->getChildReadOnly("player");
        (void)child;
    }

    // Benchmark
    BenchmarkTimer timer;
    BenchmarkStats stats;

    for (int i = 0; i < iterations; ++i) {
        timer.start();
        volatile auto child = tree->getChildReadOnly("player");
        stats.addSample(timer.elapsedUs());
        (void)child; // Prevent optimization
    }

    // Report
    reporter.printMessage("Configuration: " + std::to_string(iterations) +
                         " iterations, tree depth=3\n");

    reporter.printResult("Mean time", stats.mean(), "µs");
    reporter.printResult("Median time", stats.median(), "µs");
    reporter.printResult("P95", stats.p95(), "µs");
    reporter.printResult("Min", stats.min(), "µs");
    reporter.printResult("Max", stats.max(), "µs");

    reporter.printSubseparator();
    reporter.printSummary("Zero-copy read-only access measured");
}

// ============================================================================
// Benchmark K: Concurrent Reads Throughput
// ============================================================================

void benchmarkK_concurrent_reads() {
    BenchmarkReporter reporter;
    reporter.printHeader("K: Concurrent Reads Throughput");

    const int readsPerThread = 1000;
    std::vector<int> threadCounts = {1, 2, 4, 8};

    // Create shared tree
    auto tree = createTestTree(3);

    reporter.printTableHeader("Threads", "Total Reads/s", "Speedup");

    double baseline = 0.0;

    for (size_t i = 0; i < threadCounts.size(); ++i) {
        int numThreads = threadCounts[i];

        std::atomic<int> totalReads{0};
        std::vector<std::thread> threads;

        // Benchmark
        BenchmarkTimer timer;
        timer.start();

        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&tree, readsPerThread, &totalReads]() {
                for (int j = 0; j < readsPerThread; ++j) {
                    volatile auto child = tree->getChildReadOnly("player");
                    (void)child;
                    totalReads.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        double elapsed = timer.elapsedMs();
        double readsPerSec = (totalReads.load() / elapsed) * 1000.0;

        if (i == 0) {
            baseline = readsPerSec;
            reporter.printTableRow(std::to_string(numThreads), readsPerSec, "reads/s");
        } else {
            double speedup = readsPerSec / baseline;
            reporter.printTableRow(std::to_string(numThreads), readsPerSec, "reads/s",
                                  (speedup - 1.0) * 100.0);
        }
    }

    reporter.printSubseparator();
    reporter.printSummary("Concurrent read-only access demonstrates thread scalability");
}

// ============================================================================
// Benchmark L: Deep Navigation Speedup
// ============================================================================

void benchmarkL_deep_navigation() {
    BenchmarkReporter reporter;
    reporter.printHeader("L: Deep Navigation Speedup");

    const int depth = 10;
    const int iterations = 1000;

    // Create deep tree
    auto tree = createDeepTree(depth);

    reporter.printMessage("Configuration: Tree depth=" + std::to_string(depth) +
                         ", iterations=" + std::to_string(iterations) + "\n");

    // Benchmark getChild() (with ownership transfer - need to put back)
    // This is not practical for deep navigation, so we'll measure read-only only
    reporter.printMessage("Note: getChild() not measured for deep navigation");
    reporter.printMessage("      (ownership transfer makes chained calls impractical)\n");

    // Benchmark getChildReadOnly() chain
    BenchmarkTimer timer;
    BenchmarkStats stats;

    for (int i = 0; i < iterations; ++i) {
        timer.start();

        IDataNode* current = tree.get();
        for (int level = 1; level < depth && current; ++level) {
            current = current->getChildReadOnly("l" + std::to_string(level));
        }

        stats.addSample(timer.elapsedUs());

        // Verify we reached the end
        volatile bool reached = (current != nullptr);
        (void)reached;
    }

    reporter.printResult("Mean time (read-only)", stats.mean(), "µs");
    reporter.printResult("Median time", stats.median(), "µs");
    reporter.printResult("P95", stats.p95(), "µs");
    reporter.printResult("Avg per level", stats.mean() / depth, "µs");

    reporter.printSubseparator();
    reporter.printSummary("Read-only API enables efficient deep tree navigation");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "          DATANODE READ-ONLY API BENCHMARKS\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";

    benchmarkI_getChild_baseline();
    benchmarkJ_getChildReadOnly();
    benchmarkK_concurrent_reads();
    benchmarkL_deep_navigation();

    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "✅ ALL BENCHMARKS COMPLETE\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << std::endl;

    return 0;
}
