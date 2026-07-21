/**
 * Integration Test IT_060: a UI button AND window can wear a 9-slice (nine-patch) composed border.
 *
 * WHAT : drives the REAL UIModule (loaded as a shared library) from a JSON layout in which a button and a
 *        window each carry a `frame` block (asset id + source dims + margin insets). It proves the widget ->
 *        UIRenderer -> IIO path publishes render:nineslice:add for each, carrying the authored frame asset id,
 *        the target rect, and the margins — i.e. the composed-border feature works end to end through the
 *        actual module, not just the renderer primitive (that half is NineSliceCollectorTest).
 *
 * WHY  : the doctrine — "a UI without an E2E that really drives it does not exist". Reading the widget code is
 *        not proof; this test clicks the real module into publishing the 9-slice message. It is the regression
 *        lock that the `frame` JSON authoring reaches the renderer.
 *
 * HOW  : the IT_052 harness (load libUIModule, point it at a layout file, pump process(), capture published
 *        render:* on a subscriber IIO). We assert the two authored frames appear on render:nineslice:add.
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

TEST_CASE("IT_060: a button and a window publish a 9-slice frame", "[integration][ui][e2e][nineslice]") {
    auto& mgr = IntraIOManager::getInstance();
    auto uiIO = mgr.createInstance("ns_ui");
    auto game = mgr.createInstance("ns_game");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ns_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 400);
    cfg.setInt("windowHeight", 300);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_nineslice.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    // One record per render:nineslice:add — the authored asset id + target rect + one margin (proves the
    // insets ride through). space is expected "screen" (UI chrome is HUD).
    struct NS { std::string asset; double x, y, w, h, left; std::string space; };
    std::vector<NS> frames;
    game->subscribe("render:nineslice:add", [&](const Message& m){
        frames.push_back(NS{
            m.data->getString("asset", ""),
            m.data->getDouble("x", -1), m.data->getDouble("y", -1),
            m.data->getDouble("w", -1), m.data->getDouble("h", -1),
            m.data->getDouble("left", -1),
            m.data->getString("space", "")
        });
    });

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (game->hasMessages() > 0) game->pullAndDispatch();
    };
    pump();   // retained mode: the two framed widgets publish their :add once

    auto find = [&](const std::string& id) -> const NS* {
        auto it = std::find_if(frames.begin(), frames.end(), [&](const NS& n){ return n.asset == id; });
        return it == frames.end() ? nullptr : &*it;
    };

    INFO("frames seen: " << [&]{ std::string s; for (auto& f : frames) s += f.asset + " "; return s; }());

    // --- The button's frame. ---
    const NS* btn = find("ui/button_frame");
    REQUIRE(btn != nullptr);
    REQUIRE(btn->w == 160.0);          // authored button width
    REQUIRE(btn->h == 48.0);           // authored button height
    REQUIRE(btn->left == 8.0);         // uniform inset -> left = 8
    REQUIRE(btn->space == "screen");   // UI chrome is HUD (camera-immune)

    // --- The window's frame (whole-window box). ---
    const NS* win = find("ui/window_frame");
    REQUIRE(win != nullptr);
    REQUIRE(win->w == 200.0);          // authored window width
    REQUIRE(win->h == 140.0);          // authored window height
    REQUIRE(win->left == 12.0);        // per-side inset -> left = 12

    uiModule->shutdown();
}
