/**
 * Unit oracle for UIWidget z-order — UI framework slice 3a.
 *
 * z-order is modeled by sibling ORDER: the last child renders on top and the reverse-order hit-test
 * finds it first. bringToFront() must therefore move a widget to the END of its parent's children
 * while preserving the relative order of the others. Pure tree op — no IIO/render.
 */

#include <catch2/catch_test_macros.hpp>
#include "Core/UIWidget.h"
#include <memory>

using namespace grove;

namespace {
struct Box : UIWidget {
    void update(UIContext&, float) override {}
    void render(UIRenderer&) override {}
    std::string getType() const override { return "box"; }
};
Box* addBox(UIWidget* parent) {
    auto c = std::make_unique<Box>();
    Box* raw = c.get();
    parent->addChild(std::move(c));
    return raw;
}
} // namespace

TEST_CASE("bringToFront moves a widget to the back of its siblings (z-order)", "[ui][zorder]") {
    Box root;
    Box* a = addBox(&root);
    Box* b = addBox(&root);
    Box* c = addBox(&root);

    REQUIRE(root.children.size() == 3);
    CHECK(root.children[0].get() == a);
    CHECK(root.children[1].get() == b);
    CHECK(root.children[2].get() == c);

    // Raise b: order becomes a, c, b (b is frontmost = last). Others keep their relative order.
    b->bringToFront();
    CHECK(root.children[0].get() == a);
    CHECK(root.children[1].get() == c);
    CHECK(root.children[2].get() == b);

    // Already frontmost -> no-op (and no dangling: b still valid and last).
    b->bringToFront();
    CHECK(root.children[2].get() == b);
    CHECK(b->getType() == "box");

    // Raise the first: a goes last, leaving c, b, a.
    a->bringToFront();
    CHECK(root.children[0].get() == c);
    CHECK(root.children[1].get() == b);
    CHECK(root.children[2].get() == a);

    // No parent -> safe no-op.
    root.bringToFront();
    CHECK(root.children.size() == 3);
}
