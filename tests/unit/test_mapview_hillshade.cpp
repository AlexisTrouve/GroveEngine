/**
 * Unit Tests: grove::mapview::Hillshade (map-viewer engine, slice S1e).
 *
 * WHAT  : Locks the relief-shading math — the illumination factor a Layer multiplies its palette colour by.
 *         Verifies the Lambertian model's defining behaviours without leaning on any azimuth/aspect convention:
 *         flat-ground = light.z, overhead darkens slopes monotonically and symmetrically, a directional light
 *         brightens the slope tilted toward it, and zFactor exaggerates the effect.
 *
 * WHY    : Hillshade is the most legible cue on a top-down map; a sign error would invert the relief. Locking
 *         the pure factor here means the integration (gradient sampling) only has to get the gradient right.
 *
 * HOW    : Catch2; exact values via WithinAbs, directional relationships via strict inequalities.
 */

#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/mapview/Hillshade.h"

using namespace grove::mapview;
using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

TEST_CASE("mapview S1e - flat ground returns the light's z component", "[mapview][hillshade][unit]") {
    // Overhead light: flat ground is fully lit.
    REQUIRE_THAT(Hillshade(0, 0, 1).factor(0.0, 0.0), WithinAbs(1.0, 1e-9));
    // Light at 45° in the x-z plane: flat ground = sin(45°) = 1/sqrt(2).
    REQUIRE_THAT(Hillshade(1, 0, 1).factor(0.0, 0.0), WithinAbs(1.0 / std::sqrt(2.0), 1e-9));
}

TEST_CASE("mapview S1e - overhead light darkens slopes monotonically", "[mapview][hillshade][unit]") {
    const Hillshade h(0, 0, 1);  // straight down
    const double flat = h.factor(0.0, 0.0);
    const double gentle = h.factor(0.5, 0.0);
    const double steep = h.factor(2.0, 0.0);
    REQUIRE(flat > gentle);
    REQUIRE(gentle > steep);
    // Exact: 1/sqrt(g^2+1).
    REQUIRE_THAT(gentle, WithinAbs(1.0 / std::sqrt(1.25), 1e-9));
    REQUIRE_THAT(steep, WithinAbs(1.0 / std::sqrt(5.0), 1e-9));
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
    REQUIRE(h.factor(-1.0, 0.0) > 0.99);  // normal (1,0,1) parallel to light (1,0,1)
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
