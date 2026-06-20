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
#include <cmath>

using namespace grove::camera;

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
