#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <algorithm>

TEST_CASE("Scenario 2: Single Wildcard at Different Positions", "[wildcard][single]") {
    topictree::TopicTree<std::string> tree;

    SECTION("Wildcard in middle position") {
        tree.registerSubscriber("player:*:position", "sub1");

        auto matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");

        // Different player ID also matches
        matches = tree.findSubscribers("player:999:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");

        // Wrong number of segments doesn't match
        matches = tree.findSubscribers("player:position");
        REQUIRE(matches.empty());
    }

    SECTION("Wildcard at start position") {
        tree.registerSubscriber("*:001:health", "sub2");

        auto matches = tree.findSubscribers("player:001:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");

        matches = tree.findSubscribers("enemy:001:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");

        // Different ID doesn't match
        matches = tree.findSubscribers("player:002:health");
        REQUIRE(matches.empty());
    }

    SECTION("Multiple wildcards in same pattern") {
        tree.registerSubscriber("enemy:*:*", "sub3");

        auto matches = tree.findSubscribers("enemy:boss:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub3");

        matches = tree.findSubscribers("enemy:minion:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub3");

        // Wrong depth doesn't match
        matches = tree.findSubscribers("enemy:boss:stats:armor");
        REQUIRE(matches.empty());
    }

    SECTION("Combined test with multiple wildcard patterns") {
        tree.registerSubscriber("player:*:position", "sub1");
        tree.registerSubscriber("*:001:health", "sub2");
        tree.registerSubscriber("enemy:*:*", "sub3");

        auto matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");

        matches = tree.findSubscribers("player:001:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");

        matches = tree.findSubscribers("enemy:boss:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub3");

        // No wildcards match
        matches = tree.findSubscribers("player:999:health");
        REQUIRE(matches.empty());
    }
}
