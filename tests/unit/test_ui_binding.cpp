/**
 * UIBindingUnit — pure oracle for the JSON-UI binding socle (grove::uibind): the {{path}} resolver +
 * scope chain that BOTH data-binding (in) and the event system (out) sit on. No widgets, no IIO.
 */

#include <catch2/catch_test_macros.hpp>
#include "Core/UIBinding.h"

using namespace grove::uibind;

TEST_CASE("UIBindingUnit: resolvePath walks objects + array indices", "[ui][binding][unit]") {
    json data = {
        {"credits", 1240},
        {"ship", {{"name", "Aurora"}, {"hp", 0.8}}},
        {"fleet", json::array({ {{"name", "Aurora"}}, {{"name", "Borealis"}} })}
    };
    Scope root{&data, nullptr};

    REQUIRE(*resolvePath(root, "credits") == 1240);
    REQUIRE(*resolvePath(root, "ship.name") == "Aurora");
    REQUIRE(resolvePath(root, "ship.hp")->get<double>() == 0.8);
    REQUIRE(*resolvePath(root, "fleet.0.name") == "Aurora");
    REQUIRE(*resolvePath(root, "fleet.1.name") == "Borealis");

    REQUIRE(resolvePath(root, "missing") == nullptr);
    REQUIRE(resolvePath(root, "ship.missing") == nullptr);
    REQUIRE(resolvePath(root, "fleet.9.name") == nullptr);    // out-of-range index
    REQUIRE(resolvePath(root, "credits.x") == nullptr);       // can't descend into a scalar
}

TEST_CASE("UIBindingUnit: interpolate substitutes + keeps literal text", "[ui][binding][unit]") {
    json data = { {"credits", 1240}, {"name", "Aurora"}, {"hp", 0.8}, {"alive", true} };
    Scope s{&data, nullptr};

    REQUIRE(interpolate(s, "Credits: {{credits}}") == "Credits: 1240");
    REQUIRE(interpolate(s, "{{name}} - {{hp}}") == "Aurora - 0.8");
    REQUIRE(interpolate(s, "alive={{alive}}") == "alive=true");
    REQUIRE(interpolate(s, "no placeholders") == "no placeholders");   // verbatim
    REQUIRE(interpolate(s, "{{missing}}!") == "!");                     // missing -> empty
    REQUIRE(interpolate(s, "{{ name }}") == "Aurora");                  // whitespace trimmed
    REQUIRE(interpolate(s, "unclosed {{name") == "unclosed {{name");    // malformed -> literal
}

TEST_CASE("UIBindingUnit: hasBindings", "[ui][binding][unit]") {
    REQUIRE(hasBindings("{{x}}"));
    REQUIRE(hasBindings("a {{x}} b"));
    REQUIRE_FALSE(hasBindings("plain"));
    REQUIRE_FALSE(hasBindings("{ not a binding }"));
}

TEST_CASE("UIBindingUnit: typed resolution for non-string props", "[ui][binding][unit]") {
    json data = { {"hp", 0.8}, {"count", 5}, {"flag", true}, {"label", "7"} };
    Scope s{&data, nullptr};

    REQUIRE(resolveNumber(s, "{{hp}}") == 0.8);
    REQUIRE(resolveNumber(s, "{{count}}") == 5.0);
    REQUIRE(resolveNumber(s, "{{label}}") == 7.0);        // numeric string coerces
    REQUIRE(resolveNumber(s, "{{missing}}", 42.0) == 42.0);
    REQUIRE(resolveNumber(s, "13") == 13.0);              // literal number prop

    REQUIRE(resolveBool(s, "{{flag}}") == true);
    REQUIRE(resolveBool(s, "{{hp}}") == true);            // non-zero number -> true
    REQUIRE(resolveBool(s, "{{missing}}", true) == true);
    REQUIRE(resolveBool(s, "true") == true);              // literal
    REQUIRE(resolveBool(s, "false") == false);
}

TEST_CASE("UIBindingUnit: setAtPath writes deep paths (the write side)", "[ui][binding][unit]") {
    SECTION("top-level key") {
        json d = json::object();
        REQUIRE(setAtPath(d, "credits", 1300));
        REQUIRE(d["credits"] == 1300);
    }
    SECTION("nested — creates intermediate objects") {
        json d = json::object();
        REQUIRE(setAtPath(d, "ship.hp", 0.5));
        REQUIRE(d["ship"]["hp"] == 0.5);
    }
    SECTION("existing array element") {
        json d = { {"fleet", json::array({ {{"hp", 1.0}}, {{"hp", 1.0}} })} };
        REQUIRE(setAtPath(d, "fleet.1.hp", 0.3));
        REQUIRE(d["fleet"][1]["hp"] == 0.3);
        REQUIRE(d["fleet"][0]["hp"] == 1.0);     // untouched
    }
    SECTION("empty path replaces the whole context") {
        json d = { {"a", 1} };
        REQUIRE(setAtPath(d, "", json{{"b", 2}}));
        REQUIRE(d == json{{"b", 2}});
    }
    SECTION("type change + scalar intermediate coercion") {
        json d = { {"x", "str"} };
        REQUIRE(setAtPath(d, "x", 5));           // string -> number
        REQUIRE(d["x"] == 5);
        REQUIRE(setAtPath(d, "x.y", 1));         // scalar intermediate coerced to object
        REQUIRE(d["x"] == json{{"y", 1}});
    }
    SECTION("malformed path / mid-path array out of range -> false") {
        json d = { {"fleet", json::array()} };
        REQUIRE_FALSE(setAtPath(d, "a..b", 1));          // empty segment
        REQUIRE_FALSE(setAtPath(d, "fleet.0.hp", 1));    // index into an empty array, mid-path
    }
}

TEST_CASE("UIBindingUnit: scope chain — local / $parent / $root", "[ui][binding][unit]") {
    json rootData = { {"credits", 1240}, {"activeFleet", "alpha"} };
    json itemData = { {"name", "Aurora"}, {"id", "ship-7a3"} };
    Scope root{&rootData, nullptr};
    Scope item{&itemData, &root};        // a repeater item whose parent is the root

    // Plain path resolves in the LOCAL (item) scope.
    REQUIRE(interpolate(item, "{{name}}") == "Aurora");
    REQUIRE(interpolate(item, "{{id}}") == "ship-7a3");
    REQUIRE(interpolate(item, "{{credits}}") == "");      // not in the item scope, plain path stays local

    // $root / $parent climb the chain.
    REQUIRE(interpolate(item, "{{$root.credits}}") == "1240");
    REQUIRE(interpolate(item, "{{$parent.activeFleet}}") == "alpha");
    REQUIRE(interpolate(item, "{{name}} of {{$root.activeFleet}}") == "Aurora of alpha");
}
