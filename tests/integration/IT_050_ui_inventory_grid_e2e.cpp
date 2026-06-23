/**
 * Integration Test IT_050: modular INVENTORY GRID. The resources are an inventory of fixed-size cells (icon +
 * count number), laid out in a grid, scrollable on overflow — NOT a text list. Verified headlessly:
 *
 *   A. CELLS — each item draws an icon sprite + its count number (a grid of icon+count tiles).
 *   B. TOOLTIP — hovering a cell pops the item's name (data-bound tooltip).
 *   C. SCROLL — the 50 items overflow the panel; wheeling down brings a late item's count into view.
 *
 * Locks the composition: scrollpanel > grid-layout panel > a {{inventory}} repeater of icon+count cells.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <set>
#include <string>

using namespace grove;

TEST_CASE("IT_050: resources are a scrollable inventory grid of icon+count cells", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("iv_host");
    auto uiIO     = mgr.createInstance("iv_ui");
    auto observer = mgr.createInstance("iv_obs");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "iv_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 1280);
    cfg.setInt("windowHeight", 720);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_inventory.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::set<std::string> texts;
    int spriteAdds = 0;
    auto cap = [&](const Message& m){ texts.insert(m.data->getString("text","")); };
    observer->subscribe("render:text:add",    cap);
    observer->subscribe("render:text:update", cap);
    observer->subscribe("render:text",        cap);   // immediate (tooltip)
    observer->subscribe("render:sprite:add",  [&](const Message&){ ++spriteAdds; });

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (observer->hasMessages() > 0) observer->pullAndDispatch();
    };
    auto move  = [&](double x,double y){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",x); d->setDouble("y",y); hostPub->publish("input:mouse:move", std::move(d)); pump(); };
    auto wheel = [&](double dl){ auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("delta",dl); hostPub->publish("input:mouse:wheel", std::move(d)); pump(); };

    // 50 items in a 4-column grid (host-computed cx/cy so the cells are absolute -> the scrollpanel can scroll
    // them; a grid LAYOUT child de-scrolls itself, a known scrollpanel limitation). Distinct counts 100..149.
    {
        json inv = json::array();
        for (int i = 0; i < 50; ++i) {
            char nm[16]; std::snprintf(nm, sizeof nm, "Item %02d", i);
            inv.push_back({ {"id", "it"+std::to_string(i)}, {"name", nm}, {"icon", 1+(i%4)}, {"count", 100+i},
                            {"cx", (i%4)*56 + 6}, {"cy", (i/4)*56 + 6} });
        }
        hostPub->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"inventory", inv} }));
    }
    pump(); pump(); pump();

    // --- A. CELLS: icons drew sprites + the count numbers rendered. (Cumulative — cells render during the
    //     push pumps above, so don't reset.) ---
    INFO("sprite adds = " << spriteAdds);
    REQUIRE(spriteAdds >= 12);            // the cell icons drew (a grid of tiles)
    REQUIRE(texts.count("100") > 0);      // item 0's count
    REQUIRE(texts.count("103") > 0);      // a few cells in -> a real grid of counts

    // --- B. TOOLTIP: hover the first cell -> its item name. scrollpanel (100,80) + cell (6,6) -> (106,86)
    //     50x50, center ~ (131,111). Hold past the 0.5s delay. ---
    REQUIRE_FALSE(texts.count("Item 00") > 0);
    move(131, 111);
    for (int i = 0; i < 40; ++i) pump();
    REQUIRE(texts.count("Item 00") > 0);  // bound tooltip on the cell

    // --- C. SCROLL: the scrollpanel CULLS off-screen cells, so a late item (count "149", row 12) isn't
    //     rendered until we wheel down. Hover a real cell so the wheel routes to the scrollpanel. ---
    REQUIRE_FALSE(texts.count("149") > 0);
    move(131, 111);                       // over cell 0 -> wheel routes up to the scrollpanel
    wheel(-60); wheel(-60); wheel(-60);   // scroll to the bottom
    pump(); pump();
    REQUIRE(texts.count("149") > 0);      // overflow scrolled -> the last item's count is now visible

    uiModule->shutdown();
}
