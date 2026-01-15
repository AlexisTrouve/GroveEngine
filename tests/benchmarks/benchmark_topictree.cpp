/**
 * TopicTree Routing Benchmarks
 *
 * Proves that routing is O(k) where k = topic depth
 * Measures speedup vs naive linear search approach
 */

#include "helpers/BenchmarkTimer.h"
#include "helpers/BenchmarkStats.h"
#include "helpers/BenchmarkReporter.h"

#include <topictree/TopicTree.h>
#include <string>
#include <vector>
#include <random>
#include <sstream>

using namespace GroveEngine::Benchmark;

// Random number generator
static std::mt19937 rng(42); // Fixed seed for reproducibility

// Generate random subscriber patterns
std::vector<std::string> generatePatterns(int count, int maxDepth) {
    std::vector<std::string> patterns;
    patterns.reserve(count);

    std::uniform_int_distribution<> depthDist(2, maxDepth);
    std::uniform_int_distribution<> segmentDist(0, 20); // 0-20 or wildcard
    std::uniform_int_distribution<> wildcardDist(0, 100);

    for (int i = 0; i < count; ++i) {
        int depth = depthDist(rng);
        std::ostringstream oss;

        for (int j = 0; j < depth; ++j) {
            if (j > 0) oss << ':';

            int wildcardChance = wildcardDist(rng);
            if (wildcardChance < 10) {
                // 10% chance of wildcard
                oss << '*';
            } else if (wildcardChance < 15) {
                // 5% chance of multi-wildcard
                oss << ".*";
                break; // .* ends the pattern
            } else {
                // Regular segment
                int segmentId = segmentDist(rng);
                oss << "seg" << segmentId;
            }
        }

        patterns.push_back(oss.str());
    }

    return patterns;
}

// Generate random concrete topics (no wildcards)
std::vector<std::string> generateTopics(int count, int depth) {
    std::vector<std::string> topics;
    topics.reserve(count);

    std::uniform_int_distribution<> segmentDist(0, 50);

    for (int i = 0; i < count; ++i) {
        std::ostringstream oss;

        for (int j = 0; j < depth; ++j) {
            if (j > 0) oss << ':';
            oss << "seg" << segmentDist(rng);
        }

        topics.push_back(oss.str());
    }

    return topics;
}

// Naive linear search implementation for comparison
class NaiveRouter {
private:
    struct Subscription {
        std::string pattern;
        std::string subscriber;
    };

    std::vector<Subscription> subscriptions;

    // Split topic by ':'
    std::vector<std::string> split(const std::string& str) const {
        std::vector<std::string> result;
        std::istringstream iss(str);
        std::string segment;

        while (std::getline(iss, segment, ':')) {
            result.push_back(segment);
        }

        return result;
    }

    // Check if pattern matches topic
    bool matches(const std::string& pattern, const std::string& topic) const {
        auto patternSegs = split(pattern);
        auto topicSegs = split(topic);

        size_t pi = 0, ti = 0;

        while (pi < patternSegs.size() && ti < topicSegs.size()) {
            if (patternSegs[pi] == ".*") {
                return true; // .* matches everything
            } else if (patternSegs[pi] == "*") {
                // Single wildcard - match one segment
                ++pi;
                ++ti;
            } else if (patternSegs[pi] == topicSegs[ti]) {
                ++pi;
                ++ti;
            } else {
                return false;
            }
        }

        return pi == patternSegs.size() && ti == topicSegs.size();
    }

public:
    void subscribe(const std::string& pattern, const std::string& subscriber) {
        subscriptions.push_back({pattern, subscriber});
    }

    std::vector<std::string> findSubscribers(const std::string& topic) const {
        std::vector<std::string> result;

        for (const auto& sub : subscriptions) {
            if (matches(sub.pattern, topic)) {
                result.push_back(sub.subscriber);
            }
        }

        return result;
    }
};

// ============================================================================
// Benchmark A: Scalability with Number of Subscribers
// ============================================================================

