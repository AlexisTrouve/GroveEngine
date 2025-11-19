#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <algorithm>

TEST_CASE("Scenario 4: Overlapping Patterns", "[overlapping][multiple]") {
    topictree::TopicTree<std::string> tree;

    SECTION("Exact, single wildcard, and multi-level all match") {
        tree.registerSubscriber("player:001:position", "exactSub");
        tree.registerSubscriber("player:*:position", "wildcardSub");
        tree.registerSubscriber("player:.*", "multiSub");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 3);

        // Verify all three subscribers are present
        REQUIRE(std::find(matches.begin(), matches.end(), "exactSub") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "wildcardSub") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "multiSub") != matches.end());
    }

    SECTION("Only wildcard patterns match when exact doesn't") {
        tree.registerSubscriber("player:001:position", "exactSub");
        tree.registerSubscriber("player:*:position", "wildcardSub");
        tree.registerSubscriber("player:.*", "multiSub");

        auto matches = tree.findSubscribers("player:002:position");
        REQUIRE(matches.size() == 2);

        REQUIRE(std::find(matches.begin(), matches.end(), "exactSub") == matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "wildcardSub") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "multiSub") != matches.end());
    }

    SECTION("Only multi-level wildcard matches deeper topics") {
        tree.registerSubscriber("player:001:position", "exactSub");
        tree.registerSubscriber("player:*:position", "wildcardSub");
        tree.registerSubscriber("player:.*", "multiSub");

        auto matches = tree.findSubscribers("player:001:health");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "multiSub");
    }

    SECTION("Multiple subscribers on same pattern") {
        tree.registerSubscriber("player:*:position", "sub1");
        tree.registerSubscriber("player:*:position", "sub2");
        tree.registerSubscriber("player:*:position", "sub3");

        auto matches = tree.findSubscribers("player:123:position");
        REQUIRE(matches.size() == 3);

        REQUIRE(std::find(matches.begin(), matches.end(), "sub1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub2") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub3") != matches.end());
    }

    SECTION("Complex overlapping scenario") {
        tree.registerSubscriber("game:*:start", "sub1");
        tree.registerSubscriber("game:level:*", "sub2");
        tree.registerSubscriber("*:level:start", "sub3");
        tree.registerSubscriber("game:.*", "sub4");

        auto matches = tree.findSubscribers("game:level:start");
        REQUIRE(matches.size() == 4);

        // All four patterns should match
        REQUIRE(std::find(matches.begin(), matches.end(), "sub1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub2") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub3") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub4") != matches.end());
    }
}
