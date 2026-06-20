// ============================================================================
// test_zone_nav.cpp — objective unit test for the zone-navigation primitives (slice 1).
//
// QUOI   : verrouille fitBounds / clampPanToBounds / worldPanForScreen (Camera.h) avec des
//   oracles analytiques — pas de GPU, pas de fenêtre. C'est de la géométrie caméra pure.
// POURQUOI : ce sont les fondations de la navigation par zones (cf. docs/design/zone-navigation.md) :
//   cadrer une zone (cible du magnet), clamper le pan dans ses bornes, et la vitesse de pan ∝ 1/zoom.
//   Un bug ici fausserait tout le feel (zoom qui rate la zone, pan qui s'échappe, vitesse incohérente).
// COMMENT : convention caméra `screen = zoom·(world − camTopLeft)` (cf. Camera.h).
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include "Scene/Camera.h"
#include "Scene/ZoneNavigator.h"
#include <cmath>

using namespace grove::camera;

// A toy 2-level tree for the ZoneNavigator (slice 2): a 1000x1000 root with two 200x200 children
// (A top-left, B bottom-right). 1000x1000 viewport, no margin -> clean analytical framings.
static ZoneNavigator makeNav() {
    ZoneNavigator n;
    n.configure(1000.0f, 1000.0f, /*margin*/0.0f, /*magnetRate*/20.0f);
    n.addZone("root", "",     WorldBounds{0.0f,   0.0f,   1000.0f, 1000.0f});
    n.addZone("A",    "root", WorldBounds{100.0f, 100.0f, 300.0f,  300.0f});   // framing zoom 5
    n.addZone("B",    "root", WorldBounds{600.0f, 600.0f, 800.0f,  800.0f});   // framing zoom 5
    n.reset();
    return n;
}

TEST_CASE("fitBounds frames a square zone exactly (no margin)", "[unit][zonenav]") {
    WorldBounds z{0.0f, 0.0f, 100.0f, 100.0f};
    CameraView v = fitBounds(z, 200.0f, 200.0f, 0.0f);
    REQUIRE(std::fabs(v.zoom - 2.0f) < 1e-4f);            // 200/100
    REQUIRE(std::fabs(v.x - 0.0f) < 1e-3f);
    REQUIRE(std::fabs(v.y - 0.0f) < 1e-3f);
    // the visible rectangle equals the zone
    WorldBounds vis = visibleWorldBounds(v);
    REQUIRE(std::fabs(vis.minX - 0.0f)   < 1e-3f);
    REQUIRE(std::fabs(vis.maxX - 100.0f) < 1e-3f);
    REQUIRE(std::fabs(vis.maxY - 100.0f) < 1e-3f);
}

TEST_CASE("fitBounds fits the constraining dimension of a wide zone, centered", "[unit][zonenav]") {
    WorldBounds z{0.0f, 0.0f, 200.0f, 100.0f};            // 200 wide x 100 tall
    CameraView v = fitBounds(z, 200.0f, 200.0f, 0.0f);
    REQUIRE(std::fabs(v.zoom - 1.0f) < 1e-4f);            // min(200/200, 200/100) = 1
    // the zone center maps to the viewport center
    float sx = 0.0f, sy = 0.0f;
    worldToScreen(v, 100.0f, 50.0f, sx, sy);
    REQUIRE(std::fabs(sx - 100.0f) < 1e-3f);
    REQUIRE(std::fabs(sy - 100.0f) < 1e-3f);
}

TEST_CASE("fitBounds margin leaves padding", "[unit][zonenav]") {
    WorldBounds z{0.0f, 0.0f, 100.0f, 100.0f};
    CameraView v = fitBounds(z, 200.0f, 200.0f, 0.2f);
    REQUIRE(std::fabs(v.zoom - 1.6f) < 1e-4f);            // 2.0 * (1 - 0.2)
}

TEST_CASE("clampPanToBounds keeps the visible rect inside the zone", "[unit][zonenav]") {
    WorldBounds z{0.0f, 0.0f, 100.0f, 100.0f};
    // viewport 100x100 at zoom 2 -> visible 50x50
    CameraView a{-10.0f, 20.0f, 2.0f, 100.0f, 100.0f};
    clampPanToBounds(a, z);
    REQUIRE(std::fabs(a.x - 0.0f)  < 1e-3f);              // -10 -> 0 (hit the left edge)
    REQUIRE(std::fabs(a.y - 20.0f) < 1e-3f);             // 20 was in range -> unchanged

    CameraView b{80.0f, 0.0f, 2.0f, 100.0f, 100.0f};
    clampPanToBounds(b, z);
    REQUIRE(std::fabs(b.x - 50.0f) < 1e-3f);              // 80 -> 50 (right edge: 100 - 50)
}

