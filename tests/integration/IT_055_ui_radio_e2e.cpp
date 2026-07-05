/**
 * Integration Test IT_055: an audio/radio player UI drives the sound engine (UI slice 6b).
 *
 * WHAT  : A radio-player layout (Play / Stop buttons + a "now playing" label + a progress bar) built
 *         entirely from EXISTING widgets + the declarative binding engine. Clicking Play publishes the
 *         real engine topic `sound:music`; clicking Stop publishes `sound:music:stop`; the now-playing
 *         label reflects pushed `{{radio.title}}` data.
 * WHY   : This is the "radio player" half of slice 6b — proof that the player composes from the
 *         framework with ZERO new widget, and that a UI click reaches the SoundManager's topic contract
 *         (no test previously fired a sound:* topic from a declarative UI event). The live progress bar
 *         is fed by the engine's new sound:music:position (locked headless in SoundManagerUnit
 *         [position]); here we lock the CONTROLS + the now-playing display. "No E2E = it doesn't exist."
 * HOW   : Mirrors IT_037 (declarative events out + binding in). Observe sound:music / sound:music:stop;
 *         click each button; push ui:data and assert the label re-rendered the bound title.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <vector>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_055: a radio-player UI publishes sound:* on click + reflects now-playing data (slice 6b)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("radio_host");
    auto uiIO     = mgr.createInstance("radio_ui");
    auto observer = mgr.createInstance("radio_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "radio_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_radio.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    // Observe the real engine topics the buttons should drive + the label's rendered text.
    std::string playedPath; bool played = false, stopped = false;
    std::vector<std::string> renderedTexts;
    observer->subscribe("sound:music", [&](const Message& m) { played = true; playedPath = m.data->getString("path", ""); });
    observer->subscribe("sound:music:stop", [&](const Message& /*m*/) { stopped = true; });
    auto captureText = [&](const Message& m) { renderedTexts.push_back(m.data->getString("text", "")); };
    observer->subscribe("render:text:add", captureText);
    observer->subscribe("render:text:update", captureText);

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sawText = [&](const std::string& t) {
        return std::find(renderedTexts.begin(), renderedTexts.end(), t) != renderedTexts.end();
    };
    auto move = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d)); pump();
    };
    auto click = [&](double x, double y) {
        move(x, y);
        { auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", true);  hostPub->publish("input:mouse:button", std::move(d)); pump(); }
        { auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", false); hostPub->publish("input:mouse:button", std::move(d)); pump(); }
    };

    pump();  // settle

    // --- Now-playing display reacts to pushed data. ---
    {
        json j; j["radio"]["title"] = "Track 1"; j["radio"]["progress"] = 0.4;
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", j));
    }
    renderedTexts.clear();
    pump();
    INFO("rendered texts after ui:data: " << renderedTexts.size());
    REQUIRE(sawText("Track 1"));                 // the "now playing" label bound {{radio.title}}

    // --- Play button -> real sound:music with the track path. ---
    click(70, 75);                               // play_btn center (20,60,100,30)
    INFO("played=" << played << " path='" << playedPath << "'");
    REQUIRE(played);
    REQUIRE(playedPath == "music/track1.ogg");

    // --- Stop button -> sound:music:stop. ---
    click(70, 115);                              // stop_btn center (20,100,100,30)
    REQUIRE(stopped);

    uiModule->shutdown();
}
