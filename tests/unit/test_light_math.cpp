// ============================================================================
// LightMathUnit — the pure 2D light math (grove::light), oracle-tested headlessly.
//
// The falloff is evaluated PER PIXEL in the fragment shader, where nothing can be asserted. This
// pins the same curve as a plain function, so the GPU test has something to be right against and a
// game can ask "how lit is this point?" for gameplay without reading back a texture.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/light/Light.h>

using namespace grove::light;
using Catch::Matchers::WithinAbs;

TEST_CASE("attenuation: 1 at the centre, EXACTLY 0 at and past the rim", "[light][unit]") {
    REQUIRE_THAT(attenuation(0.0f, 10.0f), WithinAbs(1.0f, 1e-6f));

    // Exactly zero at the rim is not cosmetic: it is what makes a quad of side 2r a SUFFICIENT
    // bound. A curve that only approached zero would leak light outside the quad, and the seam
    // would show as a square edge around every lamp.
    REQUIRE_THAT(attenuation(10.0f, 10.0f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(attenuation(11.0f, 10.0f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(attenuation(1e6f, 10.0f),  WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("attenuation: squared, not linear", "[light][unit]") {
    // Half way out, a LINEAR falloff would give 0.5. The squared curve gives 0.25 — this assertion
    // is what separates the two, and the choice is deliberate: linear reads as a hard-edged disc
    // because the eye catches the derivative break at the rim.
    REQUIRE_THAT(attenuation(5.0f, 10.0f), WithinAbs(0.25f, 1e-6f));
    REQUIRE_THAT(attenuation(2.5f, 10.0f), WithinAbs(0.5625f, 1e-6f));

    // Monotonic decreasing all the way out — no bump, no ringing.
    float prev = 2.0f;
    for (float d = 0.0f; d <= 10.0f; d += 0.5f) {
        const float a = attenuation(d, 10.0f);
        REQUIRE(a <= prev);
        prev = a;
    }
}

TEST_CASE("attenuation: a degenerate radius lights nothing instead of dividing by zero",
          "[light][unit]") {
    REQUIRE_THAT(attenuation(0.0f, 0.0f),  WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(attenuation(1.0f, -5.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("bounds: a square of side 2r centred on the light", "[light][unit]") {
    Light2D l; l.cx = 100.0f; l.cy = 50.0f; l.radius = 8.0f;
    const Bounds b = bounds(l);

    // x,y = top-left CORNER while cx,cy = CENTRE — the engine's name-carries-the-anchor rule.
    REQUIRE_THAT(b.x, WithinAbs(92.0f, 1e-6f));
    REQUIRE_THAT(b.y, WithinAbs(42.0f, 1e-6f));
    REQUIRE_THAT(b.w, WithinAbs(16.0f, 1e-6f));
    REQUIRE_THAT(b.h, WithinAbs(16.0f, 1e-6f));

    // And the bound is SUFFICIENT: the attenuation is already zero at every corner, so a quad this
    // size clips nothing visible.
    const float halfDiag = 8.0f * 1.41421356f;
    REQUIRE_THAT(attenuation(halfDiag, l.radius), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("contribution: colour scaled by attenuation AND intensity", "[light][unit]") {
    Light2D l;
    l.cx = 0.0f; l.cy = 0.0f; l.radius = 10.0f;
    l.r = 1.0f; l.g = 0.5f; l.b = 0.0f;      // orange
    l.intensity = 2.0f;                       // deliberately >1: RGBA16F keeps the overbright

    float r, g, b;
    contribution(l, 0.0f, 0.0f, r, g, b);     // centre: attenuation 1
    REQUIRE_THAT(r, WithinAbs(2.0f, 1e-6f));  // 1.0 * 1 * 2 — NOT clamped to 1, that is the point
    REQUIRE_THAT(g, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(b, WithinAbs(0.0f, 1e-6f));

    contribution(l, 5.0f, 0.0f, r, g, b);     // half way: attenuation 0.25
    REQUIRE_THAT(r, WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(g, WithinAbs(0.25f, 1e-6f));

    contribution(l, 20.0f, 0.0f, r, g, b);    // outside: contributes nothing at all
    REQUIRE_THAT(r, WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(g, WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(b, WithinAbs(0.0f, 1e-6f));
}