void benchmarkA_scalability() {
    BenchmarkReporter reporter;
    reporter.printHeader("A: Scalability with Subscriber Count (O(k) Validation)");

    const std::string testTopic = "seg1:seg2:seg3"; // k=3
    const int routesPerTest = 10000;

    std::vector<int> subscriberCounts = {10, 100, 1000, 10000};
    std::vector<double> avgTimes;

    reporter.printTableHeader("Subscribers", "Avg Time (µs)", "vs. Baseline");

    double baseline = 0.0;

    for (size_t i = 0; i < subscriberCounts.size(); ++i) {
        int subCount = subscriberCounts[i];

        // Setup TopicTree with subscribers
        topictree::TopicTree<std::string> tree;
        auto patterns = generatePatterns(subCount, 5);

        for (size_t j = 0; j < patterns.size(); ++j) {
            tree.registerSubscriber(patterns[j], "sub_" + std::to_string(j));
        }

        // Warm up
        for (int j = 0; j < 100; ++j) {
            volatile auto result = tree.findSubscribers(testTopic);
        }

        // Measure
        BenchmarkStats stats;
        BenchmarkTimer timer;

        for (int j = 0; j < routesPerTest; ++j) {
            timer.start();
            volatile auto result = tree.findSubscribers(testTopic);
            stats.addSample(timer.elapsedUs());
        }

        double avgTime = stats.mean();
        avgTimes.push_back(avgTime);

        if (i == 0) {
            baseline = avgTime;
            reporter.printTableRow(std::to_string(subCount), avgTime, "µs");
        } else {
            double percentChange = ((avgTime - baseline) / baseline) * 100.0;
            reporter.printTableRow(std::to_string(subCount), avgTime, "µs", percentChange);
        }
    }

    // Verdict
    bool success = true;
    for (size_t i = 1; i < avgTimes.size(); ++i) {
        double percentChange = ((avgTimes[i] - baseline) / baseline) * 100.0;
        if (percentChange > 10.0) {
            success = false;
            break;
        }
    }

    if (success) {
        reporter.printSummary("O(k) CONFIRMED - Time remains constant with subscriber count");
    } else {
        reporter.printSummary("WARNING - Time varies >10% (may indicate O(n) behavior)");
    }
}

// ============================================================================
// Benchmark B: TopicTree vs Naive Linear Search
// ============================================================================

void benchmarkB_naive_comparison() {
    BenchmarkReporter reporter;
    reporter.printHeader("B: TopicTree vs Naive Linear Search");

    const int subscriberCount = 1000;
    const int routeCount = 10000;
    const int topicDepth = 3;

    // Generate patterns and topics
    auto patterns = generatePatterns(subscriberCount, 5);
    auto topics = generateTopics(routeCount, topicDepth);

    // Setup TopicTree
    topictree::TopicTree<std::string> tree;
    for (size_t i = 0; i < patterns.size(); ++i) {
        tree.registerSubscriber(patterns[i], "sub_" + std::to_string(i));
    }

    // Setup Naive router
    NaiveRouter naive;
    for (size_t i = 0; i < patterns.size(); ++i) {
        naive.subscribe(patterns[i], "sub_" + std::to_string(i));
    }

    // Warm up
    for (int i = 0; i < 100; ++i) {
        volatile auto result1 = tree.findSubscribers(topics[i % topics.size()]);
        volatile auto result2 = naive.findSubscribers(topics[i % topics.size()]);
    }

    // Benchmark TopicTree
    BenchmarkTimer timer;
    timer.start();
    for (const auto& topic : topics) {
        volatile auto result = tree.findSubscribers(topic);
    }
    double topicTreeTime = timer.elapsedMs();

    // Benchmark Naive
    timer.start();
    for (const auto& topic : topics) {
        volatile auto result = naive.findSubscribers(topic);
    }
    double naiveTime = timer.elapsedMs();

    // Report
    reporter.printMessage("Configuration: " + std::to_string(subscriberCount) +
                         " subscribers, " + std::to_string(routeCount) + " routes\n");

    reporter.printResult("TopicTree total", topicTreeTime, "ms");
    reporter.printResult("Naive total", naiveTime, "ms");

    double speedup = naiveTime / topicTreeTime;
    reporter.printResult("Speedup", speedup, "x");

    reporter.printSubseparator();

    if (speedup >= 10.0) {
        reporter.printSummary("SUCCESS - Speedup >10x (TopicTree is " +
                            std::to_string(static_cast<int>(speedup)) + "x faster)");
    } else {
        reporter.printSummary("Speedup only " + std::to_string(speedup) +
                            "x (expected >10x)");
    }
}

// ============================================================================
// Benchmark C: Impact of Topic Depth (k)
// ============================================================================

