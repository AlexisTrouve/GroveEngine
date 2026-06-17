/**
 * Unit Tests: grove::anim::Easing (animation system, slice 2a).
 *
 * WHAT  : Locks the easing-curve abstraction — the single extension point for ALL
 *         interpolation in the animation system (and reusable for UI/camera tweens).
 *         applyEasing(curve, t) reshapes a normalized t in [0,1]; ease(curve, a, b, t)
 *         is the generic value interpolation tracks use.
 *
 * WHY    : Decoupling the CURVE from the track keeps the system modular — adding a curve is
 *         one function here, never a switch edit in Track/Clip. This is what makes "best
 *         possible" (rich curves) coexist with "max modularité/propreté".
 *
 * HOW    : Pure std-only math, no allocation. Curves are normalized: every curve maps 0->0
 *         and 1->1; t is clamped to [0,1]. Step holds 0 until the segment end.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/anim/Easing.h"

using namespace grove::anim;
using Catch::Matchers::WithinAbs;

TEST_CASE("Easing - every curve pins the endpoints 0->0 and 1->1", "[anim][easing][unit]") {
    const Easing all[] = {
        Easing::Step, Easing::Linear,
        Easing::InQuad, Easing::OutQuad, Easing::InOutQuad,
        Easing::InCubic, Easing::OutCubic, Easing::InOutCubic,
    };
    for (Easing e : all) {
        REQUIRE_THAT(applyEasing(e, 0.0f), WithinAbs(0.0f, 0.0001f));
        REQUIRE_THAT(applyEasing(e, 1.0f), WithinAbs(1.0f, 0.0001f));
    }
}

TEST_CASE("Easing - input is clamped to [0,1]", "[anim][easing][unit]") {
    REQUIRE_THAT(applyEasing(Easing::Linear, -0.5f), WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(applyEasing(Easing::Linear,  1.5f), WithinAbs(1.0f, 0.0001f));
    REQUIRE_THAT(applyEasing(Easing::InCubic, -3.0f), WithinAbs(0.0f, 0.0001f));
}

TEST_CASE("Easing - Step holds 0 until the very end", "[anim][easing][unit]") {
    REQUIRE_THAT(applyEasing(Easing::Step, 0.0f),  WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(applyEasing(Easing::Step, 0.99f), WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(applyEasing(Easing::Step, 1.0f),  WithinAbs(1.0f, 0.0001f));
}

TEST_CASE("Easing - characteristic midpoints", "[anim][easing][unit]") {
    REQUIRE_THAT(applyEasing(Easing::Linear,     0.5f), WithinAbs(0.5f,   0.0001f));
    REQUIRE_THAT(applyEasing(Easing::InQuad,     0.5f), WithinAbs(0.25f,  0.0001f));
    REQUIRE_THAT(applyEasing(Easing::OutQuad,    0.5f), WithinAbs(0.75f,  0.0001f));
    REQUIRE_THAT(applyEasing(Easing::InOutQuad,  0.5f), WithinAbs(0.5f,   0.0001f));
    REQUIRE_THAT(applyEasing(Easing::InCubic,    0.5f), WithinAbs(0.125f, 0.0001f));
    REQUIRE_THAT(applyEasing(Easing::OutCubic,   0.5f), WithinAbs(0.875f, 0.0001f));
    REQUIRE_THAT(applyEasing(Easing::InOutCubic, 0.5f), WithinAbs(0.5f,   0.0001f));
}

TEST_CASE("Easing - ease() interpolates between values via the curve", "[anim][easing][unit]") {
    // Linear: straight average.
    REQUIRE_THAT(ease(Easing::Linear, 10.0f, 20.0f, 0.5f), WithinAbs(15.0f, 0.0001f));
    // InQuad at 0.5 = 0.25 -> 0..100 gives 25.
    REQUIRE_THAT(ease(Easing::InQuad, 0.0f, 100.0f, 0.5f), WithinAbs(25.0f, 0.0001f));
    // Endpoints regardless of curve.
    REQUIRE_THAT(ease(Easing::InOutCubic, -5.0f, 5.0f, 0.0f), WithinAbs(-5.0f, 0.0001f));
    REQUIRE_THAT(ease(Easing::InOutCubic, -5.0f, 5.0f, 1.0f), WithinAbs(5.0f,  0.0001f));
}

TEST_CASE("Easing - ease-out is always ahead of ease-in on (0,1)", "[anim][easing][unit]") {
    // Sanity that the In/Out curves are distinct and ordered as expected on the interior.
    for (float t = 0.1f; t < 1.0f; t += 0.1f) {
        REQUIRE(applyEasing(Easing::OutQuad, t) >= applyEasing(Easing::InQuad, t));
        REQUIRE(applyEasing(Easing::OutCubic, t) >= applyEasing(Easing::InCubic, t));
    }
}
