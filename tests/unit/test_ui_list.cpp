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
#include "Widgets/UIList.h"

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
