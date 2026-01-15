#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <algorithm>

TEST_CASE("Scenario 6: Unregister All Patterns for Subscriber", "[unregister][all]") {
    topictree::TopicTree<std::string> tree;

    SECTION("UnregisterAll removes subscriber from all patterns") {
        tree.registerSubscriber("player:*:position", "sub1");
        tree.registerSubscriber("enemy:*:health", "sub1");
        tree.registerSubscriber("game:.*", "sub1");
        tree.registerSubscriber("player:*:position", "sub2");

        // sub1 matches on player pattern
        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "sub1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub2") != matches.end());

        // UnregisterAll for sub1
        tree.unregisterSubscriberAll("sub1");

        // Only sub2 should match now
        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");

        // sub1 removed from enemy pattern too
        matches = tree.findSubscribers("enemy:boss:health");
        REQUIRE(matches.empty());

        // sub1 removed from game pattern too
        matches = tree.findSubscribers("game:level:01");
        REQUIRE(matches.empty());
    }

    SECTION("UnregisterAll doesn't affect other subscribers") {
        tree.registerSubscriber("player:*:position", "sub1");
        tree.registerSubscriber("player:*:position", "sub2");
        tree.registerSubscriber("player:*:position", "sub3");

        auto matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 3);

        tree.unregisterSubscriberAll("sub2");

        matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "sub1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub3") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub2") == matches.end());
    }

    SECTION("UnregisterAll on non-existent subscriber does nothing") {
        tree.registerSubscriber("player:*:position", "sub1");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);

        // Try to unregister non-existent subscriber
        tree.unregisterSubscriberAll("non_existent");

        // Original subscriber still works
        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");
    }

    SECTION("Clear removes everything") {
        tree.registerSubscriber("player:*:position", "sub1");
        tree.registerSubscriber("enemy:*:health", "sub2");
        tree.registerSubscriber("game:.*", "sub3");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(!matches.empty());

        tree.clear();

        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.empty());

        matches = tree.findSubscribers("enemy:001:health");
        REQUIRE(matches.empty());

        matches = tree.findSubscribers("game:anything");
        REQUIRE(matches.empty());
    }

    SECTION("Tree can be reused after clear") {
        tree.registerSubscriber("player:*:position", "sub1");
        tree.clear();

        tree.registerSubscriber("enemy:*:health", "sub2");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.empty());

        matches = tree.findSubscribers("enemy:001:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");
    }
}
