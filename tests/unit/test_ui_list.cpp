/**
 * UIListUnit — pure geometry/data oracle for the data-driven ship list (UI framework slice — sidebar).
 *
 * No renderer, no IIO: asserts the load-bearing math directly (rowAt hit-test, wheel/scroll clamping,
 * the items[] parser, selection clamping). The E2E (IT_033) proves it end-to-end through the module; this
 * locks the primitives so a regression points at the exact broken function.
 *
 * Reference geometry (matches the IT_033 fixture): list at (100,100) 200x160, rowHeight 40 -> 4 rows fit;
 *   8 items -> contentH 320, maxScroll 160. Row i screen-y = 100 + i*40 - scrollOffsetY.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include "Widgets/UIList.h"
#include "Rendering/UIRenderer.h"

using namespace grove;

// UIWidget declares a virtual dtor (suppresses the implicit move) and holds unique_ptr children (copy
// deleted) -> UIList is neither copyable nor movable. So fill a caller-owned instance in place.
static void fill(UIList& list, int itemCount) {
    list.absX = 100; list.absY = 100; list.width = 200; list.height = 160;
    list.rowHeight = 40;
    std::vector<ListItem> items;
    for (int i = 0; i < itemCount; ++i) {
        ListItem it; it.id = "ship" + std::to_string(i); it.label = "Ship " + std::to_string(i);
        items.push_back(std::move(it));
    }
    list.setItems(std::move(items));
}

TEST_CASE("UIListUnit: rowAt maps screen y to the item under it (scroll-aware)", "[ui][list][unit]") {
    UIList list; fill(list, 8);

    SECTION("unscrolled: rows tile from the top edge") {
        REQUIRE(list.rowAt(100) == 0);    // top edge
        REQUIRE(list.rowAt(139) == 0);
        REQUIRE(list.rowAt(160) == 1);
        REQUIRE(list.rowAt(259) == 3);    // last fully-visible row
    }

    SECTION("out of the visible band returns -1") {
        REQUIRE(list.rowAt(99)  == -1);   // above the top
        REQUIRE(list.rowAt(260) == -1);   // at/below the bottom edge (absY+height)
        REQUIRE(list.rowAt(400) == -1);
    }

    SECTION("scrolled: the same screen y maps to a deeper item") {
        list.handleMouseWheel(-8.0f);     // -8 * 20 = +160 -> clamps to maxScroll 160
        REQUIRE(list.scrollOffsetY == 160.0f);
        REQUIRE(list.rowAt(160) == 5);    // (160-100+160)/40 = 5
        REQUIRE(list.rowAt(100) == 4);    // (100-100+160)/40 = 4
    }
}

TEST_CASE("UIListUnit: wheel scroll clamps to [0, contentHeight-height]", "[ui][list][unit]") {
    UIList list; fill(list, 8);          // contentH 320, height 160 -> maxScroll 160

    list.handleMouseWheel(-100.0f);      // huge scroll down
    REQUIRE(list.scrollOffsetY == 160.0f);   // clamped to max, not 2000

    list.handleMouseWheel(100.0f);       // huge scroll up
    REQUIRE(list.scrollOffsetY == 0.0f);     // clamped to 0, not negative
}

TEST_CASE("UIListUnit: a list that fits never scrolls", "[ui][list][unit]") {
    UIList list; fill(list, 3);          // contentH 120 < height 160 -> maxScroll 0
    list.handleMouseWheel(-50.0f);
    REQUIRE(list.scrollOffsetY == 0.0f);
    REQUIRE(list.rowAt(120) == 0);
    REQUIRE(list.rowAt(240) == -1);      // (240-100)/40 = 3, but only 3 items (0..2) -> out of range
}

TEST_CASE("UIListUnit: setItems resets scroll + selection", "[ui][list][unit]") {
    UIList list; fill(list, 8);
    list.handleMouseWheel(-8.0f);
    list.setSelectedIndex(6);
    REQUIRE(list.scrollOffsetY == 160.0f);
    REQUIRE(list.selectedIndex() == 6);

    list.setItems({});                   // empty
    REQUIRE(list.selectedIndex() == -1);
    REQUIRE(list.scrollOffsetY == 0.0f);
    REQUIRE(list.rowAt(120) == -1);      // nothing to hit
}

TEST_CASE("UIListUnit: setSelectedIndex clamps to a valid item", "[ui][list][unit]") {
    UIList list; fill(list, 4);
    list.setSelectedIndex(99);
    REQUIRE(list.selectedIndex() == 3);  // clamped to N-1
    list.setSelectedIndex(-5);
    REQUIRE(list.selectedIndex() == 0);  // clamped to 0

    UIList empty; fill(empty, 0);
    empty.setSelectedIndex(2);
    REQUIRE(empty.selectedIndex() == -1); // empty -> no selection
}

TEST_CASE("UIListUnit: VIRTUALIZED — registered entries are bounded by the viewport, not the item count",
          "[ui][list][unit]") {
    // The core virtualization invariant: rendering a HUGE list must register a pool sized to what fits on
    // screen (a handful of rows), NOT one set of entries per item. A non-virtualized render would register
    // ~itemCount*4 entries -> this asserts that does NOT happen.
    auto& mgr = IntraIOManager::getInstance();
    auto io = mgr.createInstance("ui_list_vtest");
    UIRenderer renderer(io.get());

    UIList list; fill(list, 1000);       // height 160, rowHeight 40 -> ~4 rows fit
    const size_t hugeIfNotVirtual = 4 * 1000;   // bg+icon+label+subtitle per item if O(N)

    renderer.beginFrame();
    list.render(renderer);
    REQUIRE(renderer.entryCount() < 60);                 // ~ (4 visible + buffer)*4 + 1 bg
    REQUIRE(renderer.entryCount() < hugeIfNotVirtual / 10);

    // Scrolling deep must NOT grow the pool unboundedly (the slots are recycled, not per-item).
    list.handleMouseWheel(-100000.0f);   // slam to the bottom (clamped)
    renderer.beginFrame();
    list.render(renderer);
    REQUIRE(renderer.entryCount() < 60);
}

TEST_CASE("UIListUnit: visibleRange is the scroll-aware window of on-screen items", "[ui][list][unit]") {
    UIList list; fill(list, 1000);       // height 160, rowHeight 40

    int first = -1, count = -1;
    SECTION("top: rows 0..3 fit (4th starts at the bottom edge)") {
        list.visibleRange(first, count);
        REQUIRE(first == 0);
        REQUIRE(count == 4);
    }
    SECTION("scrolled deep: window tracks the offset, still ~viewport-sized") {
        list.handleMouseWheel(-100000.0f);  // slam to the bottom -> clamps to maxScroll = 1000*40-160 = 39840
        REQUIRE(list.scrollOffsetY == 39840.0f);
        list.visibleRange(first, count);
        REQUIRE(first == 996);           // floor(39840/40)
        REQUIRE(count == 4);             // items 996..999
        // and the last row is still selectable via rowAt at its screen position
        REQUIRE(list.rowAt(220) == 999); // (220-100+39840)/40 = 999
    }
    SECTION("empty list: no window") {
        UIList empty; fill(empty, 0);
        empty.visibleRange(first, count);
        REQUIRE(first == 0);
        REQUIRE(count == 0);
    }
}

TEST_CASE("UIListUnit: parseItems reads an items[] array-of-objects", "[ui][list][unit]") {
    json j;
    j["items"] = json::array();
    j["items"].push_back({ {"id", "alpha"}, {"label", "Alpha"} });
    j["items"].push_back({ {"id", "beta"},  {"label", "Beta"}, {"subtitle", "Frigate"}, {"icon", 7} });
    JsonDataNode node("payload", j);

    auto items = UIList::parseItems(node);
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].id == "alpha");
    REQUIRE(items[0].label == "Alpha");
    REQUIRE(items[0].subtitle.empty());      // default
    REQUIRE(items[0].iconTextureId == 0);    // default
    REQUIRE(items[1].id == "beta");
    REQUIRE(items[1].subtitle == "Frigate");
    REQUIRE(items[1].iconTextureId == 7);

    SECTION("missing items[] -> empty, no crash") {
        JsonDataNode empty("e", json::object());
        REQUIRE(UIList::parseItems(empty).empty());
    }
}

TEST_CASE("UIListUnit: scrollbar thumb geometry (proportional, scroll-positioned)", "[ui][list][unit]") {
    UIList list; fill(list, 8);          // h160 rowHeight40 -> contentH 320 -> scrollable
    REQUIRE(list.isScrollable());

    float x, y, w, h;
    list.scrollbarThumbRect(x, y, w, h);
    REQUIRE(w == list.scrollbarWidth);
    REQUIRE(x == 100.0f + 200.0f - list.scrollbarWidth);  // right column (absX+width-sbW)
    REQUIRE(h == 80.0f);                 // height * (height/contentH) = 160 * (160/320)
    REQUIRE(y == 100.0f);                // scroll 0 -> thumb at the top

    list.handleMouseWheel(-100.0f);      // slam to the bottom -> scroll clamps to 160
    list.scrollbarThumbRect(x, y, w, h);
    REQUIRE(y == 180.0f);                // absY + ratio(1.0) * (height - thumbH) = 100 + 80

    UIList small; fill(small, 3);        // contentH 120 < 160 -> no scrollbar
    REQUIRE_FALSE(small.isScrollable());
}

TEST_CASE("UIListUnit: parseGroups reads groups[] each with its own items[]", "[ui][list][unit]") {
    json j;
    j["groups"] = json::array();
    j["groups"].push_back({ {"id", "alpha"}, {"label", "Alpha"},
                            {"items", json::array({ {{"id","a0"},{"label","A0"}}, {{"id","a1"},{"label","A1"}} })} });
    j["groups"].push_back({ {"id", "bravo"}, {"label", "Bravo"}, {"collapsed", true},
                            {"items", json::array({ {{"id","b0"},{"label","B0"}} })} });
    JsonDataNode node("payload", j);

    auto groups = UIList::parseGroups(node);
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].id == "alpha");
    REQUIRE(groups[0].collapsed == false);
    REQUIRE(groups[0].items.size() == 2);
    REQUIRE(groups[0].items[1].id == "a1");
    REQUIRE(groups[1].id == "bravo");
    REQUIRE(groups[1].collapsed == true);
    REQUIRE(groups[1].items.size() == 1);
    REQUIRE(groups[1].items[0].id == "b0");
}

TEST_CASE("UIListUnit: grouped projection + collapse (rebuildRows via setGroups/toggleGroup)", "[ui][list][unit]") {
    UIList list; list.absX=100; list.absY=100; list.width=200; list.height=240; list.rowHeight=40;
    std::vector<ListGroup> groups = {
        { "alpha", "Alpha", false, { {"a0","A0","",0}, {"a1","A1","",0} } },
        { "bravo", "Bravo", true,  { {"b0","B0","",0}, {"b1","B1","",0} } },
    };
    list.setGroups(std::move(groups));

    // alpha expanded (header + 2 items) + bravo collapsed (header only) -> 4 rows.
    REQUIRE(list.rowCount() == 4);
    REQUIRE(list.rowPtr(0)->isHeader);
    REQUIRE(list.rowPtr(0)->groupId == "alpha");
    REQUIRE_FALSE(list.rowPtr(1)->isHeader);
    REQUIRE(list.rowPtr(1)->itemId == "a0");
    REQUIRE(list.rowPtr(1)->itemIndex == 0);
    REQUIRE(list.rowPtr(3)->isHeader);
    REQUIRE(list.rowPtr(3)->groupId == "bravo");
    REQUIRE(list.rowPtr(3)->collapsed == true);

    // Expand bravo -> its 2 items appear (6 rows).
    REQUIRE(list.toggleGroup("bravo") == false);   // new collapsed state = false (expanded)
    REQUIRE(list.rowCount() == 6);
    REQUIRE(list.rowPtr(4)->itemId == "b0");

    // Collapse alpha -> its items vanish (rows: Halpha, Hbravo, b0, b1).
    REQUIRE(list.toggleGroup("alpha") == true);
    REQUIRE(list.rowCount() == 4);
    REQUIRE(list.rowPtr(1)->isHeader);
    REQUIRE(list.rowPtr(1)->groupId == "bravo");
}

// ============================================================================
// Slice 5d — N-level tree projection (parseTree recursion + depth + recursive collapse)
// ============================================================================

TEST_CASE("UIListUnit: an N-level tree projects headers/leaves with per-level depth", "[ui][list][unit][tree]") {
    // Fleet(h) > [ Alpha(h) > {A1, A2},  Beta(h, collapsed) > {B1} ].
    json a1; a1["id"] = "a1"; a1["label"] = "A1";
    json a2; a2["id"] = "a2"; a2["label"] = "A2";
    json alpha; alpha["id"] = "alpha"; alpha["label"] = "Alpha"; alpha["children"] = json::array({a1, a2});
    json b1; b1["id"] = "b1"; b1["label"] = "B1";
    json beta; beta["id"] = "beta"; beta["label"] = "Beta"; beta["collapsed"] = true; beta["children"] = json::array({b1});
    json fleet; fleet["id"] = "fleet"; fleet["label"] = "Fleet"; fleet["children"] = json::array({alpha, beta});
    json j; j["nodes"] = json::array({fleet});

    // parseTree recursed into the nested structure.
    JsonDataNode node("n", j);
    auto nodes = UIList::parseTree(node);
    REQUIRE(nodes.size() == 1);
    REQUIRE(nodes[0].children.size() == 2);          // alpha + beta
    REQUIRE(nodes[0].children[0].children.size() == 2);  // alpha's a1 + a2

    UIList list;
    list.absX = 100; list.absY = 100; list.width = 200; list.height = 400; list.rowHeight = 40;
    list.setTree(std::move(nodes));
    REQUIRE(list.isTree());

    // Projected rows (beta collapsed -> b1 hidden): [Fleet, Alpha, A1, A2, Beta].
    REQUIRE(list.rowCount() == 5);
    REQUIRE(list.rowPtr(0)->isHeader);   REQUIRE(list.rowPtr(0)->depth == 0);  REQUIRE(list.rowPtr(0)->groupId == "fleet");
    REQUIRE(list.rowPtr(1)->isHeader);   REQUIRE(list.rowPtr(1)->depth == 1);  REQUIRE(list.rowPtr(1)->groupId == "alpha");
    REQUIRE_FALSE(list.rowPtr(2)->isHeader); REQUIRE(list.rowPtr(2)->depth == 2); REQUIRE(list.rowPtr(2)->itemId == "a1"); REQUIRE(list.rowPtr(2)->itemIndex == 0);
    REQUIRE_FALSE(list.rowPtr(3)->isHeader); REQUIRE(list.rowPtr(3)->depth == 2); REQUIRE(list.rowPtr(3)->itemId == "a2"); REQUIRE(list.rowPtr(3)->itemIndex == 1);
    REQUIRE(list.rowPtr(4)->isHeader);   REQUIRE(list.rowPtr(4)->depth == 1);  REQUIRE(list.rowPtr(4)->groupId == "beta"); REQUIRE(list.rowPtr(4)->collapsed);

    // Expand beta -> its grandchild B1 appears at depth 2 (running leaf index 2).
    REQUIRE(list.toggleGroup("beta") == false);   // was collapsed -> now expanded
    REQUIRE(list.rowCount() == 6);
    REQUIRE_FALSE(list.rowPtr(5)->isHeader); REQUIRE(list.rowPtr(5)->depth == 2); REQUIRE(list.rowPtr(5)->itemId == "b1"); REQUIRE(list.rowPtr(5)->itemIndex == 2);

    // Recursive collapse: folding Alpha removes BOTH its leaves (a1, a2) at once.
    REQUIRE(list.toggleGroup("alpha") == true);
    REQUIRE(list.rowCount() == 4);   // [Fleet, Alpha(collapsed), Beta, B1]
    REQUIRE(list.rowPtr(1)->groupId == "alpha");
    REQUIRE(list.rowPtr(1)->collapsed);
    REQUIRE_FALSE(list.rowPtr(3)->isHeader);   // B1 still there (beta stayed expanded)
    REQUIRE(list.rowPtr(3)->itemId == "b1");
}