void benchmarkC_depth_impact() {
    BenchmarkReporter reporter;
    reporter.printHeader("C: Impact of Topic Depth (k)");

    const int subscriberCount = 100;
    const int routesPerDepth = 10000;

    std::vector<int> depths = {2, 5, 10};
    std::vector<double> avgTimes;

    reporter.printTableHeader("Depth (k)", "Avg Time (µs)", "");

    for (int depth : depths) {
        // Setup
        topictree::TopicTree<std::string> tree;
        auto patterns = generatePatterns(subscriberCount, depth);

        for (size_t i = 0; i < patterns.size(); ++i) {
            tree.registerSubscriber(patterns[i], "sub_" + std::to_string(i));
        }

        auto topics = generateTopics(routesPerDepth, depth);

        // Warm up
        for (int i = 0; i < 100; ++i) {
            volatile auto result = tree.findSubscribers(topics[i % topics.size()]);
        }

        // Measure
        BenchmarkStats stats;
        BenchmarkTimer timer;

        for (const auto& topic : topics) {
            timer.start();
            volatile auto result = tree.findSubscribers(topic);
            stats.addSample(timer.elapsedUs());
        }

        double avgTime = stats.mean();
        avgTimes.push_back(avgTime);

        // Create example topic
        std::ostringstream example;
        for (int i = 0; i < depth; ++i) {
            if (i > 0) example << ':';
            example << 'a' + i;
        }

        reporter.printMessage("k=" + std::to_string(depth) + " example: \"" +
                            example.str() + "\"");
        reporter.printResult("  Avg time", avgTime, "µs");
    }

    reporter.printSubseparator();

    // Check if growth is roughly linear
    // Time should scale proportionally with depth
    bool linear = true;
    if (avgTimes.size() >= 2) {
        // Ratio between consecutive measurements should be roughly equal to depth ratio
        for (size_t i = 1; i < avgTimes.size(); ++i) {
            double timeRatio = avgTimes[i] / avgTimes[0];
            double depthRatio = static_cast<double>(depths[i]) / depths[0];

            // Allow 50% tolerance (linear within reasonable bounds)
            if (timeRatio < depthRatio * 0.5 || timeRatio > depthRatio * 2.0) {
                linear = false;
            }
        }
    }

    if (linear) {
        reporter.printSummary("Linear growth with depth (k) confirmed");
    } else {
        reporter.printSummary("Growth pattern detected (review for O(k) behavior)");
    }
}

// ============================================================================
// Benchmark D: Wildcard Performance
// ============================================================================

void benchmarkD_wildcards() {
    BenchmarkReporter reporter;
    reporter.printHeader("D: Wildcard Performance");

    const int subscriberCount = 100;
    const int routesPerTest = 10000;

    struct TestCase {
        std::string name;
        std::string pattern;
    };

    std::vector<TestCase> testCases = {
        {"Exact match", "seg1:seg2:seg3"},
        {"Single wildcard", "seg1:*:seg3"},
        {"Multi wildcard", "seg1:.*"},
        {"Multiple wildcards", "*:*:*"}
    };

    reporter.printTableHeader("Pattern Type", "Avg Time (µs)", "vs. Exact");

    double exactTime = 0.0;

    for (size_t i = 0; i < testCases.size(); ++i) {
        const auto& tc = testCases[i];

        // Setup tree with this pattern type
        topictree::TopicTree<std::string> tree;

        // Add test pattern
        tree.registerSubscriber(tc.pattern, "test_sub");

        // Add noise (other random patterns)
        auto patterns = generatePatterns(subscriberCount - 1, 5);
        for (size_t j = 0; j < patterns.size(); ++j) {
            tree.registerSubscriber(patterns[j], "sub_" + std::to_string(j));
        }

        // Generate topics to match
        auto topics = generateTopics(routesPerTest, 3);

        // Warm up
        for (int j = 0; j < 100; ++j) {
            volatile auto result = tree.findSubscribers(topics[j % topics.size()]);
        }

        // Measure
        BenchmarkStats stats;
        BenchmarkTimer timer;

        for (const auto& topic : topics) {
            timer.start();
            volatile auto result = tree.findSubscribers(topic);
            stats.addSample(timer.elapsedUs());
        }

        double avgTime = stats.mean();

        if (i == 0) {
            exactTime = avgTime;
            reporter.printTableRow(tc.name + ": " + tc.pattern, avgTime, "µs");
        } else {
            double overhead = ((avgTime / exactTime) - 1.0) * 100.0;
            reporter.printTableRow(tc.name + ": " + tc.pattern, avgTime, "µs", overhead);
        }
    }

    reporter.printSubseparator();
    reporter.printSummary("Wildcard overhead analysis complete");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "          TOPICTREE ROUTING BENCHMARKS\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";

    benchmarkA_scalability();
    benchmarkB_naive_comparison();
    benchmarkC_depth_impact();
    benchmarkD_wildcards();

    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "✅ ALL BENCHMARKS COMPLETE\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << std::endl;

    return 0;
}
