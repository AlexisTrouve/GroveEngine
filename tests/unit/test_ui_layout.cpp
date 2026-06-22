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
