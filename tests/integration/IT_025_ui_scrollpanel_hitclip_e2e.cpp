/**
 * Integration Test IT_025: ScrollPanel clips the HIT-TEST too (UI framework slice 2b).
 *
 * Visual clipping (2a) stops a scrolled-out child from being DRAWN; this stops it from being
 * CLICKED. A child positioned below the panel's visible rect must not receive clicks, even though
 * its rectangle geometrically contains the cursor — the panel clips the hit-test to its bounds.
 *
 * Fixture: a 200x150 scrollpanel at (100,100) -> visible rect [100,300] x [100,250]. Two buttons:
 *   btn_in  at panel (0,0)   -> abs [100,200] x [100,130]  (inside  -> clickable)
 *   btn_out at panel (0,200) -> abs [100,200] x [300,330]  (below   -> must be UN-clickable)
 *
 * Before slice 2b the hit-test descends into the panel regardless, so a click at (150,315) fires
 * btn_out's action. After 2b the click is outside the panel bounds -> no descent -> nothing fires.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_025: a scroll panel clips the hit-test of its children (UI slice 2b)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("hc_host");
    auto uiIO     = mgr.createInstance("hc_ui");
    auto observer = mgr.createInstance("hc_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "hc_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_scrollpanel_hitclip.json");
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

    // Control: a button INSIDE the panel is clickable.
    lastAction.clear();
    click(150, 115);
    INFO("inside action='" << lastAction << "'");
    REQUIRE(lastAction == "test:in");

    // The fix: a button BELOW the panel's visible rect is NOT clickable (clipped hit-test).
    lastAction.clear();
    click(150, 315);
    INFO("below-panel action='" << lastAction << "'");
    REQUIRE(lastAction.empty());

    uiModule->shutdown();
}
