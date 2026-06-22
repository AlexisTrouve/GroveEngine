/**
 * Integration Test IT_033: data-driven ship list — select, scroll, repopulate (UI framework slice — sidebar).
 *
 * The marquee "sidebar partiel avec des vaisseaux dedans": a scrollable, clipped, selectable list whose
 * rows are GENERATED from item data. This E2E proves the three load-bearing behaviours headlessly:
 *
 *   A. SELECT  — the list loads 8 ships (from the fixture's items[]); clicking the 2nd visible row
 *      publishes ui:list:selected {index:1, itemId:"ship1"}.
 *   B. SCROLL + CLIP (click-flip) — a wheel scroll moves the rows under the (fixed) clip rect; the SAME
 *      screen-y click now lands on a DIFFERENT ship. If scroll, clip and hit-test didn't coincide this
 *      would still report ship1 (the bug the scroll path is designed to avoid).
 *   C. REPOPULATE — ui:list:set_items swaps in 3 new items at runtime; the stale scroll/selection are
 *      reset and clicking row 0 selects the new "alpha" (proves the retained row-id pool is re-registered
 *      and the old rows purged — no ghosts, no stale hit-targets).
 *
 * Geometry: list at (100,100) 200x160, rowHeight 40 -> 4 rows fit (160/40); 8 items -> contentH 320,
 *   maxScroll 160. Row i screen-y = 100 + i*40 - scrollOffsetY.
 *     unscrolled: y=160 -> i = (160-100)/40 = 1 -> ship1.
 *     scrolled +160 (wheel -8, *20=-160, scrollOffsetY clamps to 160):
 *               y=160 -> i = (160-100+160)/40 = 220/40 = 5 -> ship5.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_033: data-driven ship list selects, scrolls, repopulates", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("list_host");
    auto uiIO     = mgr.createInstance("list_ui");
    auto observer = mgr.createInstance("list_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "list_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_list.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int    selIndex  = -1;
    std::string selItemId;
    observer->subscribe("ui:list:selected", [&](const Message& m) {
        selIndex  = m.data->getInt("index", -1);
        selItemId = m.data->getString("itemId", "");
    });

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
    auto sendWheel = [&](double delta) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("delta", delta);
        hostPub->publish("input:mouse:wheel", std::move(d));
    };
    auto click = [&](double x, double y) { sendMove(x, y); pump(); sendButton(true); pump(); sendButton(false); pump(); };

    pump();  // settle (first update positions rows)

    // --- A. SELECT: click the 2nd visible row (y=160 -> ship1). ---
    selIndex = -1; selItemId.clear();
    click(180, 160);
    INFO("A select: index=" << selIndex << " itemId='" << selItemId << "'");
    REQUIRE(selIndex == 1);
    REQUIRE(selItemId == "ship1");

    // --- B. SCROLL + CLIP click-flip: wheel down, SAME y now hits ship5. ---
    sendMove(180, 160); pump();          // hover over the list so the wheel routes to it
    sendWheel(-8.0); pump();             // -8 * 20 = -160 -> scrollOffsetY clamps to maxScroll 160
    selIndex = -1; selItemId.clear();
    click(180, 160);
    INFO("B scroll: index=" << selIndex << " itemId='" << selItemId << "'");
    REQUIRE(selIndex == 5);
    REQUIRE(selItemId == "ship5");

    // --- C. REPOPULATE at runtime: 3 new items; scroll+selection reset; row 0 -> "alpha". ---
    {
        // The payload MUST be json-backed (in the node's m_data): IIO transports only getJsonData()
        // (m_data), NOT child nodes built via setChild — so the items[] array lives in the json, exactly
        // like the layout-file fixture and how a real game would serialize the message.
        json j;
        j["id"] = "ships";
        j["items"] = json::array();
        const char* ids[]    = { "alpha", "beta", "gamma" };
        const char* labels[] = { "Alpha", "Beta", "Gamma" };
        for (int i = 0; i < 3; ++i) {
            j["items"].push_back({ {"id", ids[i]}, {"label", labels[i]} });
        }
        hostPub->publish("ui:list:set_items", std::make_unique<JsonDataNode>("d", j));
    }
    pump();  // apply set_items
    pump();  // clamp scroll (3 items fit -> offset 0) + reposition rows

    selIndex = -1; selItemId.clear();
    click(180, 120);                      // row 0 ([100,140))
    INFO("C repop: index=" << selIndex << " itemId='" << selItemId << "'");
    REQUIRE(selIndex == 0);
    REQUIRE(selItemId == "alpha");

    uiModule->shutdown();
}
