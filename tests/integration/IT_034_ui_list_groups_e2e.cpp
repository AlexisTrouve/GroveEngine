/**
 * Integration Test IT_034: warship GROUPS — collapsible grouped list (UI framework slice — fleet groups).
 *
 * The warship-groups system: the fleet list shows GROUPS (wings/squadrons), each a collapsible header over
 * its ships. The engine provides the system; Drifterra builds the final UI on top. This E2E proves the
 * load-bearing behaviours headlessly:
 *
 *   A. ITEM SELECT carries its GROUP — clicking a ship row publishes ui:list:selected with the groupId.
 *   B. EXPAND (click-flip) — clicking a COLLAPSED group's header expands it (ui:list:group:toggled
 *      {collapsed:false}); rows that did not exist become clickable (a ship row appears where there was
 *      none). The row set provably changed.
 *   C. COLLAPSE hides items — clicking an expanded group's header collapses it; a ship that was selectable
 *      is replaced by another group's header at the same screen y (its items are gone from the row list).
 *
 * Fixture: list at (100,100) 200x240, rowHeight 40. Groups: alpha (header + 2 ships, expanded), bravo
 *   (header + 2 ships, COLLAPSED). Initial rows: [H alpha, a0, a1, H bravo] at screen y 100,140,180,220.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_034: collapsible warship groups — select-with-group, expand, collapse", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("grp_host");
    auto uiIO     = mgr.createInstance("grp_ui");
    auto observer = mgr.createInstance("grp_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "grp_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_list_groups.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    int    selIndex = -1;
    std::string selItemId, selGroupId;
    std::string toggledGroup; int toggledCollapsed = -1;
    observer->subscribe("ui:list:selected", [&](const Message& m) {
        selIndex   = m.data->getInt("index", -1);
        selItemId  = m.data->getString("itemId", "");
        selGroupId = m.data->getString("groupId", "");
    });
    observer->subscribe("ui:list:group:toggled", [&](const Message& m) {
        toggledGroup     = m.data->getString("groupId", "");
        toggledCollapsed = m.data->getBool("collapsed", false) ? 1 : 0;
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
    auto click = [&](double x, double y) { sendMove(x, y); pump(); sendButton(true); pump(); sendButton(false); pump(); };
    auto reset = [&] { selIndex = -1; selItemId.clear(); selGroupId.clear(); toggledGroup.clear(); toggledCollapsed = -1; };

    pump();  // settle

    // --- A. ITEM SELECT carries its group: row 1 (y=160) = ship a0 of group alpha. ---
    reset();
    click(180, 160);
    INFO("A: idx=" << selIndex << " item='" << selItemId << "' grp='" << selGroupId << "'");
    REQUIRE(selGroupId == "alpha");
    REQUIRE(selItemId == "a0ship");
    REQUIRE(selIndex == 0);            // index WITHIN the group

    // Before expanding bravo, the row at y=280 does not exist (only 4 rows: 100..260).
    reset();
    click(180, 280);
    INFO("pre-expand y280: item='" << selItemId << "' tgl='" << toggledGroup << "'");
    REQUIRE(selItemId.empty());
    REQUIRE(toggledGroup.empty());

    // --- B. EXPAND: click bravo's header (row 3, y=240) -> toggled{collapsed:false}; its ships appear. ---
    reset();
    click(180, 240);
    INFO("B expand: tgl='" << toggledGroup << "' collapsed=" << toggledCollapsed);
    REQUIRE(toggledGroup == "bravo");
    REQUIRE(toggledCollapsed == 0);   // was collapsed -> now expanded

    // Now row 4 (y=280) is bravo's first ship.
    reset();
    click(180, 280);
    INFO("B select b0: item='" << selItemId << "' grp='" << selGroupId << "'");
    REQUIRE(selGroupId == "bravo");
    REQUIRE(selItemId == "b0ship");
    REQUIRE(selIndex == 0);

    // --- C. COLLAPSE alpha (row 0, y=120): its ships vanish; y=160 is now bravo's header, not a ship. ---
    reset();
    click(180, 120);
    INFO("C collapse: tgl='" << toggledGroup << "' collapsed=" << toggledCollapsed);
    REQUIRE(toggledGroup == "alpha");
    REQUIRE(toggledCollapsed == 1);   // now collapsed

    // Rows are now [H alpha(collapsed), H bravo, b0, b1]. y=160 was ship a0; it's now bravo's header.
    reset();
    click(180, 160);
    INFO("C after: item='" << selItemId << "' tgl='" << toggledGroup << "'");
    REQUIRE(selItemId.empty());        // no ship selected at y=160 anymore
    REQUIRE(toggledGroup == "bravo");  // it's bravo's header now

    uiModule->shutdown();
}
