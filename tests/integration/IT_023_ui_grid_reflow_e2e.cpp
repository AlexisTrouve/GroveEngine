/**
 * Integration Test IT_023: UIModule grid reflow E2E (UI framework slice 1.3).
 *
 * Proves a grid lays children into N columns whose cells FILL the width, so they reflow on resize —
 * again via the click, never reading coords. The fixture is a 2-column grid filling the viewport,
 * gap 0, rowHeight 300:
 *   800x600  -> cellW 400; b0=[0,400], b1=[400,800] (top row); click (500,150) hits b1 -> "test:b1".
 *   ui:resize 1200x600 -> cellW 600; b0=[0,600], b1=[600,1200]; the SAME click (500,150) now hits b0.
 *
 * Before slice 1.3 the "grid" mode is unknown — children are never positioned (stay at x/y=0), so the
 * first click misses and REQUIRE(test:b1) fails. Green proves the grid + the cell-fill reflow.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_023: a grid reflows its cells across a resize (UI slice 1.3)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("grd_host");
    auto uiIO     = mgr.createInstance("grd_ui");
    auto observer = mgr.createInstance("grd_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "grd_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_layout_grid.json");
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

    // --- 800 wide: 2 cols -> cellW 400; (500,150) is in the right cell (b1). ---
    lastAction.clear();
    click(500, 150);
    INFO("pre-resize action='" << lastAction << "'");
    REQUIRE(lastAction == "test:b1");

    // --- Resize to 1200 wide: cells grow to 600; the divider moves to x=600. ---
    {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setDouble("width", 1200.0);
        d->setDouble("height", 600.0);
        hostPub->publish("ui:resize", std::move(d));
    }
    pump();

    // --- Same click (500,150) now lands in the LEFT cell (b0). Proves the cells reflowed. ---
    lastAction.clear();
    click(500, 150);
    INFO("post-resize action='" << lastAction << "'");
    REQUIRE(lastAction == "test:b0");

    uiModule->shutdown();
}
