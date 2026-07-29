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

// ============================================================================
// L3 — cone lights. The angular mask, on top of the radial falloff.
//
// Convention borrowed from grove::fx::Emitter (dirDeg / spreadDeg, degrees, 0 = +x, 90 = +y
// screen-down, 360 = omni) rather than from render:sector's a0/a1 radians. The reason is practical,
// not aesthetic: a thruster's flame emitter and the light it casts then take THE SAME NUMBERS, so a
// game writes the cone once.
//
// The shader works on the COSINE of the angle to the cone axis (a dot product), never atan2 — which
// also makes omni fall out with no branch at all: cos(180°) = -1 passes everything.
// ============================================================================

TEST_CASE("cone: the default is omni and it is EXACTLY 1 everywhere", "[light][unit][cone]") {
    Light2D l;
    l.cx = 0.0f; l.cy = 0.0f; l.radius = 10.0f;
    REQUIRE(l.spreadDeg == 360.0f);           // the default IS the non-regression

    // Exactly 1, not "about 1": every existing radial light must come out byte-identical.
    for (float deg = 0.0f; deg < 360.0f; deg += 15.0f) {
        const float rad = deg * 3.14159265f / 180.0f;
        REQUIRE_THAT(coneFactor(std::cos(rad), std::sin(rad), l), WithinAbs(1.0f, 1e-6f));
    }
}

TEST_CASE("cone: lights forward, not backward", "[light][unit][cone]") {
    Light2D l;
    l.radius = 10.0f;
    l.dirDeg = 0.0f;          // pointing +x
    l.spreadDeg = 60.0f;      // ±30°

    REQUIRE_THAT(coneFactor(1.0f, 0.0f, l), WithinAbs(1.0f, 1e-6f));   // straight down the axis
    REQUIRE_THAT(coneFactor(-1.0f, 0.0f, l), WithinAbs(0.0f, 1e-6f));  // straight behind: nothing

    // Just outside the half-angle: nothing. This is the assertion that separates a cone from a
    // disc — a light that ignored the cone entirely would return 1 here.
    const float justOutside = 35.0f * 3.14159265f / 180.0f;
    REQUIRE_THAT(coneFactor(std::cos(justOutside), std::sin(justOutside), l), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("cone: the rim is SOFT, not a pie slice", "[light][unit][cone]") {
    Light2D l;
    l.radius = 10.0f; l.dirDeg = 0.0f; l.spreadDeg = 90.0f;   // ±45°

    // A hard cut reads as a cardboard pie slice — the eye catches the discontinuity exactly as it
    // does on a linear radial falloff. So there must be at least one angle strictly between 0 and 1.
    bool sawPartial = false;
    for (float deg = 0.0f; deg <= 45.0f; deg += 1.0f) {
        const float rad = deg * 3.14159265f / 180.0f;
        const float f = coneFactor(std::cos(rad), std::sin(rad), l);
        if (f > 0.02f && f < 0.98f) sawPartial = true;
    }
    REQUIRE(sawPartial);

    // ...and it stays monotonic from the axis outward: no bump, no ringing.
    float prev = 2.0f;
    for (float deg = 0.0f; deg <= 60.0f; deg += 2.0f) {
        const float rad = deg * 3.14159265f / 180.0f;
        const float f = coneFactor(std::cos(rad), std::sin(rad), l);
        REQUIRE(f <= prev + 1e-6f);
        prev = f;
    }
}

TEST_CASE("cone: dirDeg rotates it, 90 = screen-down (fx convention)", "[light][unit][cone]") {
    Light2D l;
    l.radius = 10.0f; l.dirDeg = 90.0f; l.spreadDeg = 60.0f;

    // 90° = +y = screen-DOWN, matching grove::fx::Emitter. Getting this wrong would point every
    // thruster light the opposite way from its flame.
    REQUIRE_THAT(coneFactor(0.0f, 1.0f, l), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(coneFactor(0.0f, -1.0f, l), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("cone: contribution() honours it too", "[light][unit][cone]") {
    // The gameplay query ("is this point lit?") must agree with what the screen shows, or a stealth
    // rule would disagree with the picture the player sees.
    Light2D l;
    l.cx = 0.0f; l.cy = 0.0f; l.radius = 10.0f;
    l.r = 1.0f; l.g = 1.0f; l.b = 1.0f;
    l.dirDeg = 0.0f; l.spreadDeg = 60.0f;

    float r, g, b;
    contribution(l, 5.0f, 0.0f, r, g, b);      // in front, half way out
    REQUIRE(r > 0.2f);

    contribution(l, -5.0f, 0.0f, r, g, b);     // behind, same distance
    REQUIRE_THAT(r, WithinAbs(0.0f, 1e-6f));
}
