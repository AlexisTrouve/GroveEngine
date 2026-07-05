/**
 * Integration Test IT_056: N-level TREE list — recursive expand/collapse hierarchy (UI slice 5d).
 *
 * Extends the one-level warship-groups model (IT_034) to an arbitrary-depth tree: internal nodes render as
 * collapsible headers, leaves as selectable items, each level indented. The flat-`m_rows` projection means
 * virtualization / scroll / clip / hit-test are unchanged — this locks the load-bearing tree behaviours:
 *
 *   A. DEEP LEAF SELECT — clicking a depth-2 leaf publishes ui:list:selected with its itemId.
 *   B. EXPAND a nested node (click-flip) — clicking a COLLAPSED depth-1 header expands it
 *      (ui:list:group:toggled {collapsed:false}); a GRANDCHILD that did not exist becomes clickable at a
 *      y that was empty. Proves multi-level expansion (a level-2 leaf under a level-1 node).
 *   C. RECURSIVE COLLAPSE — collapsing a depth-1 node removes its whole subtree; a y that was a deep leaf
 *      now resolves to a different node's header (its children are gone from the row list).
 *
 * Fixture (assets/ui/test_e2e_list_tree.json): list at (100,100) 300x400, rowHeight 40. Tree:
 *   Fleet(h) > [ Wing Alpha(h) > {Ship A1, Ship A2},  Wing Beta(h, COLLAPSED) > {Ship B1} ].
 *   Initial rows: [Fleet, Wing Alpha, Ship A1, Ship A2, Wing Beta] at screen y 100,140,180,220,260.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

using namespace grove;

TEST_CASE("IT_056: N-level tree list — deep select, nested expand, recursive collapse (slice 5d)", "[integration][ui][e2e]") {
    auto& mgr = IntraIOManager::getInstance();
    auto hostPub  = mgr.createInstance("tree_host");
    auto uiIO     = mgr.createInstance("tree_ui");
    auto observer = mgr.createInstance("tree_observer");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "tree_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 800);
    cfg.setInt("windowHeight", 600);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_list_tree.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    std::string selItemId; int selIndex = -1;
    std::string toggledGroup; int toggledCollapsed = -1;
    observer->subscribe("ui:list:selected", [&](const Message& m) {
        selItemId = m.data->getString("itemId", ""); selIndex = m.data->getInt("index", -1);
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
    auto reset = [&] { selItemId.clear(); selIndex = -1; toggledGroup.clear(); toggledCollapsed = -1; };

    pump();  // settle

    // --- A. DEEP LEAF SELECT: row 2 (y=200) = Ship A1, a DEPTH-2 leaf under Fleet > Wing Alpha. ---
    reset();
    click(200, 200);
    INFO("A: item='" << selItemId << "' idx=" << selIndex);
    REQUIRE(selItemId == "a1");
    REQUIRE(selIndex == 0);            // first leaf in the running tree order

    // Before expanding Wing Beta, y=320 is below all 5 rows (100..300) -> nothing there.
    reset();
    click(200, 320);
    INFO("pre-expand y320: item='" << selItemId << "' tgl='" << toggledGroup << "'");
    REQUIRE(selItemId.empty());
    REQUIRE(toggledGroup.empty());

    // --- B. EXPAND a nested node: Wing Beta header (row 4, y=280) -> toggled{collapsed:false}. ---
    reset();
    click(200, 280);
    INFO("B expand: tgl='" << toggledGroup << "' collapsed=" << toggledCollapsed);
    REQUIRE(toggledGroup == "beta");
    REQUIRE(toggledCollapsed == 0);    // was collapsed -> now expanded

    // Now the GRANDCHILD Ship B1 exists at row 5 (y=320) — previously empty. Multi-level expansion proven.
    reset();
    click(200, 320);
    INFO("B grandchild select: item='" << selItemId << "'");
    REQUIRE(selItemId == "b1");
    REQUIRE(selIndex == 2);            // leaf order: a1(0), a2(1), b1(2)

    // --- C. RECURSIVE COLLAPSE: Wing Alpha header (row 1, y=160) -> collapse; its 2 ships vanish. ---
    reset();
    click(200, 160);
    INFO("C collapse: tgl='" << toggledGroup << "' collapsed=" << toggledCollapsed);
    REQUIRE(toggledGroup == "alpha");
    REQUIRE(toggledCollapsed == 1);

    // Rows are now [Fleet, Wing Alpha(collapsed), Wing Beta(expanded), Ship B1]. y=200 was Ship A1; it is now
    // Wing Beta's HEADER — clicking it toggles beta, it does NOT select a leaf. The subtree provably shrank.
    reset();
    click(200, 200);
    INFO("C after: item='" << selItemId << "' tgl='" << toggledGroup << "'");
    REQUIRE(selItemId.empty());        // no leaf at y=200 anymore (a1/a2 collapsed away)
    REQUIRE(toggledGroup == "beta");   // y=200 is now Wing Beta's header

    uiModule->shutdown();
}
