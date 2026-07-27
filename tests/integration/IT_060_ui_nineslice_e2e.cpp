/**
 * Integration Test IT_060: a UI button AND window can wear a 9-slice (nine-patch) composed border.
 *
 * WHAT : drives the REAL UIModule (loaded as a shared library) from a JSON layout in which a button and a
 *        window each carry a `frame` block (asset id + source dims + margin insets). It proves the widget ->
 *        UIRenderer -> IIO path publishes render:nineslice:add for each, carrying the authored frame asset id,
 *        the target rect, and the margins — i.e. the composed-border feature works end to end through the
 *        actual module, not just the renderer primitive (that half is NineSliceCollectorTest).
 *
 * WHY  : the doctrine — "a UI without an E2E that really drives it does not exist". Reading the widget code is
 *        not proof; this test clicks the real module into publishing the 9-slice message. It is the regression
 *        lock that the `frame` JSON authoring reaches the renderer.
 *
 * HOW  : the IT_052 harness (load libUIModule, point it at a layout file, pump process(), capture published
 *        render:* on a subscriber IIO). We assert the two authored frames appear on render:nineslice:add.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <string>
#include <vector>
#include <algorithm>

using namespace grove;

TEST_CASE("IT_060: a button, a window and a panel publish a 9-slice frame", "[integration][ui][e2e][nineslice]") {
    auto& mgr = IntraIOManager::getInstance();
    auto uiIO = mgr.createInstance("ns_ui");
    auto game = mgr.createInstance("ns_game");

    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/libUIModule.dll";
#endif
    std::unique_ptr<IModule> uiModule;
    REQUIRE_NOTHROW(uiModule = uiLoader.load(uiPath, "ns_ui"));
    REQUIRE(uiModule != nullptr);

    JsonDataNode cfg("config");
    cfg.setInt("windowWidth", 400);
    cfg.setInt("windowHeight", 300);
    cfg.setString("layoutFile", "../../assets/ui/test_e2e_nineslice.json");
    cfg.setInt("baseLayer", 1000);
    REQUIRE_NOTHROW(uiModule->setConfiguration(cfg, uiIO.get(), nullptr));

    // One record per render:nineslice:add — the authored asset id + target rect + one margin (proves the
    // insets ride through). space is expected "screen" (UI chrome is HUD).
    struct NS { std::string asset; double x, y, w, h, left; std::string space; };
    std::vector<NS> frames;
    game->subscribe("render:nineslice:add", [&](const Message& m){
        frames.push_back(NS{
            m.data->getString("asset", ""),
            m.data->getDouble("x", -1), m.data->getDouble("y", -1),
            m.data->getDouble("w", -1), m.data->getDouble("h", -1),
            m.data->getDouble("left", -1),
            m.data->getString("space", "")
        });
    });

    // Flat rects, to check WHERE a framed window puts its chrome. NOTE the UI renderer emits a rect as
    // a retained SPRITE — there is no render:rect:* topic — and it follows the engine anchor convention:
    // cx,cy = CENTRE, scaleX/scaleY = the box size.
    struct R { double cx, cy, w, h; };
    std::vector<R> rects;
    game->subscribe("render:sprite:add", [&](const Message& m){
        rects.push_back(R{ m.data->getDouble("cx", -1), m.data->getDouble("cy", -1),
                           m.data->getDouble("scaleX", -1), m.data->getDouble("scaleY", -1) });
    });

    auto pump = [&]{
        JsonDataNode input("input"); input.setDouble("deltaTime", 0.016);
        uiModule->process(input);
        while (game->hasMessages() > 0) game->pullAndDispatch();
    };
    pump();   // retained mode: the two framed widgets publish their :add once

    auto find = [&](const std::string& id) -> const NS* {
        auto it = std::find_if(frames.begin(), frames.end(), [&](const NS& n){ return n.asset == id; });
        return it == frames.end() ? nullptr : &*it;
    };

    INFO("frames seen: " << [&]{ std::string s; for (auto& f : frames) s += f.asset + " "; return s; }());

    // --- The button's frame. ---
    const NS* btn = find("ui/button_frame");
    REQUIRE(btn != nullptr);
    REQUIRE(btn->w == 160.0);          // authored button width
    REQUIRE(btn->h == 48.0);           // authored button height
    REQUIRE(btn->left == 8.0);         // uniform inset -> left = 8
    REQUIRE(btn->space == "screen");   // UI chrome is HUD (camera-immune)

    // --- The window's frame (whole-window box). ---
    const NS* win = find("ui/window_frame");
    REQUIRE(win != nullptr);
    REQUIRE(win->w == 200.0);          // authored window width
    REQUIRE(win->h == 140.0);          // authored window height
    REQUIRE(win->left == 12.0);        // per-side inset -> left = 12

    // --- The panel's frame. `panel` is the generic container and BY FAR the most-used widget in real
    //     layouts, so it is the one that makes an art pass visible everywhere.
    const NS* pan = find("ui/panel_frame");
    REQUIRE(pan != nullptr);
    REQUIRE(pan->w == 150.0);          // authored panel width
    REQUIRE(pan->h == 60.0);           // authored panel height
    REQUIRE(pan->left == 6.0);         // uniform inset -> left = 6
    REQUIRE(pan->space == "screen");

    // --- The scroll panel's frame. Its flat look draws the border as FOUR separate rects (top, bottom,
    //     left, right); one nine-patch replaces all four plus the background — the textbook case.
    const NS* scr = find("ui/scroll_frame");
    REQUIRE(scr != nullptr);
    REQUIRE(scr->w == 140.0);          // authored scrollpanel width
    REQUIRE(scr->h == 120.0);          // authored scrollpanel height
    REQUIRE(scr->left == 10.0);        // uniform inset -> left = 10

    // --- The list's own frame (its panel background).
    const NS* lst = find("ui/list_frame");
    REQUIRE(lst != nullptr);
    REQUIRE(lst->w == 180.0);
    REQUIRE(lst->h == 80.0);
    REQUIRE(lst->left == 8.0);

    // --- The list's ROW frame. Authored once, emitted per visible row: the existing
    //     selected > hovered > zebra colour becomes the frame's TINT (same trick as a button's state
    //     colour), so selection highlighting keeps working with art instead of flat rects.
    const size_t rowFrames = std::count_if(frames.begin(), frames.end(),
                                           [](const NS& n){ return n.asset == "ui/row_frame"; });
    REQUIRE(rowFrames >= 3u);          // three authored items, all inside an 80px-tall viewport
    const NS* rowf = find("ui/row_frame");
    REQUIRE(rowf != nullptr);
    REQUIRE(rowf->h == 24.0);          // authored rowHeight
    REQUIRE(rowf->left == 4.0);

    // --- The checkbox's frame dresses the BOX, not the whole widget row (the label sits beside it).
    //     Only the box gets one: stretching the middle of a checkmark would be meaningless, so the
    //     tick stays a sprite.
    const NS* chk = find("ui/check_frame");
    REQUIRE(chk != nullptr);
    REQUIRE(chk->w == 24.0);           // boxSize, NOT the widget's 140 width
    REQUIRE(chk->h == 24.0);           // square box
    REQUIRE(chk->left == 4.0);

    // --- The progress bar gets TWO frames: the track (full width) and the FILL, whose width tracks
    //     `progress`. A nine-patch fill is what keeps a bar's end caps crisp at any fill level —
    //     asserting 0.5 x 160 = 80 proves the fill frame really follows the value.
    const NS* bar = find("ui/bar_frame");
    REQUIRE(bar != nullptr);
    REQUIRE(bar->w == 160.0);          // the whole track
    REQUIRE(bar->left == 5.0);
    const NS* fill = find("ui/bar_fill");
    REQUIRE(fill != nullptr);
    REQUIRE(fill->w == 80.0);          // progress 0.5 of 160
    REQUIRE(fill->h == 18.0);          // full height (horizontal bar)
    REQUIRE(fill->left == 3.0);

    // --- P4: the four widgets that had NO art path at all. Each dresses the surface that actually
    //     reads as its chrome — deliberately not "the whole widget rect" everywhere.
    const NS* inp = find("ui/input_frame");
    REQUIRE(inp != nullptr);
    REQUIRE(inp->w == 130.0);          // the field box
    REQUIRE(inp->left == 5.0);

    const NS* tab = find("ui/tabs_frame");
    REQUIRE(tab != nullptr);
    REQUIRE(tab->w == 120.0);          // the content background (the tab strip stays flat for now)
    REQUIRE(tab->left == 7.0);

    const NS* drw = find("ui/drawer_frame");
    REQUIRE(drw != nullptr);
    REQUIRE(drw->left == 9.0);

    // The modal dresses its DIALOG, not its dim backdrop: stretching art over a full-screen dim veil
    // would be meaningless. So the frame must measure the dialog (210x130), NOT the 400x300 overlay.
    const NS* mod = find("ui/modal_frame");
    REQUIRE(mod != nullptr);
    REQUIRE(mod->w == 210.0);
    REQUIRE(mod->h == 130.0);
    REQUIRE(mod->left == 11.0);

    // --- DEBT LIFT: a framed window must put its chrome INSIDE the border art, not on top of it.
    //     The window is 200x140 with a uniform inset of 12, so its inner box is 176 wide and the title
    //     bar must be 176x28 at (x+12, y+12) — NOT 200 wide at the window origin, straddling the frame's
    //     top edge and corners, which is what it did before and what looked wrong.
    const bool titleBarInset = std::any_of(rects.begin(), rects.end(), [](const R& r) {
        return r.w == 176.0 && r.h == 28.0;      // 200 - 12 - 12 wide, titleBarHeight tall
    });
    const bool titleBarOnFrame = std::any_of(rects.begin(), rects.end(), [](const R& r) {
        return r.w == 200.0 && r.h == 28.0;      // the old, full-width bar sitting over the border
    });
    INFO("rects seen: " << rects.size());
    REQUIRE(titleBarInset);
    REQUIRE_FALSE(titleBarOnFrame);

    // NON-REGRESSION, the whole point of "additif": EVERY published frame must belong to one of the
    // authored `frame` blocks. The root panel, the labels and every other widget carry none and must
    // publish nothing here. (Counted this way rather than by a magic total, so it survives the list
    // emitting one frame per visible row.)
    const std::vector<std::string> authored = {
        "ui/button_frame", "ui/window_frame", "ui/panel_frame",
        "ui/scroll_frame", "ui/list_frame",   "ui/row_frame",
        "ui/check_frame",  "ui/bar_frame",    "ui/bar_fill",
        "ui/input_frame",  "ui/tabs_frame",   "ui/drawer_frame", "ui/modal_frame"
    };
    for (const NS& f : frames) {
        INFO("unexpected frame asset: '" << f.asset << "'");
        REQUIRE(std::find(authored.begin(), authored.end(), f.asset) != authored.end());
    }

    uiModule->shutdown();
}
