#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <algorithm>

TEST_CASE("Scenario 3: Multi-Level Wildcard Matching", "[wildcard][multilevel]") {
    topictree::TopicTree<std::string> tree;

    SECTION("Multi-level wildcard matches any depth") {
        tree.registerSubscriber("player:.*", "sub1");

        // Matches with 1 additional segment
        auto matches = tree.findSubscribers("player:001");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");

        // Matches with 2 additional segments
        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");

        // Matches with 3 additional segments
        matches = tree.findSubscribers("player:001:stats:armor");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");

        // Matches with many segments
        matches = tree.findSubscribers("player:001:inventory:weapons:sword:damage:fire");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");

        // Doesn't match different prefix
        matches = tree.findSubscribers("enemy:001");
        REQUIRE(matches.empty());
    }

    SECTION("Multiple multi-level patterns") {
        tree.registerSubscriber("player:.*", "sub1");
        tree.registerSubscriber("game:.*", "sub2");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub1");

        matches = tree.findSubscribers("game:level:01:start");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub2");

        matches = tree.findSubscribers("enemy:001");
        REQUIRE(matches.empty());
    }

    SECTION("Multi-level at root level") {
        tree.registerSubscriber(".*", "sub_all");

        // Should match everything
        auto matches = tree.findSubscribers("anything");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub_all");

        matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub_all");

        matches = tree.findSubscribers("very:deep:topic:hierarchy:here");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub_all");
    }

    SECTION("Multi-level after exact segments") {
        tree.registerSubscriber("game:world:region:.*", "sub1");

        auto matches = tree.findSubscribers("game:world:region:north");
        REQUIRE(matches.size() == 1);

        matches = tree.findSubscribers("game:world:region:north:zone:001");
        REQUIRE(matches.size() == 1);

        // Missing "region" segment
        matches = tree.findSubscribers("game:world:north");
        REQUIRE(matches.empty());
    }
}
