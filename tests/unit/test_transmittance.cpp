// ============================================================================
// TransmittanceUnit — the polar mapping + transmittance accumulation (lighting core, C1).
//
// This is the oracle for a GPU pass that does not exist yet. The socle stores, per light, a table
// T(angle, radius) = how much light SURVIVES from the lamp out to that distance in that direction,
// and the GPU builds it with a log2(N) prefix-product scan. None of that is assertable in a shader,
// so the same arithmetic lives here as plain functions and this test pins it.
//
// Everything the three plans (walls / filters / attenuators) will add reduces to feeding this: a
// wall writes 0, a filter writes a colour, fog writes exp(-alpha). See
// docs/design/lighting-transmittance-core.md.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/light/Transmittance.h>

#include <vector>

using namespace grove::light;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---------------------------------------------------------------------------
// Polar mapping — how a world point finds its cell in the table.
// ---------------------------------------------------------------------------

TEST_CASE("polar: the light's own centre maps to radius 0", "[light][unit][transmittance]") {
    Light2D l; l.cx = 100.0f; l.cy = 50.0f; l.radius = 20.0f;
    const PolarIndex p = toPolar(l, 100.0f, 50.0f);
    REQUIRE(p.inside);
    REQUIRE_THAT(p.radius01, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("polar: radius is normalised by the light's own radius", "[light][unit][transmittance]") {
    // radius01 is a TEXTURE coordinate, so it must be 0..1 across the light — not world units.
    // Normalising here rather than in the shader is what lets one table serve every light size.
    Light2D l; l.cx = 0.0f; l.cy = 0.0f; l.radius = 20.0f;

    REQUIRE_THAT(toPolar(l, 10.0f, 0.0f).radius01, WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(toPolar(l, 20.0f, 0.0f).radius01, WithinAbs(1.0f, 1e-6f));

    // Past the rim the light contributes nothing anyway, so the table need not cover it: the index
    // clamps instead of running off the end.
    const PolarIndex out = toPolar(l, 60.0f, 0.0f);
    REQUIRE_FALSE(out.inside);
    REQUIRE_THAT(out.radius01, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("polar: angle 0 = +x and 90 deg = +y, matching the CONE convention",
          "[light][unit][transmittance]") {
    // Same convention as Light2D::dirDeg (and therefore grove::fx::Emitter): 0 = +x, 90 = +y =
    // screen-DOWN. Two angular conventions inside one lighting system would guarantee that a cone
    // and its occlusion disagree by 90 degrees somewhere, and that bug would be invisible until a
    // wall shadowed the wrong side of a spotlight.
    Light2D l; l.cx = 0.0f; l.cy = 0.0f; l.radius = 10.0f;

    REQUIRE_THAT(toPolar(l, 5.0f, 0.0f).angle01, WithinAbs(0.0f, 1e-5f));      // +x
    REQUIRE_THAT(toPolar(l, 0.0f, 5.0f).angle01, WithinAbs(0.25f, 1e-5f));     // +y (down)
    REQUIRE_THAT(toPolar(l, -5.0f, 0.0f).angle01, WithinAbs(0.5f, 1e-5f));     // -x
    REQUIRE_THAT(toPolar(l, 0.0f, -5.0f).angle01, WithinAbs(0.75f, 1e-5f));    // -y (up)
}

TEST_CASE("polar: angle01 stays in [0,1) so it indexes a texture directly",
          "[light][unit][transmittance]") {
    Light2D l; l.cx = 0.0f; l.cy = 0.0f; l.radius = 10.0f;
    for (int i = 0; i < 360; ++i) {
        const float rad = static_cast<float>(i) * 3.14159265f / 180.0f;
        const float a = toPolar(l, 5.0f * std::cos(rad), 5.0f * std::sin(rad)).angle01;
        REQUIRE(a >= 0.0f);
        REQUIRE(a < 1.0f);
    }
}

TEST_CASE("polar: round-trips through fromPolar", "[light][unit][transmittance]") {
    Light2D l; l.cx = 30.0f; l.cy = -12.0f; l.radius = 25.0f;
    const float px = 42.0f, py = 3.0f;

    const PolarIndex p = toPolar(l, px, py);
    float bx, by;
    fromPolar(l, p.angle01, p.radius01, bx, by);

    REQUIRE_THAT(bx, WithinAbs(px, 1e-3f));
    REQUIRE_THAT(by, WithinAbs(py, 1e-3f));
}

// ---------------------------------------------------------------------------
// Transmittance — what survives a traversal.
// ---------------------------------------------------------------------------

TEST_CASE("transmit: vacuum changes nothing, whatever the distance", "[light][unit][transmittance]") {
    // The socle on its own must be strictly neutral — an all-ones table has to leave the render
    // byte-identical to L2. If this drifted by an epsilon, "the socle changes nothing" would stop
    // being provable.
    REQUIRE_THAT(transmitThrough(1.0f, 0.0f), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(transmitThrough(1.0f, 1.0f), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(transmitThrough(1.0f, 1000.0f), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("transmit: an opaque sample kills the ray, and zero distance does not",
          "[light][unit][transmittance]") {
    // A wall is transmittance 0 per unit. Crossing ANY of it leaves nothing...
    REQUIRE_THAT(transmitThrough(0.0f, 0.5f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(transmitThrough(0.0f, 1e-4f), WithinAbs(0.0f, 1e-6f));

    // ...but crossing NONE of it is not a traversal at all. pow(0,0) is 1 by convention and that is
    // the right answer here: a ray that has not moved has lost nothing.
    REQUIRE_THAT(transmitThrough(0.0f, 0.0f), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("transmit: thickness matters, multiplicatively", "[light][unit][transmittance]") {
    // Glass that passes 50% per unit passes 25% over two units — this is what makes filters and fog
    // ONE mechanism: discrete matter and a continuous medium differ only in how far you travel.
    REQUIRE_THAT(transmitThrough(0.5f, 1.0f), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(transmitThrough(0.5f, 2.0f), WithinAbs(0.25f, 1e-6f));
    REQUIRE_THAT(transmitThrough(0.5f, 3.0f), WithinAbs(0.125f, 1e-6f));
}

TEST_CASE("transmit: a density converts to Beer-Lambert", "[light][unit][transmittance]") {
    // fromDensity(alpha) = exp(-alpha), so transmitThrough(.., d) = exp(-alpha*d) exactly — the
    // Beer-Lambert law falls out of the multiplicative form instead of being bolted on.
    const float alpha = 0.7f;
    const float perUnit = fromDensity(alpha);
    for (float d : {0.0f, 0.5f, 1.0f, 3.0f}) {
        REQUIRE_THAT(transmitThrough(perUnit, d), WithinAbs(std::exp(-alpha * d), 1e-5f));
    }

    // Doubling the distance SQUARES what is left. That is the signature of an exponential, and the
    // assertion that separates it from any linear dimming.
    const float t1 = transmitThrough(perUnit, 1.0f);
    REQUIRE_THAT(transmitThrough(perUnit, 2.0f), WithinAbs(t1 * t1, 1e-5f));
}

// ---------------------------------------------------------------------------
// The accumulation — the CPU oracle for the GPU's prefix-product scan.
// ---------------------------------------------------------------------------

TEST_CASE("accumulate: an empty ray leaves the table at 1", "[light][unit][transmittance]") {
    // THE socle's own proof: nothing published anywhere means an all-ones table, so the render is
    // byte-identical to a build with no occlusion at all.
    std::vector<float> perUnit(8, 1.0f), out(8, 0.0f);
    accumulate(perUnit.data(), 8, 0.25f, out.data());
    for (float v : out) REQUIRE_THAT(v, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("accumulate: it is a PREFIX product, not a per-sample value",
          "[light][unit][transmittance]") {
    // Each cell is what survives from the LAMP to that distance, not what that one sample does. A
    // per-sample table would light the far side of a wall as brightly as the near side.
    const float perUnit[4] = { 0.5f, 0.5f, 1.0f, 0.5f };
    float out[4] = {};
    accumulate(perUnit, 4, 1.0f, out);

    REQUIRE_THAT(out[0], WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(out[1], WithinAbs(0.25f, 1e-6f));
    REQUIRE_THAT(out[2], WithinAbs(0.25f, 1e-6f));   // vacuum leaves it where it was
    REQUIRE_THAT(out[3], WithinAbs(0.125f, 1e-6f));

    // Monotonic non-increasing: light can only be lost along a ray, never regained.
    for (int i = 1; i < 4; ++i) REQUIRE(out[i] <= out[i - 1] + 1e-6f);
}

TEST_CASE("accumulate: one wall zeroes everything BEYOND it", "[light][unit][transmittance]") {
    // The wall is not a special case in the code — it is what a zero does to a running product.
    // That is the whole reason the three plans share one mechanism.
    const float perUnit[5] = { 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };
    float out[5] = {};
    accumulate(perUnit, 5, 1.0f, out);

    REQUIRE_THAT(out[0], WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(out[1], WithinAbs(1.0f, 1e-6f));    // still lit right up to the wall
    REQUIRE_THAT(out[2], WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(out[3], WithinAbs(0.0f, 1e-6f));    // and nothing comes back afterwards
    REQUIRE_THAT(out[4], WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("accumulate: the ORDER of the matter does not change the outcome",
          "[light][unit][transmittance]") {
    // The product is commutative, so red-then-blue equals blue-then-red at the END of the ray. This
    // is what spares the whole system a depth sort of occluders — and the reflex would have been to
    // build that sort first. (The values along the way differ, of course; only the total matches.)
    const float a[3] = { 0.8f, 0.4f, 0.9f };
    const float b[3] = { 0.4f, 0.9f, 0.8f };
    float outA[3] = {}, outB[3] = {};
    accumulate(a, 3, 1.0f, outA);
    accumulate(b, 3, 1.0f, outB);

    REQUIRE_THAT(outA[2], WithinRel(outB[2], 1e-5f));
}

TEST_CASE("accumulate: the step length scales the loss", "[light][unit][transmittance]") {
    // Halving the step must halve the exponent, not the value: two half-steps through the same
    // matter equal one whole step. A scan that ignored the step would make the result depend on the
    // table's resolution, which is the kind of defect that only shows when someone changes N.
    const float perUnit2[2] = { 0.5f, 0.5f };
    float half[2] = {};
    accumulate(perUnit2, 2, 0.5f, half);

    const float perUnit1[1] = { 0.5f };
    float whole[1] = {};
    accumulate(perUnit1, 1, 1.0f, whole);

    REQUIRE_THAT(half[1], WithinAbs(whole[0], 1e-6f));
}

// ---------------------------------------------------------------------------
// Plan F — the AUTHORING inverse: a tint over a thickness -> a per-unit value.
//
// The map stores transmittance PER UNIT, because fog demands it: a thicker cloud must absorb more.
// But an author writing a stained-glass window states the tint they want to SEE behind it, not a
// per-unit coefficient. Without the inverse they would have to hand-compute 0.3^(1/40) = 0.9703 to
// get a red pane, and every value they might type by instinct (0.3) would come out as a wall.
//
// perUnitForTint is that inverse, and the round-trip against transmitThrough is its whole contract.
// ---------------------------------------------------------------------------

TEST_CASE("perUnitForTint: round-trips through transmitThrough", "[light][unit][transmittance][filter]") {
    // The ONLY assertion that matters: state a tint, cross the stated thickness, get that tint back.
    // Anything weaker (e.g. "the result is below 1") would pass with an arbitrary darkening.
    const float thickness = 40.0f;
    const float perUnit = perUnitForTint(0.3f, thickness);

    REQUIRE_THAT(transmitThrough(perUnit, thickness), WithinRel(0.3f, 1e-4f));
}

TEST_CASE("perUnitForTint: crossing MORE than the stated thickness tints MORE",
          "[light][unit][transmittance][filter]") {
    // A ray entering the pane at an angle travels further inside it, and must come out darker. This
    // is what makes the conversion physical rather than a lookup: the tint is stated for ONE
    // perpendicular crossing, and the geometry does the rest.
    const float perUnit = perUnitForTint(0.5f, 10.0f);

    REQUIRE_THAT(transmitThrough(perUnit, 10.0f), WithinRel(0.5f, 1e-4f));
    REQUIRE_THAT(transmitThrough(perUnit, 20.0f), WithinRel(0.25f, 1e-4f));   // squared, not halved
}

TEST_CASE("perUnitForTint: the degenerate ends stay exact", "[light][unit][transmittance][filter]") {
    // Vacuum and opacity must survive the conversion EXACTLY, not to within an epsilon: a pane that
    // is supposed to do nothing has to multiply by exactly 1, or a stack of them would slowly
    // darken a scene that contains no matter at all.
    REQUIRE_THAT(perUnitForTint(1.0f, 25.0f), WithinAbs(1.0f, 0.0f));
    REQUIRE_THAT(perUnitForTint(0.0f, 25.0f), WithinAbs(0.0f, 0.0f));

    // A degenerate thickness cannot be inverted (1/0). It must fall back to "no matter", never to a
    // NaN that would poison the running product and blank the whole ray.
    REQUIRE_THAT(perUnitForTint(0.3f, 0.0f), WithinAbs(1.0f, 0.0f));
    REQUIRE_THAT(perUnitForTint(0.3f, -5.0f), WithinAbs(1.0f, 0.0f));
}

TEST_CASE("perUnitForTint: two panes stacked multiply, in either order",
          "[light][unit][transmittance][filter]") {
    // The oracle plan F asks for: superposed filters compose by PRODUCT. The renderer gets this from
    // a multiplicative blend into the occlusion map; here we pin what that blend is supposed to
    // produce, so a wrong blend mode has something to be wrong against.
    const float red  = perUnitForTint(0.4f, 10.0f);
    const float blue = perUnitForTint(0.7f, 10.0f);

    const float bothWays = transmitThrough(red * blue, 10.0f);
    REQUIRE_THAT(bothWays, WithinRel(0.4f * 0.7f, 1e-4f));

    const float swapped = transmitThrough(blue * red, 10.0f);
    REQUIRE_THAT(swapped, WithinRel(bothWays, 1e-5f));
}

// ---------------------------------------------------------------------------
// Plan A — a MEDIUM: absorption that compounds with the distance travelled through it.
//
// A filter states the tint after one crossing of a pane. A medium cannot: the whole point of a cloud
// is that going further through it absorbs MORE. So its authoring quantity is the Beer-Lambert
// coefficient alpha, which has no upper bound and is per-unit by nature — the opposite convention to
// a filter, deliberately, because the two things behave differently.
//
// `color` makes the absorption SELECTIVE: a channel with a lower colour absorbs faster. That is what
// gives sunsets (blue extinguishes before red) and coloured nebulae.
// ---------------------------------------------------------------------------

TEST_CASE("fogPerUnit: white is neutral — every channel absorbs at the stated density",
          "[light][unit][transmittance][fog]") {
    // The non-regression of the colour knob: a medium that states no colour preference must absorb
    // identically everywhere, i.e. fall back exactly onto fromDensity.
    REQUIRE_THAT(fogPerUnit(0.5f, 1.0f), WithinRel(fromDensity(0.5f), 1e-5f));
    REQUIRE_THAT(fogPerUnit(0.0f, 1.0f), WithinAbs(1.0f, 0.0f));   // no density = vacuum, EXACTLY
}

TEST_CASE("fogPerUnit: a darker channel absorbs FASTER", "[light][unit][transmittance][fog]") {
    // Halving a channel's colour doubles its extinction, so what survives is SQUARED. Asserting
    // merely "less survives" would pass with any monotonic darkening and would not pin the model.
    const float base = fogPerUnit(0.4f, 1.0f);
    const float half = fogPerUnit(0.4f, 0.5f);
    REQUIRE_THAT(half, WithinRel(base * base, 1e-4f));

    // ...and a channel with no colour at all is opaque: the medium is a wall for that wavelength.
    REQUIRE_THAT(fogPerUnit(0.4f, 0.0f), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("fogPerUnit: it is BEER-LAMBERT, not a subtraction", "[light][unit][transmittance][fog]") {
    // THE assertion of this slice. What characterises an exponential is that doubling the distance
    // SQUARES what survives. A constant darkening — the thing a naive implementation produces —
    // would halve it instead, and "it got darker" would be green for both.
    const float perUnit = fogPerUnit(0.3f, 1.0f);
    const float at10 = transmitThrough(perUnit, 10.0f);
    const float at20 = transmitThrough(perUnit, 20.0f);

    REQUIRE_THAT(at20, WithinRel(at10 * at10, 1e-4f));
    REQUIRE_THAT(at10, WithinRel(std::exp(-0.3f * 10.0f), 1e-4f));   // and it IS exp(-alpha*d)
}

TEST_CASE("fogPerUnit: doubling the DENSITY also squares what survives",
          "[light][unit][transmittance][fog]") {
    // The other half of the same law, and the one the GPU test mirrors: alpha and distance enter the
    // exponent together, so doubling either has the same effect.
    const float thin  = transmitThrough(fogPerUnit(0.2f, 1.0f), 8.0f);
    const float thick = transmitThrough(fogPerUnit(0.4f, 1.0f), 8.0f);
    REQUIRE_THAT(thick, WithinRel(thin * thin, 1e-4f));
}
