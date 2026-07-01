/**
 * Unit Tests: grove::mapview::Hillshade (map-viewer engine, slice S1e + soft-shade fix).
 *
 * WHAT  : Locks the relief-shading math — the illumination factor a Layer multiplies its palette colour by.
 *         Verifies the model's defining behaviours without leaning on any azimuth/aspect convention: flat
 *         ground under overhead light is fully lit, slopes darken monotonically & symmetrically, a directional
 *         light brightens the slope facing it, zFactor exaggerates. AND locks the soft-shade fix: no finite
 *         gradient is ever pure black, and distinct away-slopes stay distinct (the old hard `max(0,d)` clamp
 *         crushed every back-slope to identical black — "des shades SUPER chelou").
 *
 * WHY    : Hillshade is the most legible cue on a top-down map; a sign error would invert the relief, and a
 *         hard clamp blots whole hillsides into one flat black. Locking the pure factor here means the
 *         integration (gradient sampling) only has to get the gradient right.
 *
 * HOW    : Catch2; exact values via the model formula (encoding the spec), relationships via strict
 *         inequalities. `model(d, ambient)` mirrors Hillshade's curve so the exact-value cases document it.
 */

#include <algorithm>
#include <cmath>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/mapview/Hillshade.h"

using namespace grove::mapview;
using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

// The shading curve spec: squared half-Lambert lifted by an ambient floor (mirrors Hillshade::factor).
static double model(double d, double ambient) {
    const double lit = 0.5 + 0.5 * d;
    return ambient + (1.0 - ambient) * lit * lit;
}

TEST_CASE("mapview S1e - flat ground under overhead light is fully lit", "[mapview][hillshade][unit]") {
    // Overhead light, flat ground: d = 1 -> fully lit regardless of ambient.
    REQUIRE_THAT(Hillshade(0, 0, 1).factor(0.0, 0.0), WithinAbs(1.0, 1e-9));
    // Light at 45° in the x-z plane, flat ground: d = sin(45°) = 1/sqrt(2), shaded by the wrap curve.
    const Hillshade h(1, 0, 1);
    REQUIRE_THAT(h.factor(0.0, 0.0), WithinAbs(model(1.0 / std::sqrt(2.0), h.ambient()), 1e-9));
}

TEST_CASE("mapview S1e - overhead light darkens slopes monotonically", "[mapview][hillshade][unit]") {
    const Hillshade h(0, 0, 1);  // straight down
    const double flat = h.factor(0.0, 0.0);
    const double gentle = h.factor(0.5, 0.0);
    const double steep = h.factor(2.0, 0.0);
    REQUIRE(flat > gentle);
    REQUIRE(gentle > steep);
    // Exact via the model: overhead d = 1/sqrt(g^2+1).
    REQUIRE_THAT(gentle, WithinAbs(model(1.0 / std::sqrt(1.25), h.ambient()), 1e-9));
    REQUIRE_THAT(steep, WithinAbs(model(1.0 / std::sqrt(5.0), h.ambient()), 1e-9));
}

TEST_CASE("mapview S1e - overhead light is symmetric in slope sign", "[mapview][hillshade][unit]") {
    const Hillshade h(0, 0, 1);
    REQUIRE_THAT(h.factor(0.7, 0.0), WithinAbs(h.factor(-0.7, 0.0), 1e-12));
    REQUIRE_THAT(h.factor(0.0, 0.7), WithinAbs(h.factor(0.0, -0.7), 1e-12));
}

TEST_CASE("mapview S1e - a directional light brightens the slope facing it", "[mapview][hillshade][unit]") {
    const Hillshade h(1, 0, 1);  // light from +x, 45° up
    // dz/dx < 0 -> surface normal tilts toward +x -> toward the light -> brighter than the opposite slope.
    REQUIRE(h.factor(-0.5, 0.0) > h.factor(0.5, 0.0));
    // The slope whose normal best aligns with the light approaches full illumination.
    REQUIRE(h.factor(-1.0, 0.0) > 0.99);  // normal (1,0,1) parallel to light (1,0,1) -> d=1
}