TEST_CASE("clampPanToBounds centers a zone smaller than the view", "[unit][zonenav]") {
    WorldBounds z{0.0f, 0.0f, 40.0f, 40.0f};
    CameraView v{999.0f, -999.0f, 1.0f, 100.0f, 100.0f}; // visible 100x100 > zone 40 on both axes
    clampPanToBounds(v, z);
    REQUIRE(std::fabs(v.x - (-30.0f)) < 1e-3f);           // center: 20 - 50
    REQUIRE(std::fabs(v.y - (-30.0f)) < 1e-3f);
}

TEST_CASE("worldPanForScreen scales pan inversely with zoom", "[unit][zonenav]") {
    REQUIRE(std::fabs(worldPanForScreen(100.0f, 2.0f) - 50.0f)  < 1e-4f);  // zoomed in  -> slow
    REQUIRE(std::fabs(worldPanForScreen(100.0f, 0.5f) - 200.0f) < 1e-4f);  // zoomed out -> fast
}

// ============================================================================
// Slice 2: ZoneNavigator — tree + active zone + soft magnet + elastic clamp + pan-speed.
// ============================================================================

TEST_CASE("ZoneNavigator: reset frames the root", "[unit][zonenav]") {
    ZoneNavigator n = makeNav();
    REQUIRE(n.activeZone() == "root");
    CameraView t = n.target();
    REQUIRE(std::fabs(t.zoom - 1.0f) < 1e-3f);   // 1000/1000
    REQUIRE(std::fabs(t.x - 0.0f) < 1e-2f);      // root centered: 500 - 500
    REQUIRE(std::fabs(t.y - 0.0f) < 1e-2f);
}

TEST_CASE("ZoneNavigator: zooming over a child descends into it (magnet re-centers)", "[unit][zonenav]") {
    ZoneNavigator n = makeNav();
    n.zoomBy(3.0f);                  // zoom 3: pan range opens, still root (A's framing 5 > 3)
    REQUIRE(n.activeZone() == "root");
    n.panScreen(-900.0f, -900.0f);   // pan focus toward A's center: -900/3 = -300 -> 500 -> 200
    REQUIRE(std::fabs(n.focusX() - 200.0f) < 1e-2f);
    n.zoomBy(2.0f);                  // zoom 6 >= A's framing 5 AND focus in A -> enter A
    REQUIRE(n.activeZone() == "A");
    REQUIRE(std::fabs(n.focusX() - 200.0f) < 1e-2f);  // re-centered on A (soft magnet)
    REQUIRE(std::fabs(n.focusY() - 200.0f) < 1e-2f);
}

TEST_CASE("ZoneNavigator: pan stays clamped inside the active zone", "[unit][zonenav]") {
    ZoneNavigator n = makeNav();
    n.setActive("A");                // frame A (zoom 5, focus 200,200)
    REQUIRE(n.activeZone() == "A");
    n.zoomBy(2.0f);                  // zoom 10 -> visible 100 < zone 200, so panning is now possible
    n.panScreen(100000.0f, 0.0f);    // huge pan right -> hits A's right wall
    REQUIRE(std::fabs(n.focusX() - 250.0f) < 1e-2f);  // maxX(300) - visW/2(50)
    n.panScreen(-100000.0f, 0.0f);   // huge pan left -> hits A's left wall
    REQUIRE(std::fabs(n.focusX() - 150.0f) < 1e-2f);  // minX(100) + visW/2(50)
}

TEST_CASE("ZoneNavigator: zooming out ascends to the parent", "[unit][zonenav]") {
    ZoneNavigator n = makeNav();
    n.setActive("A");
    REQUIRE(n.activeZone() == "A");
    n.zoomBy(0.05f);                 // zoom 5*0.05 -> clamped to min 1 -> A's framing 5 > 1 -> root
    REQUIRE(n.activeZone() == "root");
    REQUIRE(std::fabs(n.focusX() - 500.0f) < 1e-2f);  // re-centered on root
}

TEST_CASE("ZoneNavigator: setActive frames the zone and update() glides to it", "[unit][zonenav]") {
    ZoneNavigator n = makeNav();
    n.setActive("B");
    REQUIRE(n.activeZone() == "B");
    CameraView t = n.target();
    REQUIRE(std::fabs(t.zoom - 5.0f) < 1e-3f);   // B is 200x200 -> framing 5
    REQUIRE(std::fabs(t.x - 600.0f) < 1e-2f);    // centerOn(700,5) = 700-100 = 600 = B.minX

    for (int i = 0; i < 100; ++i) n.update(0.05f);   // the live view glides toward the target
    CameraView v = n.view();
    REQUIRE(std::fabs(v.zoom - 5.0f) < 0.05f);
    REQUIRE(std::fabs(v.x - 600.0f) < 0.5f);
}

