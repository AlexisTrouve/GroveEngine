/**
 * Integration Test IT_036: input-capture signal (anti-click-through) — ui:capture {mouse, keyboard}.
 *
 * The UI must tell the game when IT is consuming the pointer, so a click/drag on the UI does NOT also
 * reach the world/camera behind it. UIModule publishes ui:capture {mouse} (on change):
 *
 *   A. OVER a widget (the list absorbs) -> mouse=true.
 *   B. Over EMPTY space (a bare panel doesn't absorb) -> mouse=false.
 *   C. A press that GRABBED the UI keeps capture even when the cursor leaves the widget mid-drag, until
 *      release. (So dragging the scrollbar off the list still suppresses world input.)
 *
 * Fixture: flat list (100,100) 200x160 inside an 800x600 root panel; empty area at (700,500).
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_036: ui:capture guards click-through (over UI / over world / during a grab)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("cap_host");
    auto uiIO     = mgr.createInstance("cap_ui");
    auto observer = mgr.createInstance("cap_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "cap_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_list.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int capMouse = -1;   // latest ui:capture.mouse (-1 = none yet)
    observer->subscribe("ui:capture", [&](const Message& m) { capMouse = m.data->getBool("mouse", false) ? 1 : 0; });

    auto pump = [&] {
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto move = [&](double x, double y) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", x); d->setDouble("y", y);
        hostPub->publish("input:mouse:move", std::move(d)); pump();
    };
    auto button = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setInt("button", 0); d->setBool("pressed", pressed);
        hostPub->publish("input:mouse:button", std::move(d)); pump();
    };

    pump();  // settle

    // --- A. Over the list (it absorbs) -> captured. ---
    move(180, 160);
    REQUIRE(capMouse == 1);

    // --- B. Over empty space (the bare root panel doesn't absorb) -> not captured. ---
    move(700, 500);
    REQUIRE(capMouse == 0);

    // --- C. Press on the list, then drag the cursor OFF the list while held -> capture persists. ---
    move(180, 160);
    REQUIRE(capMouse == 1);
    button(true);                 // grab the UI
    move(700, 500);               // cursor leaves the list, still holding
    INFO("during off-widget grab, capMouse=" << capMouse);
    REQUIRE(capMouse == 1);       // capture persists for the whole drag
    button(false);                // release
    REQUIRE(capMouse == 0);       // and drops once released over empty space

    uiModule->shutdown();
}
