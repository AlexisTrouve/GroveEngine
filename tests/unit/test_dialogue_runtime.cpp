/**
 * DialogueRuntimeUnit — pure oracle for the VN/cutscene state machine (UI framework slice 7).
 *
 * No IIO / renderer: asserts the load-bearing graph logic directly — parse, start, linear advance,
 * branch-by-choice, jump-by-id, terminal detection, and the failure modes (bad choice index /
 * unknown id leave the state UNCHANGED). The E2E (IT_057) proves it end-to-end through the module;
 * this locks the primitives so a regression points at the exact broken method.
 */

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "DialogueRuntime.h"

using namespace grove::dialogue;
using nlohmann::json;

// A small branching script: intro -> decide -> {fight | flee} -> end.
static json sampleScript() {
    return json{
        {"start", "intro"},
        {"nodes", {
            {"intro",  {{"speaker", "Vega"}, {"text", "We're under attack."}, {"goto", "decide"}}},
            {"decide", {{"speaker", "Vega"}, {"text", "Orders?"}, {"choices", json::array({
                            json{{"text", "Fight"}, {"goto", "fight"}},
                            json{{"text", "Flee"},  {"goto", "flee"}}})}}},
            {"fight",  {{"text", "The fleet engages."}, {"voice", "vo/fight.ogg"}, {"goto", "end"}}},
            {"flee",   {{"text", "We retreat."}, {"goto", "end"}}},
            {"end",    {{"text", "..."}}}
        }}
    };
}

TEST_CASE("DialogueRuntimeUnit: parse + start lands on the start node", "[dialogue][unit]") {
    DialogueRuntime rt;
    REQUIRE(rt.loadFromJson(sampleScript()));
    REQUIRE(rt.nodeCount() == 5);
    REQUIRE(rt.start());
    REQUIRE(rt.currentId() == "intro");
    REQUIRE(rt.current() != nullptr);
    REQUIRE(rt.current()->speaker == "Vega");
    REQUIRE(rt.current()->text == "We're under attack.");
    REQUIRE_FALSE(rt.isEnd());
}

TEST_CASE("DialogueRuntimeUnit: linear advance follows goto", "[dialogue][unit]") {
    DialogueRuntime rt; rt.loadFromJson(sampleScript()); rt.start();
    REQUIRE(rt.advance());                 // intro -> decide
    REQUIRE(rt.currentId() == "decide");
    REQUIRE(rt.current()->choices.size() == 2);
    // A choice node has no linear next -> advance() must fail and NOT move.
    REQUIRE_FALSE(rt.advance());
    REQUIRE(rt.currentId() == "decide");
}

TEST_CASE("DialogueRuntimeUnit: choose branches to the picked target", "[dialogue][unit]") {
    SECTION("choice 0 -> fight") {
        DialogueRuntime rt; rt.loadFromJson(sampleScript()); rt.start(); rt.advance();  // at decide
        REQUIRE(rt.choose(0));
        REQUIRE(rt.currentId() == "fight");
        REQUIRE(rt.current()->voice == "vo/fight.ogg");
    }
    SECTION("choice 1 -> flee") {
        DialogueRuntime rt; rt.loadFromJson(sampleScript()); rt.start(); rt.advance();  // at decide
        REQUIRE(rt.choose(1));
        REQUIRE(rt.currentId() == "flee");
    }
}

TEST_CASE("DialogueRuntimeUnit: reaching a terminal node is isEnd", "[dialogue][unit]") {
    DialogueRuntime rt; rt.loadFromJson(sampleScript()); rt.start(); rt.advance(); rt.choose(0);  // fight
    REQUIRE(rt.advance());                 // fight -> end
    REQUIRE(rt.currentId() == "end");
    REQUIRE(rt.isEnd());
    REQUIRE_FALSE(rt.advance());            // terminal: no move
}

TEST_CASE("DialogueRuntimeUnit: goToNode by id (how the UI takes a choice)", "[dialogue][unit]") {
    DialogueRuntime rt; rt.loadFromJson(sampleScript()); rt.start();
    REQUIRE(rt.goToNode("flee"));
    REQUIRE(rt.currentId() == "flee");
}

TEST_CASE("DialogueRuntimeUnit: bad moves leave the state unchanged", "[dialogue][unit]") {
    DialogueRuntime rt; rt.loadFromJson(sampleScript()); rt.start(); rt.advance();  // at decide
    REQUIRE_FALSE(rt.choose(5));           // out of range
    REQUIRE(rt.currentId() == "decide");
    REQUIRE_FALSE(rt.choose(-1));
    REQUIRE(rt.currentId() == "decide");
    REQUIRE_FALSE(rt.goToNode("nowhere")); // unknown id
    REQUIRE(rt.currentId() == "decide");
}

