/**
 * Integration Test IT_030: edge drawer slides open/closed (UI framework slice 5b).
 *
 * A LEFT drawer (closed, openExtent 200, slideDuration 0.5s) holds a content button at content (20,20)
 * -> abs [20,140]x[20,50] (center 80,35) when fully open. We assert:
 *   1. closed  -> the button is off screen, a click at its open position hits nothing,
 *   2. right after toggling open it's STILL not clickable (it is sliding, not instant),
 *   3. after the slide settles the button responds,
 *   4. toggling closed and settling makes it un-clickable again.
 *
 * The slide is driven per-frame, so we pump several frames (16ms each) to advance / settle it.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_030: an edge drawer slides open and closed (UI slice 5b)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("dr_host");
    auto uiIO     = mgr.createInstance("dr_ui");
    auto observer = mgr.createInstance("dr_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "dr_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_drawer.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string lastAction;
    observer->subscribe("ui:action", [&](const Message& m) { lastAction = m.data->getString("action", ""); });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto pumpN = [&](int n) { for (int i = 0; i < n; ++i) pump(); };
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d));
    };
    auto click = [&](double x, double y) { sendMove(x, y); pump(); sendButton(true); pump(); sendButton(false); pump(); };
    auto toggle = [&] {
        auto d = std::make_unique<JsonDataNode>("d"); d->setString("id", "drawer");
        hostPub->publish("ui:drawer:toggle", std::move(d));
        pump();  // process the toggle
    };

    pump();  // settle

    // 1. Closed: the content button is off screen.
    lastAction.clear();
    click(80, 35);
    INFO("closed action='" << lastAction << "'");
    REQUIRE(lastAction.empty());

    // 2. Toggle open, then click immediately -> still sliding, not yet reachable (proves not instant).
    toggle();
    lastAction.clear();
    click(80, 35);
    INFO("mid-slide action='" << lastAction << "'");
    REQUIRE(lastAction.empty());

    // 3. Let the slide settle -> the button responds.
    pumpN(40);
    lastAction.clear();
    click(80, 35);
    INFO("open action='" << lastAction << "'");
    REQUIRE(lastAction == "draweritem");

    // 4. Toggle closed, settle -> un-clickable again.
    toggle();
    pumpN(40);
    lastAction.clear();
    click(80, 35);
    INFO("reclosed action='" << lastAction << "'");
    REQUIRE(lastAction.empty());

    uiModule->shutdown();
}
