/**
 * Integration Test IT_035: UIList scrollbar drag + content drag-to-scroll (UI framework slice).
 *
 * Proves the two drag-scroll paths via the click-flip idiom (a fixed-y click lands on a DIFFERENT ship
 * after the drag scrolled the rows), plus that a plain click still selects (select moved to release):
 *
 *   A. CONTENT DRAG — press on a row and drag up; the content scrolls 1:1 and the drag does NOT select
 *      (it's a scroll, not a click). A later fixed-y click then hits a deeper ship.
 *   B. THUMB DRAG — press on the scrollbar thumb and drag down; the list scrolls proportionally.
 *   C. PLAIN CLICK still selects (press+release, no movement -> select on release).
 *
 * Fixture: flat list (100,100) 200x160, rowHeight 40 -> 4 rows fit; 8 items -> contentH 320, maxScroll 160.
 *   Scrollbar column x∈[292,300] (width 8). Thumb height = 160*(160/320)=80; at scroll 0, thumb y∈[100,180].
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_035: UIList scrollbar + content drag-to-scroll", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("drag_host");
    auto uiIO     = mgr.createInstance("drag_ui");
    auto observer = mgr.createInstance("drag_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "drag_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_list.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string selItemId;
    observer->subscribe("ui:list:selected", [&](const Message& m) { selItemId = m.data->getString("itemId", ""); });

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
    auto click = [&](double x, double y) { move(x, y); button(true); button(false); };
    // Press at (x0,y0), drag through to (x1,y1) in steps, release.
    auto drag = [&](double x0, double y0, double x1, double y1) {
        move(x0, y0); button(true);
        const int steps = 4;
        for (int i = 1; i <= steps; ++i)
            move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
        button(false);
    };

    pump();  // settle

    SECTION("A: content drag-to-scroll (does not select; later click hits a deeper ship)") {
        // Baseline: a plain click at y=160 selects ship1.
        selItemId.clear();
        click(180, 160);
        REQUIRE(selItemId == "ship1");

        // Drag content UP by 120 (press y=220 -> y=100) -> scroll +120. The drag itself must NOT select.
        selItemId.clear();
        drag(180, 220, 180, 100);
        INFO("after content drag, sel='" << selItemId << "'");
        REQUIRE(selItemId.empty());          // a drag is a scroll, not a click

        // Same fixed-y click now lands on a deeper ship: rowAt(160) @ scroll120 = (160-100+120)/40 = 4.
        selItemId.clear();
        click(180, 160);
        INFO("post-drag click sel='" << selItemId << "'");
        REQUIRE(selItemId == "ship4");
    }

    SECTION("B: scrollbar thumb drag scrolls proportionally") {
        // Press the thumb (x~296, y~120) and drag to the bottom -> scroll clamps to max (160).
        drag(296, 120, 296, 260);
        // rowAt(160) @ scroll160 = (160-100+160)/40 = 5 -> ship5.
        selItemId.clear();
        click(180, 160);
        INFO("after thumb drag, click sel='" << selItemId << "'");
        REQUIRE(selItemId == "ship5");
    }

    SECTION("C: a plain click (no drag) still selects") {
        selItemId.clear();
        click(180, 140);                     // rowAt(140) @ scroll0 = 1 -> ship1
        REQUIRE(selItemId == "ship1");
    }

    uiModule->shutdown();
}
