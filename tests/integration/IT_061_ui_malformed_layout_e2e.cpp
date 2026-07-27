/**
 * Integration Test IT_061: ONE malformed widget must not take down the whole screen.
 *
 * WHAT : loads a layout in which a `list` is authored with the WRONG items shape — a plain string array
 *        (`["Alpha","Beta"]`) instead of the array-of-objects the parser documents. A well-formed button
 *        sits beside it. The button's chrome must still reach the renderer.
 *
 * WHY  : this is a real incident, not a hypothetical. Authoring exactly that array made the ENTIRE layout
 *        fail to load — the button and a window elsewhere in the file silently vanished, which reads as
 *        "my last change broke the UI" and sends you hunting in the wrong place. A widget whose factory
 *        throws must be DROPPED with an actionable diagnostic, never take its siblings with it. Losing a
 *        list is a bug; losing the screen is an outage, and one with no clue attached.
 *
 * HOW  : the IT_060 harness (load the real libUIModule, point it at a layout, pump process(), capture
 *        published render:*). The surviving button carries a `frame`, so a single render:nineslice:add
 *        is the objective proof that the rest of the tree was built and rendered.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <string>
#include <vector>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_061: a malformed widget is dropped, its siblings still render", "[integration][ui][e2e][robustness]") {
    auto& mgr = IntraIOManager::getInstance();
    auto uiIO = mgr.createInstance("mal_ui");
    auto game = mgr.createInstance("mal_game");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "mal_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 400);
    cfg.setInt("windowHeight", 300);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_malformed.json");
    cfg.setInt("baseLayer", 1000);
    // Loading a layout with a bad widget must not throw out of setConfiguration either.
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::vector<std::string> frameAssets;
    game->subscribe("render:nineslice:add", [&](const Message& m) {
        frameAssets.push_back(m.data->getString("asset", ""));
    });
    int textCount = 0;
    game->subscribe("render:text:add", [&](const Message&) { textCount++; });

    JsonDataNode input("input");
    input.setDouble("deltaTime", 0.016);
    REQUIRE_NOTHROW(uiModule->process(input));
    while (game->hasMessages() > 0) game->pullAndDispatch();

    // THE POINT: the sibling of the malformed list rendered. Before the fix this vector was EMPTY —
    // the throw from the list factory unwound the whole parse and the screen came up blank.
    INFO("frames seen: " << frameAssets.size());
    REQUIRE(std::find(frameAssets.begin(), frameAssets.end(), "ui/survivor_frame") != frameAssets.end());

    // The button's label rendered too, so the survivor is fully alive, not just its chrome.
    REQUIRE(textCount > 0);

    uiModule->shutdown();
}
