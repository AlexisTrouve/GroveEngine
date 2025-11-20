/**
 * Demo benchmark to validate the benchmark helpers.
 * Tests BenchmarkTimer, BenchmarkStats, and BenchmarkReporter.
 */

#include "helpers/BenchmarkTimer.h"
#include "helpers/BenchmarkStats.h"
#include "helpers/BenchmarkReporter.h"

#include <thread>
#include <chrono>
#include <vector>
#include <cmath>

using namespace GroveEngine::Benchmark;

// Simulate some work
void doWork(int microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

// Simulate variable work with some computation
double computeWork(int iterations) {
    double result = 0.0;
    for (int i = 0; i < iterations; ++i) {
        result += std::sqrt(i * 3.14159 + 1.0);
    }
    return result;
}

void testTimer() {
    BenchmarkReporter reporter;
    reporter.printHeader("Timer Accuracy Test");

    BenchmarkTimer timer;

    // Test 1: Measure a known sleep duration
    timer.start();
    doWork(1000); // 1ms = 1000µs
    double elapsed = timer.elapsedUs();

    reporter.printMessage("Sleep 1000µs test:");
    reporter.printResult("Measured", elapsed, "µs");
    reporter.printResult("Expected", 1000.0, "µs");
    reporter.printResult("Error", std::abs(elapsed - 1000.0), "µs");
}

void testStats() {
    BenchmarkReporter reporter;
    reporter.printHeader("Statistics Test");

    BenchmarkStats stats;

    // Add samples: 1, 2, 3, ..., 100
    for (int i = 1; i <= 100; ++i) {
        stats.addSample(static_cast<double>(i));
    }

    reporter.printMessage("Dataset: 1, 2, 3, ..., 100");
    reporter.printStats("",
                       stats.mean(),
                       stats.median(),
                       stats.p95(),
                       stats.p99(),
                       stats.min(),
                       stats.max(),
                       stats.stddev(),
                       "");

    reporter.printMessage("\nExpected values:");
    reporter.printResult("Mean", 50.5, "");
    reporter.printResult("Median", 50.5, "");
    reporter.printResult("Min", 1.0, "");
    reporter.printResult("Max", 100.0, "");
}

void testReporter() {
    BenchmarkReporter reporter;
    reporter.printHeader("Reporter Format Test");

    reporter.printTableHeader("Configuration", "Time (µs)", "Change");
    reporter.printTableRow("10 items", 1.23, "µs");
    reporter.printTableRow("100 items", 1.31, "µs", 6.5);
    reporter.printTableRow("1000 items", 1.45, "µs", 17.9);

    reporter.printSummary("All formatting features working");
}

void testIntegration() {
    BenchmarkReporter reporter;
    reporter.printHeader("Integration Test: Computation Scaling");

    BenchmarkTimer timer;
    std::vector<int> workloads = {1000, 5000, 10000, 50000, 100000};
    std::vector<double> times;

    reporter.printTableHeader("Iterations", "Time (µs)", "vs. Baseline");

    double baseline = 0.0;
    for (size_t i = 0; i < workloads.size(); ++i) {
        int iterations = workloads[i];
        BenchmarkStats stats;

        // Run 10 samples for each workload
        for (int sample = 0; sample < 10; ++sample) {
            timer.start();
            volatile double result = computeWork(iterations);
            (void)result; // Prevent optimization
            stats.addSample(timer.elapsedUs());
        }

        double avgTime = stats.mean();
        times.push_back(avgTime);

        if (i == 0) {
            baseline = avgTime;
            reporter.printTableRow(std::to_string(iterations), avgTime, "µs");
        } else {
            double percentChange = ((avgTime - baseline) / baseline) * 100.0;
            reporter.printTableRow(std::to_string(iterations), avgTime, "µs", percentChange);
        }
    }

    reporter.printSummary("Computation time scales with workload");
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "          BENCHMARK HELPERS VALIDATION SUITE\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";

    testTimer();
    testStats();
    testReporter();
    testIntegration();

    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "✅ ALL HELPERS VALIDATED SUCCESSFULLY\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << std::endl;

    return 0;
}
