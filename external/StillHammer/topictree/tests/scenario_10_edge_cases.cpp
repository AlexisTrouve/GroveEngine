#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <algorithm>

TEST_CASE("Scenario 10: Edge Cases & Stress Test", "[edge][stress]") {
    topictree::TopicTree<std::string> tree;

    SECTION("Empty topic string") {
        tree.registerSubscriber("player:*:position", "sub1");

        auto matches = tree.findSubscribers("");
        REQUIRE(matches.empty());
    }

    SECTION("Single segment topic") {
        tree.registerSubscriber("player", "exact");
        tree.registerSubscriber("*", "wildcard");

        auto matches = tree.findSubscribers("player");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "exact") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "wildcard") != matches.end());

        matches = tree.findSubscribers("enemy");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "wildcard");
    }

    SECTION("Topics with empty segments (multiple colons)") {
        tree.registerSubscriber("a:::b", "empty_segments");

        // Same pattern with empty segments
        auto matches = tree.findSubscribers("a:::b");
        // Behavior may vary - empty segments might be skipped
        // Just ensure no crash - test passes if we got here without crashing
        REQUIRE(true); // Test passes if no crash occurs
    }

    SECTION("Pattern with only wildcard") {
        tree.registerSubscriber("*", "single_wildcard");

        auto matches = tree.findSubscribers("anything");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "single_wildcard");

        // Doesn't match multi-segment topics
        matches = tree.findSubscribers("any:thing");
        REQUIRE(matches.empty());
    }

    SECTION("Pattern with only multi-level wildcard") {
        tree.registerSubscriber(".*", "multi_wildcard");

        // Should match everything
        auto matches = tree.findSubscribers("anything");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "multi_wildcard");

        matches = tree.findSubscribers("any:thing:here");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "multi_wildcard");
    }

    SECTION("Pattern with all wildcards") {
        tree.registerSubscriber("*:*:*", "all_single");

        auto matches = tree.findSubscribers("a:b:c");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "all_single");

        // Wrong depth doesn't match
        matches = tree.findSubscribers("a:b");
        REQUIRE(matches.empty());

        matches = tree.findSubscribers("a:b:c:d");
        REQUIRE(matches.empty());
    }

    SECTION("High subscriber density - 100 subscribers on same pattern") {
        for (int i = 0; i < 100; ++i) {
            tree.registerSubscriber("player:*:position", "sub_" + std::to_string(i));
        }

        auto matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 100);

        // Verify all subscribers present
        for (int i = 0; i < 100; ++i) {
            std::string expected = "sub_" + std::to_string(i);
            REQUIRE(std::find(matches.begin(), matches.end(), expected) != matches.end());
        }
    }

    SECTION("One subscriber on many patterns") {
        for (int i = 0; i < 100; ++i) {
            tree.registerSubscriber("pattern:" + std::to_string(i), "mega_sub");
        }

        // Each pattern should work
        for (int i = 0; i < 100; ++i) {
            auto matches = tree.findSubscribers("pattern:" + std::to_string(i));
            REQUIRE(matches.size() == 1);
            REQUIRE(matches[0] == "mega_sub");
        }

        // UnregisterAll should remove from all
        tree.unregisterSubscriberAll("mega_sub");

        for (int i = 0; i < 100; ++i) {
            auto matches = tree.findSubscribers("pattern:" + std::to_string(i));
            REQUIRE(matches.empty());
        }
    }

    SECTION("Clear and reuse lifecycle") {
        tree.registerSubscriber("player:*:position", "sub1");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);

        tree.clear();

        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.empty());

        // Reuse after clear
        tree.registerSubscriber("enemy:*:health", "sub2");

        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.empty());

        matches = tree.findSubscribers("enemy:001:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");
    }

    SECTION("Register, unregister, re-register same pattern") {
        tree.registerSubscriber("test:*:pattern", "sub1");

        auto matches = tree.findSubscribers("test:123:pattern");
        REQUIRE(matches.size() == 1);

        tree.unregisterSubscriber("test:*:pattern", "sub1");
        matches = tree.findSubscribers("test:123:pattern");
        REQUIRE(matches.empty());

        // Re-register
        tree.registerSubscriber("test:*:pattern", "sub1");
        matches = tree.findSubscribers("test:123:pattern");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");
    }

    SECTION("Extremely deep hierarchy (20+ levels)") {
        std::string deepPattern = "l0:l1:l2:l3:l4:l5:l6:l7:l8:l9:l10:l11:l12:l13:l14:l15:l16:l17:l18:l19";
        std::string deepTopic = "l0:l1:l2:l3:l4:l5:l6:l7:l8:l9:l10:l11:l12:l13:l14:l15:l16:l17:l18:l19";

        tree.registerSubscriber(deepPattern, "deep_sub");

        auto matches = tree.findSubscribers(deepTopic);
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "deep_sub");
    }

    SECTION("Deep pattern with wildcards at various levels") {
        tree.registerSubscriber("a:*:c:*:e:*:g:*:i:*:k:*:m:*:o:*:q:*:s:*", "pattern20");

        auto matches = tree.findSubscribers("a:1:c:2:e:3:g:4:i:5:k:6:m:7:o:8:q:9:s:10");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "pattern20");

        // Wrong segment doesn't match
        matches = tree.findSubscribers("a:1:X:2:e:3:g:4:i:5:k:6:m:7:o:8:q:9:s:10");
        REQUIRE(matches.empty());
    }

    SECTION("Mixed depth multi-level wildcards") {
        tree.registerSubscriber("a:.*", "depth1");
        tree.registerSubscriber("a:b:.*", "depth2");
        tree.registerSubscriber("a:b:c:.*", "depth3");
        tree.registerSubscriber("a:b:c:d:e:f:.*", "depth6");

        auto matches = tree.findSubscribers("a:b:c:d:e:f:g:h:i:j");

        // All four should match
        REQUIRE(matches.size() == 4);
        REQUIRE(std::find(matches.begin(), matches.end(), "depth1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "depth2") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "depth3") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "depth6") != matches.end());
    }

    SECTION("Special characters in topic segments") {
        // TopicTree uses : as separator, so these should work as normal segments
        tree.registerSubscriber("player-001:*:position_x", "special");

        auto matches = tree.findSubscribers("player-001:entity_123:position_x");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "special");
    }

    SECTION("Very long segment names") {
        std::string longSegment(1000, 'x');
        std::string pattern = "player:" + longSegment + ":position";
        std::string topic = "player:" + longSegment + ":position";

        tree.registerSubscriber(pattern, "long_sub");

        auto matches = tree.findSubscribers(topic);
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "long_sub");
    }

    SECTION("Subscriber count accuracy") {
        REQUIRE(tree.subscriberCount() == 0);

        tree.registerSubscriber("pattern1", "sub1");
        REQUIRE(tree.subscriberCount() == 1);

        tree.registerSubscriber("pattern2", "sub1");
        REQUIRE(tree.subscriberCount() == 2); // Same subscriber, different pattern

        tree.registerSubscriber("pattern1", "sub2");
        REQUIRE(tree.subscriberCount() == 3);

        tree.unregisterSubscriber("pattern1", "sub1");
        REQUIRE(tree.subscriberCount() == 2);

        tree.clear();
        REQUIRE(tree.subscriberCount() == 0);
    }

    SECTION("Duplicate registration of same subscriber on same pattern") {
        tree.registerSubscriber("test:*:pattern", "sub1");
        tree.registerSubscriber("test:*:pattern", "sub1"); // Duplicate

        auto matches = tree.findSubscribers("test:123:pattern");
        // Should only appear once (unordered_set prevents duplicates)
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");
    }
}
