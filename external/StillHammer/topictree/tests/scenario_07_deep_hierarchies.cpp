#include <catch2/catch_test_macros.hpp>
#include <topictree/TopicTree.h>
#include <algorithm>

TEST_CASE("Scenario 7: Deep Topic Hierarchies", "[deep][hierarchy]") {
    topictree::TopicTree<std::string> tree;

    SECTION("Deep topic with multiple wildcards") {
        tree.registerSubscriber("game:world:region:zone:*:entity:player", "sub1");
        tree.registerSubscriber("game:world:*:zone:*:entity:player", "sub2");
        tree.registerSubscriber("game:.*", "sub3");

        // All three patterns match
        auto matches = tree.findSubscribers("game:world:region:zone:001:entity:player");
        REQUIRE(matches.size() == 3);
        REQUIRE(std::find(matches.begin(), matches.end(), "sub1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub2") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub3") != matches.end());

        // sub1 doesn't match (wrong region name)
        matches = tree.findSubscribers("game:world:north:zone:002:entity:player");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "sub2") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "sub3") != matches.end());

        // Only sub3 matches (different entity type)
        matches = tree.findSubscribers("game:world:region:area:001:entity:npc");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "sub3");
    }

    SECTION("Very deep hierarchy (15+ levels)") {
        tree.registerSubscriber("a:b:c:d:e:f:g:h:i:j:k:l:m:n:o", "exact");
        tree.registerSubscriber("a:b:c:d:e:f:g:h:*:j:k:l:m:n:o", "wildcard");
        tree.registerSubscriber("a:b:c:.*", "multi");

        auto matches = tree.findSubscribers("a:b:c:d:e:f:g:h:i:j:k:l:m:n:o");
        REQUIRE(matches.size() == 3);
        REQUIRE(std::find(matches.begin(), matches.end(), "exact") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "wildcard") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "multi") != matches.end());

        // Different middle segment
        matches = tree.findSubscribers("a:b:c:d:e:f:g:h:X:j:k:l:m:n:o");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "wildcard") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "multi") != matches.end());
    }

    SECTION("Nested wildcards at multiple depths") {
        tree.registerSubscriber("*:*:*:*:end", "all_wildcards");
        tree.registerSubscriber("a:*:c:*:end", "partial_wildcards");
        tree.registerSubscriber("a:b:c:d:end", "exact");

        auto matches = tree.findSubscribers("a:b:c:d:end");
        REQUIRE(matches.size() == 3);

        matches = tree.findSubscribers("a:X:c:Y:end");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "all_wildcards") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "partial_wildcards") != matches.end());

        matches = tree.findSubscribers("X:Y:Z:W:end");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "all_wildcards");
    }

    SECTION("Multi-level wildcard at various depths") {
        tree.registerSubscriber("level1:.*", "depth1");
        tree.registerSubscriber("level1:level2:.*", "depth2");
        tree.registerSubscriber("level1:level2:level3:.*", "depth3");

        auto matches = tree.findSubscribers("level1:anything");
        REQUIRE(matches.size() == 1);
        REQUIRE(matches[0] == "depth1");

        matches = tree.findSubscribers("level1:level2:anything");
        REQUIRE(matches.size() == 2);
        REQUIRE(std::find(matches.begin(), matches.end(), "depth1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "depth2") != matches.end());

        matches = tree.findSubscribers("level1:level2:level3:anything");
        REQUIRE(matches.size() == 3);
        REQUIRE(std::find(matches.begin(), matches.end(), "depth1") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "depth2") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "depth3") != matches.end());

        matches = tree.findSubscribers("level1:level2:level3:level4:level5");
        REQUIRE(matches.size() == 3);
    }

    SECTION("Complex real-world scenario") {
        // Game world hierarchy
        tree.registerSubscriber("mmo:server:*:world:*:zone:*:entity:player:*:status", "player_status");
        tree.registerSubscriber("mmo:server:*:world:*:.*", "world_events");
        tree.registerSubscriber("mmo:.*", "all_mmo");

        auto matches = tree.findSubscribers("mmo:server:eu1:world:azeroth:zone:elwynn:entity:player:123:status");
        REQUIRE(matches.size() == 3);

        matches = tree.findSubscribers("mmo:server:us1:world:kalimdor:zone:barrens:entity:npc:456:spawn");
        REQUIRE(matches.size() == 2); // world_events and all_mmo
        REQUIRE(std::find(matches.begin(), matches.end(), "world_events") != matches.end());
        REQUIRE(std::find(matches.begin(), matches.end(), "all_mmo") != matches.end());
    }
}