TEST_CASE("mapview S1e - zFactor exaggerates the relief", "[mapview][hillshade][unit]") {
    const double g = 0.5;
    const Hillshade flat(0, 0, 1, 1.0);
    const Hillshade tall(0, 0, 1, 2.0);
    // More exaggeration -> the same gradient reads as a steeper slope -> darker under overhead light.
    REQUIRE(tall.factor(g, 0.0) < flat.factor(g, 0.0));
}

TEST_CASE("mapview S1e - fromAzimuthAltitude: 90 degrees altitude is overhead", "[mapview][hillshade][unit]") {
    const Hillshade overhead = Hillshade::fromAzimuthAltitude(0.0, kPi / 2.0);
    REQUIRE_THAT(overhead.factor(0.0, 0.0), WithinAbs(1.0, 1e-9));
    // A low sun (10°) leaves the flat dimmer than a high sun (80°).
    const Hillshade low = Hillshade::fromAzimuthAltitude(0.0, 10.0 * kPi / 180.0);
    const Hillshade high = Hillshade::fromAzimuthAltitude(0.0, 80.0 * kPi / 180.0);
    REQUIRE(low.factor(0.0, 0.0) < high.factor(0.0, 0.0));
}

// ------------------------------------------------------------------------------------------------
// The soft-shade fix: no away-slope is ever pure black, and away-slopes stay DISTINCT (not crushed).
// Regression against the old hard `max(0,d)` clamp, which returned 0 for every back-slope -> identical black.
// ------------------------------------------------------------------------------------------------
TEST_CASE("mapview soft-shade - away-slopes darken but stay distinct and never pure black", "[mapview][hillshade][unit]") {
    const Hillshade h(1, 0, 1);  // light from +x

    // Two back-slopes (dz/dx > 0 => normal tilts AWAY from +x): both are shadowed, the steeper one darker.
    const double near = h.factor(2.0, 0.0);
    const double far  = h.factor(4.0, 0.0);
    REQUIRE(far < near);                 // steeper-away is darker (OLD model: both == 0 -> this failed)
    REQUIRE(near > 0.0);                 // never pure black (OLD model: == 0)
    REQUIRE(far  > 0.0);
    REQUIRE(near >= h.ambient());        // nothing dips below the ambient floor
    REQUIRE(far  >= h.ambient());
    // Even a near-vertical away-slope stays at/above the floor and finite.
    const double cliff = h.factor(1.0e6, 0.0);
    REQUIRE(std::isfinite(cliff));
    REQUIRE(cliff >= h.ambient());
}

TEST_CASE("mapview soft-shade - the shadow side keeps varying (not flat-lined at the floor)", "[mapview][hillshade][unit]") {
    const Hillshade h(1, 0, 1);  // light from +x; for g >= 1 the dot is <= 0 (the shadow side).
    // Sweep the shadow side and require the factor to keep DECREASING smoothly as the away-slope steepens.
    // The old hard clamp returned a constant 0 across this whole region -> strict-decrease fails on it (the bug),
    // and the small max-step proves there's no jump/crease.
    double prev = h.factor(1.0, 0.0);
    double maxStep = 0.0;
    for (double g = 1.0 + 0.05; g <= 6.0 + 1e-9; g += 0.05) {
        const double cur = h.factor(g, 0.0);
        REQUIRE(cur < prev);                           // strictly decreasing (OLD: constant 0 -> fails)
        REQUIRE(cur > 0.0);                            // never pure black (OLD: 0)
        maxStep = std::max(maxStep, prev - cur);
        prev = cur;
    }
    REQUIRE(maxStep < 0.05);                            // no jump -> smooth (a clamp would crease/jump)
}

TEST_CASE("mapview soft-shade - non-finite gradient folds to the ambient floor", "[mapview][hillshade][unit]") {
    const Hillshade h(0, 0, 1);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_THAT(h.factor(nan, 0.0), WithinAbs(h.ambient(), 1e-12));
    REQUIRE_THAT(h.factor(0.0, nan), WithinAbs(h.ambient(), 1e-12));
}
