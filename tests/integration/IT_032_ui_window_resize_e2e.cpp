/**
 * Integration Test IT_032: in-app window resize via the bottom-right grip (UI framework slice 3b-3).
 *
 * w1 (100,100,200,150) holds a content button at content (50,150) -> abs [150,250]x[278,308]
 * (center 200,293). With the window 150 tall the content area ends at y=250, so the button is BELOW
 * it and CLIPPED out of the hit-test. Dragging the bottom-right grip (center 293,243) to (390,393)
 * grows the window to ~290x293; the content area now reaches y=393, so the button becomes reachable.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_032: dragging the grip resizes the window (UI slice 3b-3)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("wz_host");
    auto uiIO     = mgr.createInstance("wz_ui");
    auto observer = mgr.createInstance("wz_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "wz_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_window_resize.json");
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
    auto sendMove = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d));
    };
    auto click = [&](double x, double y) { sendMove(x, y); pump(); sendButton(true); pump(); sendButton(false); pump(); };

    pump();  // settle

    // Before: the deep button is below the content area -> clipped -> unreachable.
    lastAction.clear();
    click(200, 293);
    INFO("before action='" << lastAction << "'");
    REQUIRE(lastAction.empty());

    // Drag the bottom-right grip (293,243) to (390,393) -> the window grows to ~290x293.
    sendMove(293, 243); pump();
    sendButton(true);   pump();   // grab the grip
    sendMove(390, 393); pump();   // drag while held -> resize
    sendButton(false);  pump();   // release

    // After: the content area now reaches the button -> it responds.
    lastAction.clear();
    click(200, 293);
    INFO("after action='" << lastAction << "'");
    REQUIRE(lastAction == "deep");

    uiModule->shutdown();
}
