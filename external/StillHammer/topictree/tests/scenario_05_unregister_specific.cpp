#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <algorithm>

TEST_CASE("Scenario 5: Unregister Specific Pattern", "[unregister][specific]") {
    topictree::TopicTree<std::string> tree;

    SECTION("Unregister removes only specified pattern") {
        tree.registerSubscriber("player:*:health", "sub1");
        tree.registerSubscriber("player:.*", "sub2");

        // Both match initially
        auto matches = tree.findSubscribers("player:001:health");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "sub1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub2") != matches.end());

        // Unregister specific pattern
        tree.unregisterSubscriber("player:*:health", "sub1");

        // Only sub2 should match now
        matches = tree.findSubscribers("player:001:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");

        // Same for different ID
        matches = tree.findSubscribers("player:002:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");
    }

    SECTION("Unregister one subscriber doesn't affect others on same pattern") {
        tree.registerSubscriber("player:*:position", "sub1");
        tree.registerSubscriber("player:*:position", "sub2");
        tree.registerSubscriber("player:*:position", "sub3");

        auto matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 3);

        // Remove one subscriber
        tree.unregisterSubscriber("player:*:position", "sub2");

        matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "sub1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub3") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub2") == matches.end());
    }

    SECTION("Unregister non-existent pattern does nothing") {
        tree.registerSubscriber("player:*:health", "sub1");

        auto matches = tree.findSubscribers("player:001:health");
        REQUIRE(matches.size() == 1);

        // Try to unregister pattern that doesn't exist
        tree.unregisterSubscriber("enemy:*:health", "sub1");

        // Original pattern still works
        matches = tree.findSubscribers("player:001:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");
    }

    SECTION("Unregister then re-register same pattern") {
        tree.registerSubscriber("player:*:position", "sub1");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);

        tree.unregisterSubscriber("player:*:position", "sub1");
        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.empty());

        // Re-register
        tree.registerSubscriber("player:*:position", "sub1");
        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");
    }

    SECTION("Unregister exact pattern doesn't affect wildcard") {
        tree.registerSubscriber("player:001:position", "exact");
        tree.registerSubscriber("player:*:position", "wildcard");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 2);

        tree.unregisterSubscriber("player:001:position", "exact");

        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "wildcard");
    }
}