// ============================================================================
// Slice 3: dynamic add/remove + current-zone-deletion back-out to nearest live ancestor.
// ============================================================================

// A 3-level chain: root (1000) > S (400, framing 2.5) > P (100, framing 10).
static ZoneNavigator makeNav3() {
    ZoneNavigator n;
    n.configure(1000.0f, 1000.0f, 0.0f, 20.0f);
    n.addZone("root", "",     WorldBounds{0.0f,   0.0f,   1000.0f, 1000.0f});
    n.addZone("S",    "root", WorldBounds{100.0f, 100.0f, 500.0f,  500.0f});
    n.addZone("P",    "S",    WorldBounds{150.0f, 150.0f, 250.0f,  250.0f});
    n.reset();
    return n;
}

TEST_CASE("ZoneNavigator: deleting the active leaf backs out one zone", "[unit][zonenav]") {
    ZoneNavigator n = makeNav3();
    n.setActive("P");
    REQUIRE(n.activeZone() == "P");
    n.removeZone("P");
    REQUIRE(n.activeZone() == "S");        // backed out to the parent
    REQUIRE_FALSE(n.hasZone("P"));
}

TEST_CASE("ZoneNavigator: deleting the active zone's parent backs out two zones", "[unit][zonenav]") {
    ZoneNavigator n = makeNav3();
    n.setActive("P");
    REQUIRE(n.activeZone() == "P");
    n.removeZone("S");                     // deletes S AND its child P (the subtree)
    REQUIRE(n.activeZone() == "root");     // the parent S is gone too -> grandparent
    REQUIRE_FALSE(n.hasZone("S"));
    REQUIRE_FALSE(n.hasZone("P"));
}

TEST_CASE("ZoneNavigator: deleting a non-active zone leaves the camera put", "[unit][zonenav]") {
    ZoneNavigator n = makeNav3();
    n.setActive("root");
    n.removeZone("S");
    REQUIRE(n.activeZone() == "root");     // unaffected
    REQUIRE_FALSE(n.hasZone("S"));
    REQUIRE_FALSE(n.hasZone("P"));         // S's subtree went with it
}

TEST_CASE("ZoneNavigator: a zone can be added at runtime then entered", "[unit][zonenav]") {
    ZoneNavigator n;
    n.configure(1000.0f, 1000.0f, 0.0f, 20.0f);
    n.addZone("root", "", WorldBounds{0.0f, 0.0f, 1000.0f, 1000.0f});
    n.reset();
    REQUIRE_FALSE(n.hasZone("S"));
    n.addZone("S", "root", WorldBounds{100.0f, 100.0f, 500.0f, 500.0f});   // appears at runtime
    REQUIRE(n.hasZone("S"));
    n.setActive("S");
    REQUIRE(n.activeZone() == "S");
    REQUIRE(std::fabs(n.target().zoom - 2.5f) < 1e-3f);                     // 1000/400
}

TEST_CASE("ZoneNavigator: re-adding a zone updates its bounds, keeps children", "[unit][zonenav]") {
    ZoneNavigator n = makeNav3();
    n.setActive("S");
    REQUIRE(std::fabs(n.target().zoom - 2.5f) < 1e-3f);                     // 400x400
    n.addZone("S", "root", WorldBounds{100.0f, 100.0f, 300.0f, 300.0f});   // resized to 200x200
    n.setActive("S");
    REQUIRE(std::fabs(n.target().zoom - 5.0f) < 1e-3f);                     // 1000/200
    REQUIRE(n.hasZone("P"));                                                // child preserved
}

TEST_CASE("ZoneNavigator: back-out is seamless (the view glides out)", "[unit][zonenav]") {
    ZoneNavigator n = makeNav3();
    n.setActive("P");
    for (int i = 0; i < 100; ++i) n.update(0.05f);   // settle inside P (framing 10)
    REQUIRE(std::fabs(n.view().zoom - 10.0f) < 0.1f);
    n.removeZone("S");                                // active vanishes -> back out to root
    REQUIRE(n.activeZone() == "root");
    for (int i = 0; i < 200; ++i) n.update(0.05f);   // glide out, not a jump
    REQUIRE(std::fabs(n.view().zoom - 1.0f) < 0.05f); // now framing the root
}
