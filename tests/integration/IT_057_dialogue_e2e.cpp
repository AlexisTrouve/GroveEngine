/**
 * Integration Test IT_057: DialogueModule end-to-end — the VN/cutscene runtime (UI slice 7).
 *
 * Drives the module with real scene:* topics and asserts the PRESENTATION it publishes: the node
 * event stream (scene:node), the bound VN-screen data (ui:data:merge carries text + the choices
 * array), a branch taken by choice, and scene:end at a terminal node. This is the E2E lock — "no
 * E2E = it doesn't exist": the runtime logic is proven through the module + IIO, not just read.
 *
 * Script: intro -> decide -> {fight | flee} -> end. We advance the linear line, branch on a choice,
 * and confirm the runtime followed the picked edge and reported the terminal node.
 */

#include <catch2/catch_test_macros.hpp>
#include "DialogueModule.h"
#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>

using namespace grove;
using nlohmann::json;

static std::string uid(const std::string& p) {
    auto n = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream o; o << p << "_" << n; return o.str();
}

static json branchingScript() {
    return json{
        {"start", "intro"},
        {"nodes", {
            {"intro",  {{"speaker", "Vega"}, {"text", "We're under attack."}, {"goto", "decide"}}},
            {"decide", {{"speaker", "Vega"}, {"text", "Orders?"}, {"choices", json::array({
                            json{{"text", "Fight"}, {"goto", "fight"}},
                            json{{"text", "Flee"},  {"goto", "flee"}}})}}},
            {"fight",  {{"text", "The fleet engages."}, {"voice", "vo/fight.ogg"}, {"goto", "end"}}},
            {"flee",   {{"text", "We retreat."}, {"goto", "end"}}},
            {"end",    {{"text", "Silence."}}}
        }}
    };
}

namespace {
struct Harness {
    std::unique_ptr<DialogueModule> module;
    std::shared_ptr<IIO> moduleIO, pubIO, obsIO;

    // Captured presentation.
    std::string nodeId, nodeText, nodeSpeaker; bool nodeIsEnd = false; int nodeChoiceCount = -1;
    std::string endId; int endCount = 0;
    std::string voicePath; int voiceCount = 0;
    json sceneData;   // last ui:data:merge payload

    Harness() {
        auto& mgr = IntraIOManager::getInstance();
        moduleIO = mgr.createInstance(uid("dlg_mod"));
        pubIO    = mgr.createInstance(uid("dlg_pub"));
        obsIO    = mgr.createInstance(uid("dlg_obs"));

        module = std::make_unique<DialogueModule>();
        JsonDataNode cfg("config");
        module->setConfiguration(cfg, moduleIO.get(), nullptr);

        obsIO->subscribe("scene:node", [this](const Message& m) {
            nodeId = m.data->getString("id", ""); nodeText = m.data->getString("text", "");
            nodeSpeaker = m.data->getString("speaker", ""); nodeIsEnd = m.data->getBool("isEnd", false);
            nodeChoiceCount = m.data->getInt("choiceCount", -1);
        });
        obsIO->subscribe("scene:end", [this](const Message& m) { endId = m.data->getString("id", ""); ++endCount; });
        obsIO->subscribe("sound:sfx", [this](const Message& m) { voicePath = m.data->getString("path", ""); ++voiceCount; });
        obsIO->subscribe("ui:data:merge", [this](const Message& m) {
            if (auto* jn = dynamic_cast<const JsonDataNode*>(m.data.get())) sceneData = jn->getJsonData();
        });
    }

    void send(const std::string& topic, json payload) {
        pubIO->publish(topic, std::make_unique<JsonDataNode>("m", std::move(payload)));
        pump();
    }
    void pump() {
        JsonDataNode in("input");
        module->process(in);
        while (obsIO->hasMessages() > 0) obsIO->pullAndDispatch();
    }
};
} // namespace

TEST_CASE("IT_057: DialogueModule plays a branching script (advance / choose / end) — slice 7", "[integration][dialogue][e2e]") {
    Harness h;

    // --- Load: presents the start node immediately. ---
    h.send("scene:load", branchingScript());
    REQUIRE(h.nodeId == "intro");
    REQUIRE(h.nodeText == "We're under attack.");
    REQUIRE(h.nodeSpeaker == "Vega");
    REQUIRE_FALSE(h.nodeIsEnd);
    // The bound VN screen got the node data (nested choices array must ride in the json).
    REQUIRE(h.sceneData["scene"]["text"] == "We're under attack.");

    // --- Advance the linear line: intro -> decide (a choice node). ---
    h.send("scene:advance", json::object());
    REQUIRE(h.nodeId == "decide");
    REQUIRE(h.nodeChoiceCount == 2);
    REQUIRE(h.sceneData["scene"]["choices"].size() == 2);
    REQUIRE(h.sceneData["scene"]["choices"][0]["text"] == "Fight");
    REQUIRE(h.sceneData["scene"]["choices"][1]["goto"] == "flee");

    // --- Branch by choice: pick index 1 -> flee (NOT fight). ---
    h.send("scene:choose", json{{"index", 1}});
    REQUIRE(h.nodeId == "flee");
    REQUIRE(h.nodeText == "We retreat.");

    // --- Advance to the terminal node -> scene:end fires. ---
    h.send("scene:advance", json::object());
    REQUIRE(h.nodeId == "end");
    REQUIRE(h.nodeIsEnd);
    REQUIRE(h.endCount == 1);
    REQUIRE(h.endId == "end");
}

TEST_CASE("IT_057b: choose tolerates a STRING index (declarative UI event) + voice + goto-by-id", "[integration][dialogue][e2e]") {
    Harness h;
    h.send("scene:load", branchingScript());
    h.send("scene:advance", json::object());          // at decide

    // A declarative UI event can only carry strings — choose with index as a STRING must still work.
    h.send("scene:choose", json{{"index", "0"}});     // -> fight
    REQUIRE(h.nodeId == "fight");
    // fight has a voice line -> it was published on sound:sfx.
    REQUIRE(h.voiceCount == 1);
    REQUIRE(h.voicePath == "vo/fight.ogg");

    // scene:goto by id (how a choice button fires — a string arg) jumps directly.
    h.send("scene:goto", json{{"node", "flee"}});
    REQUIRE(h.nodeId == "flee");
}
