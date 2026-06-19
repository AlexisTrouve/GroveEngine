// ============================================================================
// test_zoom_ladder.cpp — objective unit test for grove::camera::ZoomLadder.
//
// QUOI   : verrouille snap()/blend() (ZoomLadder.h) avec des oracles analytiques —
//   pas de GPU, pas de caméra, pas d'IIO. C'est du math pur (espace log + smoothstep).
// POURQUOI : le continuum de zoom a besoin de plateaux LISIBLES (l'échelle se pose) et
//   d'une transition inter-strates seamless. Un bug d'espace (linéaire au lieu de log)
//   ou de fenêtre de transition donnerait des plateaux qui dérivent ou une bouillie.
// COMMENT : strates galaxy/system/ship/interior (zoom croissant), largeur de transition
//   0.5 (la moitié médiane de chaque écart fait la rampe, les quarts extérieurs = plateau).
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include "Scene/ZoomLadder.h"
#include <cmath>
#include <vector>

using namespace grove::camera;

// galaxy / system / ship / interior — plateaux de zoom croissants.
static ZoomLadder makeLadder() {
    return ZoomLadder(std::vector<float>{0.05f, 0.5f, 4.0f, 16.0f}, 0.5f);
}

TEST_CASE("ZoomLadder: at a plateau -> fully that strata (t=0)", "[unit][zoom]") {
    auto L = makeLadder();
    auto b = L.blend(0.5f);              // pile sur le plateau 'system' (index 1)
    REQUIRE(b.active == 1);
    REQUIRE(b.t < 0.001f);
}

TEST_CASE("ZoomLadder: clamps below first / above last plateau", "[unit][zoom]") {
    auto L = makeLadder();
    auto lo = L.blend(0.001f);          // sous galaxy
    REQUIRE(lo.active == 0); REQUIRE(lo.lower == 0); REQUIRE(lo.upper == 0); REQUIRE(lo.t < 0.001f);
    auto hi = L.blend(1000.0f);         // au-dessus d'interior
    REQUIRE(hi.active == 3); REQUIRE(hi.lower == 3); REQUIRE(hi.upper == 3); REQUIRE(hi.t < 0.001f);
}

TEST_CASE("ZoomLadder: geometric midpoint between strata is mid-transition", "[unit][zoom]") {
    auto L = makeLadder();
    const float mid = std::sqrt(0.5f * 4.0f);   // moyenne géométrique system/ship -> u=0.5 en log
    auto b = L.blend(mid);
    REQUIRE(b.lower == 1);
    REQUIRE(b.upper == 2);
    REQUIRE(std::fabs(b.t - 0.5f) < 0.001f);    // smoothstep(0.25, 0.75, 0.5) = 0.5
}

TEST_CASE("ZoomLadder: near a plateau stays locked (flat plateau, t pinned)", "[unit][zoom]") {
    auto L = makeLadder();
    auto justAbove = L.blend(0.6f);     // juste au-dessus de system(0.5): u<0.25 -> t=0, active=1
    REQUIRE(justAbove.active == 1);
    REQUIRE(justAbove.t < 0.001f);
    auto nearShip = L.blend(3.5f);      // proche de ship(4.0): u>0.75 -> t=1, active=2
    REQUIRE(nearShip.active == 2);
    REQUIRE(nearShip.t > 0.999f);
}

TEST_CASE("ZoomLadder: snap returns the nearest plateau (log space)", "[unit][zoom]") {
    auto L = makeLadder();
    REQUIRE(L.snap(0.6f)    == 0.5f);   // plus proche de system que de ship
    REQUIRE(L.snap(3.5f)    == 4.0f);   // plus proche de ship
    REQUIRE(L.snap(0.001f)  == 0.05f);  // clamp -> galaxy
    REQUIRE(L.snap(1000.0f) == 16.0f);  // clamp -> interior
}

TEST_CASE("ZoomLadder: degenerate ladders are safe", "[unit][zoom]") {
    ZoomLadder empty;
    REQUIRE(empty.count() == 0);
    REQUIRE(empty.snap(2.0f) == 2.0f);          // pas de plateaux -> identité
    auto b = empty.blend(2.0f);
    REQUIRE(b.active == 0); REQUIRE(b.t < 0.001f);

    ZoomLadder one(std::vector<float>{1.0f});
    auto b1 = one.blend(50.0f);
    REQUIRE(b1.active == 0); REQUIRE(b1.lower == 0); REQUIRE(b1.upper == 0);
}
