/**
 * Unit oracle for UILayout reflow + relative (percent) sizing — UI framework slice 1.1.
 *
 * Pure: drives UILayout::layout on a tiny widget tree and asserts child geometry. No IIO,
 * no window, no renderer. Locks:
 *   (a) reflow — re-running layout() at a new size re-distributes flex children (the core of
 *       "reflow on resize"; this already worked, so it's a regression lock);
 *   (b) percent sizing — a widthPercent/heightPercent child takes that fraction of the parent
 *       content box and RE-resolves when the layout is re-run at a new size (the new behavior).
 *
 * Links UIModule_static (UILayout lives in a .cpp, not header-only). Floats are compared with a
 * small tolerance helper — the repo's unit tests avoid Catch::Approx.
 */

#include <catch2/catch_test_macros.hpp>
#include "Core/UILayout.h"
#include "Core/UIWidget.h"
#include <memory>
#include <cmath>

using namespace grove;

namespace {

// Minimal concrete widget. UILayout only touches the public geometry/layout fields; it never
// renders or updates, so update/render/getType are inert stubs that satisfy the interface.
struct Box : UIWidget {
    void update(UIContext&, float) override {}
    void render(UIRenderer&) override {}
    std::string getType() const override { return "box"; }
};

Box* addBox(UIWidget* parent) {
    auto child = std::make_unique<Box>();
    Box* raw = child.get();
    parent->addChild(std::move(child));
    return raw;
}

bool near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) < eps; }

} // namespace

TEST_CASE("UILayout reflows flex children when re-laid-out at a new size", "[ui][layout][reflow]") {
    Box root;
    root.layoutProps.mode = LayoutMode::Horizontal;
    Box* a = addBox(&root); a->layoutProps.flex = 1.0f;
    Box* b = addBox(&root); b->layoutProps.flex = 1.0f;

    UILayout::layout(&root, 800.0f, 600.0f);
    CHECK(near(a->width, 400.0f));
    CHECK(near(b->width, 400.0f));
    CHECK(near(a->x, 0.0f));
    CHECK(near(b->x, 400.0f));

    // Reflow: same tree, a new viewport -> children re-distribute. This is the heart of reflow.
    UILayout::layout(&root, 1200.0f, 600.0f);
    CHECK(near(a->width, 600.0f));
    CHECK(near(b->width, 600.0f));
    CHECK(near(b->x, 600.0f));
}

TEST_CASE("UILayout resolves widthPercent against the parent content box (main axis)", "[ui][layout][percent]") {
    Box root;
    root.layoutProps.mode = LayoutMode::Horizontal;
    Box* side = addBox(&root); side->widthPercent = 0.25f;     // a 25% sidebar
    Box* main = addBox(&root); main->layoutProps.flex = 1.0f;  // fills the remainder

    UILayout::layout(&root, 800.0f, 600.0f);
    CHECK(near(side->width, 200.0f));   // 0.25 * 800
    CHECK(near(main->width, 600.0f));   // remaining after the fixed 25%
    CHECK(near(main->x, 200.0f));

    // Reflow: the percent re-resolves against the new width; flex takes the rest.
    UILayout::layout(&root, 1200.0f, 600.0f);
    CHECK(near(side->width, 300.0f));   // 0.25 * 1200
    CHECK(near(main->width, 900.0f));
}

TEST_CASE("UILayout resolves width/heightPercent on both axes in stack mode", "[ui][layout][percent]") {
    Box root;
    root.layoutProps.mode = LayoutMode::Stack;
    Box* child = addBox(&root);
    child->widthPercent  = 0.5f;
    child->heightPercent = 0.5f;

    UILayout::layout(&root, 800.0f, 600.0f);
    CHECK(near(child->width, 400.0f));
    CHECK(near(child->height, 300.0f));
}

// ----------------------------------------------------------------------------
// Slice 1.2 — anchoring
// ----------------------------------------------------------------------------

TEST_CASE("resolveAnchor pins the widget to each point of the parent box", "[ui][layout][anchor]") {
    // Box 800x600 at the origin; widget 100x50; no offset. Expected top-left per anchor.
    const float bx = 0.0f, by = 0.0f, bw = 800.0f, bh = 600.0f, w = 100.0f, h = 50.0f;
    auto at = [&](Anchor a) { return UILayout::resolveAnchor(a, bx, by, bw, bh, w, h, 0.0f, 0.0f); };

    CHECK(near(at(Anchor::TopLeft).x, 0.0f));      CHECK(near(at(Anchor::TopLeft).y, 0.0f));
    CHECK(near(at(Anchor::Top).x, 350.0f));        CHECK(near(at(Anchor::Top).y, 0.0f));
    CHECK(near(at(Anchor::TopRight).x, 700.0f));   CHECK(near(at(Anchor::TopRight).y, 0.0f));
    CHECK(near(at(Anchor::Left).x, 0.0f));         CHECK(near(at(Anchor::Left).y, 275.0f));
    CHECK(near(at(Anchor::Center).x, 350.0f));     CHECK(near(at(Anchor::Center).y, 275.0f));
    CHECK(near(at(Anchor::Right).x, 700.0f));      CHECK(near(at(Anchor::Right).y, 275.0f));
    CHECK(near(at(Anchor::BottomLeft).x, 0.0f));   CHECK(near(at(Anchor::BottomLeft).y, 550.0f));
    CHECK(near(at(Anchor::Bottom).x, 350.0f));     CHECK(near(at(Anchor::Bottom).y, 550.0f));
    CHECK(near(at(Anchor::BottomRight).x, 700.0f));CHECK(near(at(Anchor::BottomRight).y, 550.0f));
}

TEST_CASE("resolveAnchor applies the offset and honors a padded box origin", "[ui][layout][anchor]") {
    // Box origin (20,30) (e.g. padding), size 760x540; widget 100x40; offset (-10,-10).
    AnchorPos br = UILayout::resolveAnchor(Anchor::BottomRight, 20.0f, 30.0f, 760.0f, 540.0f, 100.0f, 40.0f, -10.0f, -10.0f);
    CHECK(near(br.x, 20.0f + 760.0f - 100.0f - 10.0f));   // 670
    CHECK(near(br.y, 30.0f + 540.0f - 40.0f - 10.0f));    // 520

    // Anchor::None is a pass-through to the offset (legacy x/y handling lives in the caller).
    AnchorPos none = UILayout::resolveAnchor(Anchor::None, 0.0f, 0.0f, 800.0f, 600.0f, 100.0f, 40.0f, 5.0f, 7.0f);
    CHECK(near(none.x, 5.0f));
    CHECK(near(none.y, 7.0f));
}

TEST_CASE("An anchored child in an absolute parent tracks the corner on reflow", "[ui][layout][anchor]") {
    Box root;
    root.layoutProps.mode = LayoutMode::Absolute;
    Box* hud = addBox(&root);
    hud->width = 100.0f; hud->height = 40.0f;
    hud->anchor = Anchor::BottomRight;
    hud->anchorOffsetX = -10.0f; hud->anchorOffsetY = -10.0f;

    UILayout::layout(&root, 800.0f, 600.0f);
    CHECK(near(hud->x, 690.0f));   // 800-100-10
    CHECK(near(hud->y, 550.0f));   // 600-40-10

    // Reflow to a bigger viewport: the HUD follows the bottom-right corner.
    UILayout::layout(&root, 1200.0f, 800.0f);
    CHECK(near(hud->x, 1090.0f));  // 1200-100-10
    CHECK(near(hud->y, 750.0f));   // 800-40-10
}