TEST_CASE("DialogueRuntimeUnit: no nodes -> load fails", "[dialogue][unit]") {
    DialogueRuntime rt;
    REQUIRE_FALSE(rt.loadFromJson(json{{"start", "x"}}));   // no `nodes`
    REQUIRE_FALSE(rt.start());
}

// A script exercising conditions: gated choices, on-entry `set`, a conditional `branch`, and vars.
static json condScript() {
    return json{
        {"vars", {{"gold", 0}, {"hasKey", false}}},
        {"start", "gate"},
        {"nodes", {
            {"gate", {{"text", "A locked door."}, {"choices", json::array({
                json{{"text", "Open"},  {"goto", "opened"}, {"when", json::array({ json{{"var","hasKey"}} })}},
                json{{"text", "Pick"},  {"goto", "picked"}, {"when", json::array({ json{{"var","gold"},{"op","ge"},{"value",5}} })}},
                json{{"text", "Leave"}, {"goto", "left"}}})}}},
            {"opened", {{"text", "Open."}}},
            {"picked", {{"text", "Picked."}}},
            {"left",   {{"text", "Left."}}},
            {"getkey", {{"text", "A key!"}, {"set", {{"hasKey", true}}}, {"goto", "gate"}}},
            {"fork",   {{"text", "A fork."}, {"branch", json::array({
                json{{"when", json::array({ json{{"var","hasKey"}} })}, {"goto", "opened"}},
                json{{"goto", "left"}}})}}}
        }}
    };
}

TEST_CASE("DialogueRuntimeUnit: choice `when` gates the OFFERED choices", "[dialogue][unit][conditions]") {
    DialogueRuntime rt; rt.loadFromJson(condScript()); rt.start();   // at gate; gold 0, hasKey false
    REQUIRE(rt.availableChoices().size() == 1);          // only "Leave" (Open/Pick gated out)
    REQUIRE(rt.availableChoices()[0].target == "left");

    rt.setVar("gold", 5);
    REQUIRE(rt.availableChoices().size() == 2);           // Pick (gold>=5) now offered + Leave

    rt.setVar("hasKey", true);
    REQUIRE(rt.availableChoices().size() == 3);           // Open + Pick + Leave
    REQUIRE(rt.availableChoices()[0].target == "opened"); // in declaration order
}

TEST_CASE("DialogueRuntimeUnit: node `set` assigns vars on entry", "[dialogue][unit][conditions]") {
    DialogueRuntime rt; rt.loadFromJson(condScript()); rt.start();
    REQUIRE(rt.getVar("hasKey") == false);
    REQUIRE(rt.goToNode("getkey"));            // entering getkey applies set {hasKey:true}
    REQUIRE(rt.getVar("hasKey") == true);
    REQUIRE(rt.advance());                     // getkey -> gate
    REQUIRE(rt.currentId() == "gate");
    REQUIRE(rt.availableChoices().size() == 2);   // Open now offered (hasKey) + Leave
}

TEST_CASE("DialogueRuntimeUnit: conditional `branch` picks the first matching goto", "[dialogue][unit][conditions]") {
    SECTION("gate closed -> fallthrough branch") {
        DialogueRuntime rt; rt.loadFromJson(condScript()); rt.goToNode("fork");   // hasKey false
        REQUIRE(rt.advance());
        REQUIRE(rt.currentId() == "left");     // first branch (hasKey) skipped -> default -> left
    }
    SECTION("gate open -> first branch") {
        DialogueRuntime rt; rt.loadFromJson(condScript());
        rt.setVar("hasKey", true);
        rt.goToNode("fork");
        REQUIRE(rt.advance());
        REQUIRE(rt.currentId() == "opened");   // first branch (hasKey) taken
    }
}

TEST_CASE("DialogueRuntimeUnit: choose() indexes the AVAILABLE (gated) list", "[dialogue][unit][conditions]") {
    DialogueRuntime rt; rt.loadFromJson(condScript()); rt.start();   // only "Leave" available
    REQUIRE(rt.choose(0));
    REQUIRE(rt.currentId() == "left");         // index 0 of the offered list = Leave, not the raw choice 0
    // Out of range against the (size-1) available list.
    DialogueRuntime rt2; rt2.loadFromJson(condScript()); rt2.start();
    REQUIRE_FALSE(rt2.choose(1));
    REQUIRE(rt2.currentId() == "gate");
}

TEST_CASE("DialogueRuntimeUnit: absent start falls back to the first node in map order", "[dialogue][unit]") {
    json s = {{"nodes", {
        {"aStart", {{"text", "first"}, {"goto", "zEnd"}}},
        {"zEnd",   {{"text", "second"}}}
    }}};
    DialogueRuntime rt;
    REQUIRE(rt.loadFromJson(s));
    REQUIRE(rt.start());
    // No explicit "start" -> the first key in nlohmann's (sorted) map order = "aStart".
    REQUIRE(rt.currentId() == "aStart");
}
