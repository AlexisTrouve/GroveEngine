/**
 * Integration Test IT_022: UIModule anchoring reflow E2E (UI framework slice 1.2).
 *
 * Proves a widget anchored to a corner TRACKS that corner when the viewport resizes — again via
 * the click, never reading coords. The fixture pins a button to the parent's bottom-right (offset
 * -10,-10) inside an absolute, viewport-filling root:
 *   800x600 -> button at [690,790]x[550,590]; click (740,570) hits it -> "test:anchor".
 *   ui:resize 1200x800 -> button follows to [1090,1190]x[750,790]:
 *       the OLD spot (740,570) is now empty (no action), the NEW corner (1140,770) hits it.
 *
 * Before slice 1.2 the anchor is ignored (the button stays at its x/y=0 origin), so the very first
 * bottom-right click misses and REQUIRE(test:anchor) fails. Green proves anchoring + corner-tracking.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_022: an anchored widget tracks its corner across a resize (UI slice 1.2)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("anc_host");
    auto uiIO     = mgr.createInstance("anc_ui");
    auto observer = mgr.createInstance("anc_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "anc_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_layout_anchor.json");
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

    pump();  // settle

    // --- 800x600: the button is anchored bottom-right; (740,570) is on it. ---
    lastAction.clear();
    click(740, 570);
    INFO("pre-resize action='" << lastAction << "'");
    REQUIRE(lastAction == "test:anchor");

    // --- Resize to 1200x800: the button must FOLLOW the bottom-right corner. ---
    {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("width", 1200.0);
        d->setDouble("height", 800.0);
        hostPub->publish("ui:resize", std::move(d));
    }
    pump();

    // The OLD spot is now empty — the anchor moved away from it.
    lastAction.clear();
    click(740, 570);
    INFO("old-spot action='" << lastAction << "'");
    REQUIRE(lastAction.empty());

    // The NEW bottom-right corner hits the button.
    lastAction.clear();
    click(1140, 770);
    INFO("new-corner action='" << lastAction << "'");
    REQUIRE(lastAction == "test:anchor");

    uiModule->shutdown();
}
