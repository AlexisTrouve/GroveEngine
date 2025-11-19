#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <topictree/TopicTree.h>
#include <chrono>
#include <random>
#include <sstream>

// Helper to generate random topics
std::string generateRandomTopic(std::mt19937& rng, int segments) {
    std::uniform_int_distribution<> dist(1, 999);
    std::ostringstream oss;

    const char* prefixes[] = {"player", "enemy", "npc", "item", "quest", "world"};
    const char* middles[] = {"health", "position", "status", "stats", "inventory"};

    oss << prefixes[dist(rng) % 6];
    for (int i = 1; i < segments - 1; ++i) {
        oss << ":" << dist(rng);
    }
    oss << ":" << middles[dist(rng) % 5];

    return oss.str();
}

// Helper to generate wildcard pattern
std::string generateWildcardPattern(std::mt19937& rng, int segments, bool useMultiLevel) {
    std::uniform_int_distribution<> dist(0, 10);
    std::ostringstream oss;

    const char* prefixes[] = {"player", "enemy", "npc", "item", "quest", "world"};

    oss << prefixes[dist(rng) % 6];

    if (useMultiLevel && dist(rng) < 5) {
        oss << ":.*";
        return oss.str();
    }

    for (int i = 1; i < segments; ++i) {
        if (dist(rng) < 3) {
            oss << ":*";
        } else {
            oss << ":" << dist(rng);
        }
    }

    return oss.str();
}

TEST_CASE("Scenario 8: Performance Test", "[performance][benchmark]") {
    topictree::TopicTree<std::string> tree;
    std::mt19937 rng(42); // Fixed seed for reproducibility

    SECTION("Baseline: Register 1000 patterns") {
        for (int i = 0; i < 1000; ++i) {
            std::string pattern = generateWildcardPattern(rng, 3, i % 5 == 0);
            tree.registerSubscriber(pattern, "sub_" + std::to_string(i));
        }

        REQUIRE(tree.subscriberCount() >= 1000);
    }

    SECTION("Lookup performance with 1000 patterns") {
        // Register 1000 diverse patterns
        for (int i = 0; i < 1000; ++i) {
            std::string pattern = generateWildcardPattern(rng, 3, i % 5 == 0);
            tree.registerSubscriber(pattern, "sub_" + std::to_string(i));
        }

        // Generate test topics
        std::vector<std::string> testTopics;
        testTopics.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            testTopics.push_back(generateRandomTopic(rng, 3));
        }

        // Measure lookup time
        auto start = std::chrono::high_resolution_clock::now();

        for (const auto& topic : testTopics) {
            auto matches = tree.findSubscribers(topic);
            (void)matches; // Prevent optimization
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double avgMicroseconds = static_cast<double>(duration.count()) / testTopics.size();

        INFO("Average lookup time: " << avgMicroseconds << " μs");
        INFO("Total lookup time for 1000 topics: " << duration.count() / 1000.0 << " ms");

        // Success criteria: Average lookup < 1000 μs (1 ms)
        REQUIRE(avgMicroseconds < 1000.0);
    }

    SECTION("High subscriber density") {
        // 100 subscribers on same pattern
        for (int i = 0; i < 100; ++i) {
            tree.registerSubscriber("player:*:position", "sub_" + std::to_string(i));
        }

        auto matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 100);
    }

    SECTION("Deep topic performance (10 levels)") {
        tree.registerSubscriber("a:b:c:d:e:f:g:h:i:j", "exact");
        tree.registerSubscriber("a:*:c:*:e:*:g:*:i:*", "wildcards");
        tree.registerSubscriber("a:b:.*", "multi");

        std::string deepTopic = "a:b:c:d:e:f:g:h:i:j";

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 10000; ++i) {
            auto matches = tree.findSubscribers(deepTopic);
            (void)matches;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double avgMicroseconds = static_cast<double>(duration.count()) / 10000.0;

        INFO("Deep topic average lookup: " << avgMicroseconds << " μs");
        REQUIRE(avgMicroseconds < 100.0); // Even faster for repeated lookups
    }

    SECTION("Register/unregister performance") {
        std::vector<std::string> patterns;
        for (int i = 0; i < 1000; ++i) {
            patterns.push_back(generateWildcardPattern(rng, 3, false));
        }

        // Measure registration time
        auto start = std::chrono::high_resolution_clock::now();

        for (const auto& pattern : patterns) {
            tree.registerSubscriber(pattern, "sub");
        }

        auto regEnd = std::chrono::high_resolution_clock::now();

        // Measure unregistration time
        for (const auto& pattern : patterns) {
            tree.unregisterSubscriber(pattern, "sub");
        }

        auto unregEnd = std::chrono::high_resolution_clock::now();

        auto regDuration = std::chrono::duration_cast<std::chrono::microseconds>(regEnd - start);
        auto unregDuration = std::chrono::duration_cast<std::chrono::microseconds>(unregEnd - regEnd);

        INFO("Registration time: " << regDuration.count() / 1000.0 << " ms");
        INFO("Unregistration time: " << unregDuration.count() / 1000.0 << " ms");

        // Should be reasonably fast
        REQUIRE(regDuration.count() < 100000); // < 100ms for 1000 registrations
        REQUIRE(unregDuration.count() < 100000); // < 100ms for 1000 unregistrations
    }

    SECTION("Scalability test: 10000 patterns") {
        // Register 10000 patterns
        for (int i = 0; i < 10000; ++i) {
            std::string pattern = generateWildcardPattern(rng, 3, i % 10 == 0);
            tree.registerSubscriber(pattern, "sub_" + std::to_string(i));
        }

        // Test lookup still fast
        std::vector<std::string> testTopics;
        for (int i = 0; i < 100; ++i) {
            testTopics.push_back(generateRandomTopic(rng, 3));
        }

        auto start = std::chrono::high_resolution_clock::now();

        for (const auto& topic : testTopics) {
            auto matches = tree.findSubscribers(topic);
            (void)matches;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double avgMicroseconds = static_cast<double>(duration.count()) / testTopics.size();

        INFO("Average lookup with 10k patterns: " << avgMicroseconds << " μs");

        // Should still be under 5ms average even with 10k patterns
        REQUIRE(avgMicroseconds < 5000.0);
    }
}
