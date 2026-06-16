/**
 * Unit Tests: grove::anim Transform2D + Hierarchy (animation system, slice 1).
 *
 * WHAT  : Locks the 2D transform composition and the parent/child hierarchy that the whole
 *         animation system stands on — "linked objects" where child sprites move relative to
 *         a parent (a ship hull with turrets, a body with limbs). update() recomputes every
 *         node's WORLD transform from its LOCAL transform and its parent's world transform.
 *
 * WHY    : Sprites in the engine are flat and independent. Animation-by-movement needs child
 *         transforms expressed relative to a parent and composed to world space. This is the
 *         pure, headless-testable foundation (no GPU) under clips/players/blending (slices 2-4).
 *
 * HOW    : Pure math — translate/rotate/scale compose. Nodes are stored in an array with a
 *         parent index (parent always added before child), so update() is a single forward
 *         pass: world[i] = parent<0 ? local[i] : compose(world[parent], local[i]).
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/anim/Transform2D.h"

#include <cmath>

using namespace grove::anim;
using Catch::Matchers::WithinAbs;

static constexpr float PI = 3.14159265358979323846f;

TEST_CASE("Transform2D - transformPoint applies scale, rotation, then translation", "[anim][transform][unit]") {
    Transform2D t;
    t.x = 100.0f; t.y = 50.0f;
    t.rotation = PI / 2.0f;          // +90°
    t.scaleX = 2.0f; t.scaleY = 2.0f;

    float ox = 0.0f, oy = 0.0f;
    transformPoint(t, 10.0f, 0.0f, ox, oy);   // local point (10,0)

    // scale -> (20,0); rotate +90° -> (0,20); translate -> (100,70)
    REQUIRE_THAT(ox, WithinAbs(100.0f, 0.001f));
    REQUIRE_THAT(oy, WithinAbs(70.0f, 0.001f));
}

TEST_CASE("Transform2D - identity compose returns the local unchanged", "[anim][transform][unit]") {
    Transform2D parent;  // identity
    Transform2D local;
    local.x = 5.0f; local.y = -3.0f; local.rotation = 0.4f; local.scaleX = 1.5f; local.scaleY = 0.5f;

    Transform2D w = compose(parent, local);
    REQUIRE_THAT(w.x, WithinAbs(5.0f, 0.001f));
    REQUIRE_THAT(w.y, WithinAbs(-3.0f, 0.001f));
    REQUIRE_THAT(w.rotation, WithinAbs(0.4f, 0.001f));
    REQUIRE_THAT(w.scaleX, WithinAbs(1.5f, 0.001f));
    REQUIRE_THAT(w.scaleY, WithinAbs(0.5f, 0.001f));
}

TEST_CASE("Transform2D - compose accumulates rotation/scale and places the child origin", "[anim][transform][unit]") {
    Transform2D parent;
    parent.x = 200.0f; parent.y = 200.0f;
    parent.rotation = PI / 2.0f;     // +90°
    parent.scaleX = 2.0f; parent.scaleY = 2.0f;

    Transform2D local;
    local.x = 10.0f; local.y = 0.0f;  // child sits 10 units along parent's local +X
    local.rotation = PI / 4.0f;       // +45°
    local.scaleX = 3.0f; local.scaleY = 3.0f;

    Transform2D w = compose(parent, local);

    // Child origin: parent transforms (10,0) -> scale 20 -> rot+90 -> (0,20) -> +200 -> (200,220)
    REQUIRE_THAT(w.x, WithinAbs(200.0f, 0.001f));
    REQUIRE_THAT(w.y, WithinAbs(220.0f, 0.001f));
    // Rotation adds, scale multiplies.
    REQUIRE_THAT(w.rotation, WithinAbs(PI / 2.0f + PI / 4.0f, 0.001f));
    REQUIRE_THAT(w.scaleX, WithinAbs(6.0f, 0.001f));
    REQUIRE_THAT(w.scaleY, WithinAbs(6.0f, 0.001f));
}

TEST_CASE("Hierarchy - single root node: world equals local", "[anim][hierarchy][unit]") {
    Hierarchy h;
    Transform2D root;
    root.x = 42.0f; root.y = 7.0f; root.rotation = 0.3f;
    int id = h.addNode(-1, root);
    h.update();

    REQUIRE(id == 0);
    REQUIRE_THAT(h.world(id).x, WithinAbs(42.0f, 0.001f));
    REQUIRE_THAT(h.world(id).y, WithinAbs(7.0f, 0.001f));
    REQUIRE_THAT(h.world(id).rotation, WithinAbs(0.3f, 0.001f));
}

TEST_CASE("Hierarchy - child follows a moving/rotating parent", "[anim][hierarchy][unit]") {
    Hierarchy h;

    Transform2D parentLocal;
    parentLocal.x = 100.0f; parentLocal.y = 100.0f;
    int parent = h.addNode(-1, parentLocal);

    Transform2D childLocal;
    childLocal.x = 50.0f; childLocal.y = 0.0f;   // 50 to the right of the parent
    int child = h.addNode(parent, childLocal);

    h.update();
    // No parent rotation yet: child world = (150,100)
    REQUIRE_THAT(h.world(child).x, WithinAbs(150.0f, 0.001f));
    REQUIRE_THAT(h.world(child).y, WithinAbs(100.0f, 0.001f));

    // Rotate the parent +90° and re-update: the child swings to (100,150).
    h.local(parent).rotation = PI / 2.0f;
    h.update();
    REQUIRE_THAT(h.world(child).x, WithinAbs(100.0f, 0.001f));
    REQUIRE_THAT(h.world(child).y, WithinAbs(150.0f, 0.001f));
    // Child inherits parent rotation.
    REQUIRE_THAT(h.world(child).rotation, WithinAbs(PI / 2.0f, 0.001f));
}

TEST_CASE("Hierarchy - three levels compose down the chain", "[anim][hierarchy][unit]") {
    Hierarchy h;
    Transform2D a; a.x = 10.0f;
    Transform2D b; b.x = 10.0f;
    Transform2D c; c.x = 10.0f;
    int ia = h.addNode(-1, a);
    int ib = h.addNode(ia, b);
    int ic = h.addNode(ib, c);
    h.update();

    // No rotation: offsets just add -> 30.
    REQUIRE_THAT(h.world(ic).x, WithinAbs(30.0f, 0.001f));
    REQUIRE_THAT(h.world(ic).y, WithinAbs(0.0f, 0.001f));
}
