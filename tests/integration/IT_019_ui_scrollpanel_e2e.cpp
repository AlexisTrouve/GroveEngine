/**
 * Integration Test IT_019: UIModule ScrollPanel E2E — scroll then click a scrolled child.
 *
 * ScrollPanel was flagged broken: render() temporarily set each child's absX/absY to
 * the scrolled position, rendered, then RESTORED them to the un-scrolled values. So
 * the panel DREW children at the scrolled spot but hit-testing (which reads absX at the
 * top of the next frame) saw the un-scrolled spot — clicking a scrolled child missed.
 *
 * This test scrolls a panel to the bottom via the mouse wheel, then clicks the child
 * that is now visible at the bottom, and asserts its onClick action fires. It locks the
 * fix that makes the scroll offset persist in absX so render and hit-test agree.
 *
 * Fixture (absolute root): scrollpanel sp at (100,100) 300x200, vertical scroll, content:
 *   btn_top    @ rel (0,0)   200x50 -> abs (100,100), center (200,125)  [visible at rest]
 *   btn_bottom @ rel (0,400) 200x50 -> content height 450 -> scrollable
 * Max scroll = 450 - 200 = 250. After scrolling to the bottom, btn_bottom sits at
 * absY = 100 + 400 - 250 = 250 -> center (200,275), inside the panel and clickable.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_019: clicking a child after scrolling hits it at its rendered position", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto inputPub = mgr.createInstance("sp_input");
    auto uiIO     = mgr.createInstance("sp_ui");
    auto observer = mgr.createInstance("sp_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "sp_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_scrollpanel.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int actions = 0;
    std::string lastAction;
    observer->subscribe("ui:action", [&](const Message& m) {
        actions++;
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
        inputPub->publish("input:mouse:move", std::move(d));
    };
    auto sendButton = [&](bool pressed) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setInt("button", 0); d->setBool("pressed", pressed);
        inputPub->publish("input:mouse:button", std::move(d));
    };
    auto sendWheel = [&](double delta) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("delta", delta);
        inputPub->publish("input:mouse:wheel", std::move(d));
    };

    // Sanity (holds before and after the fix): at rest, btn_top is clickable at (200,125).
    sendMove(200.0, 125.0); pump();
    sendButton(true);       pump();
    sendButton(false);      pump();
    REQUIRE(actions == 1);
    REQUIRE(lastAction == "scroll:top");

    // Scroll to the bottom: hover a child (so the wheel routes to its scrollpanel
    // parent), then send a large negative wheel delta (scrolls content up, clamps to max).
    sendMove(200.0, 125.0); pump();   // ensure hovered child = btn_top
    sendWheel(-100.0);      pump();   // route to scrollpanel -> scrollOffsetY clamps to 250

    // btn_bottom is now rendered at center (200,275). Click it.
    sendMove(200.0, 275.0); pump();
    sendButton(true);       pump();
    sendButton(false);      pump();

    INFO("actions=" << actions << " lastAction='" << lastAction << "'");
    REQUIRE(actions == 2);                 // a new action fired...
    REQUIRE(lastAction == "scroll:bottom"); // ...from the scrolled-into-view child

    uiModule->shutdown();
}
