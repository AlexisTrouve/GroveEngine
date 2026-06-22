/**
 * Integration Test IT_026: in-app Window — opacity + content clipping (UI framework slice 3b-1).
 *
 * The window sits over a big background button. We assert three things via real clicks:
 *   1. a button INSIDE the window's content area is clickable (clipped-in),
 *   2. the window is OPAQUE — clicking its title bar (over the background button) is ABSORBED, so the
 *      background button does NOT fire (no leak to widgets behind),
 *   3. a content button positioned BELOW the content area is CLIPPED out of the hit-test — the click
 *      falls through to the background instead of firing the (invisible) content button.
 *
 * Geometry: window at (150,150) 200x150, titleBarHeight 28 -> content rect (150,178,200,122).
 *   content_btn -> abs [150,270] x [178,208]  (inside  -> "content")
 *   below_btn   -> abs [150,270] x [378,408]  (below content -> clipped, unreachable)
 *   bg_btn      -> [100,400] x [100,400]       ("background")
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_026: an in-app window is opaque and clips its content (UI slice 3b-1)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("win_host");
    auto uiIO     = mgr.createInstance("win_ui");
    auto observer = mgr.createInstance("win_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "win_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_window.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    observer->subscribe("ui:action", [&](const Message& m) {
        lastAction = m.data->getString("action", "");
    });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d));
    };
    auto click = [&](double x, double y) {
        sendMove(x, y);    pump();
        sendButton(true);  pump();
        sendButton(false); pump();
    };

    pump();  // settle (content children get positioned under the title bar)

    // 1. A button inside the content area is clickable.
    lastAction.clear();
    click(210, 193);
    INFO("content action='" << lastAction << "'");
    REQUIRE(lastAction == "content");

    // 2. Opaque: a title-bar click (over the background button) is absorbed -> nothing fires.
    lastAction.clear();
    click(300, 165);
    INFO("titlebar action='" << lastAction << "'");
    REQUIRE(lastAction.empty());

    // 3. Content clip: a content button below the content area is unreachable -> the click falls
    //    through to the background button instead of firing "below".
    lastAction.clear();
    click(210, 393);
    INFO("below action='" << lastAction << "'");
    REQUIRE(lastAction == "background");

    uiModule->shutdown();
}
