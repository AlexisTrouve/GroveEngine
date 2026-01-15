#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <algorithm>

TEST_CASE("Scenario 1: Basic Exact Matching", "[basic][exact]") {
    topictree::TopicTree<std::string> tree;

    SECTION("Exact match returns subscriber") {
        tree.registerSubscriber("player:001:position", "subscriber1");

        auto matches = tree.findSubscribers("player:001:position");
        REQUIRE(matches.size() == 1);
        REQUIRE(std::find(matches.begin(), matches.end(), "subscriber1") != matches.end());
    }

    SECTION("Different ID does not match") {
        tree.registerSubscriber("player:001:position", "subscriber1");

        auto matches = tree.findSubscribers("player:002:position");
        REQUIRE(matches.empty());
    }

    SECTION("Different topic does not match") {
        tree.registerSubscriber("player:001:position", "subscriber1");

        auto matches = tree.findSubscribers("player:001:health");
        REQUIRE(matches.empty());
    }

    SECTION("Multiple exact patterns, different subscribers") {
        tree.registerSubscriber("player:001:position", "sub1");
        tree.registerSubscriber("player:002:position", "sub2");
        tree.registerSubscriber("enemy:001:health", "sub3");

        auto matches1 = tree.findSubscribers("player:001:position");
        REQUIRE(matches1.size() == 1);
        REQUIRE(matches1[0] == "sub1");

        auto matches2 = tree.findSubscribers("player:002:position");
        REQUIRE(matches2.size() == 1);
        REQUIRE(matches2[0] == "sub2");

        auto matches3 = tree.findSubscribers("enemy:001:health");
        REQUIRE(matches3.size() == 1);
        REQUIRE(matches3[0] == "sub3");
    }

    SECTION("Non-existent topic returns empty") {
        tree.registerSubscriber("player:001:position", "sub1");

        auto matches = tree.findSubscribers("nonexistent:topic:here");
        REQUIRE(matches.empty());
    }
}
