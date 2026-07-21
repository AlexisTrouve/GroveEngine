/**
 * Unit Tests: grove::ui::computeNineSlice — the pure 9-slice (nine-patch) geometry helper.
 *
 * WHAT : locks the geometry contract that turns one border texture into a continuous-border frame at any
 *        target size — (1) a large target yields the full 3×3 grid with corners at NATIVE source size and the
 *        centre filling the rest; (2) the 9 quads TILE the target exactly (widths across a row sum to dw,
 *        heights down a column sum to dh) with NO gap/overlap; (3) UV boundaries are contiguous and cover the
 *        whole source (0..1) so the sampled art is seamless; (4) a target narrower/shorter than its corners
 *        SQUEEZES the corners (no overlap) and drops the centre band; (5) degenerate source/target -> 0 quads.
 *
 * WHY  : this is the math every textured button/window border rides on; a regression here (a wrong UV split,
 *        a non-tiling layout) shows up as a seam or a stretched corner — exactly the "looks right in code"
 *        the doctrine forbids. Pure + headless, so it runs every ctest as the cheap first line of proof
 *        (the GPU --shot is the second, end-to-end line).
 *
 * HOW  : Catch2, no GPU. Assertions compare exact spans (epsilon for float) against the nine-patch definition.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/ui/NineSlice.h"

using grove::ui::NinePatch;
using grove::ui::NinePatchQuad;
using grove::ui::computeNineSlice;
using Catch::Matchers::WithinAbs;

namespace {
// A 32×32 source with uniform 8px margins — the canonical nine-patch used across the cases below.
NinePatch uniform8() { return NinePatch{32.0f, 32.0f, 8.0f, 8.0f, 8.0f, 8.0f}; }
}

TEST_CASE("computeNineSlice: large target -> full 3x3 grid, native corners, filled centre", "[nineslice]") {
    NinePatchQuad q[9];
    const int n = computeNineSlice(uniform8(), 100.0f, 200.0f, 128.0f, 64.0f, q);
    REQUIRE(n == 9);   // every band is non-degenerate

    // Row-major order: [0]=TL, [1]=TC, [2]=TR, [3]=ML, [4]=MC(centre), [5]=MR, [6]=BL, [7]=BC, [8]=BR.
    // Top-left corner: at the target origin, NATIVE 8×8, sampling the source's top-left 8px UV block.
    REQUIRE_THAT(q[0].x, WithinAbs(100.0, 1e-4)); REQUIRE_THAT(q[0].y, WithinAbs(200.0, 1e-4));
    REQUIRE_THAT(q[0].w, WithinAbs(8.0, 1e-4));   REQUIRE_THAT(q[0].h, WithinAbs(8.0, 1e-4));
    REQUIRE_THAT(q[0].u0, WithinAbs(0.0, 1e-6));  REQUIRE_THAT(q[0].u1, WithinAbs(0.25, 1e-6));  // 8/32
    REQUIRE_THAT(q[0].v0, WithinAbs(0.0, 1e-6));  REQUIRE_THAT(q[0].v1, WithinAbs(0.25, 1e-6));

    // Centre (index 4): stretched to fill the interior (128-16 wide, 64-16 tall), sampling the middle UV band.
    REQUIRE_THAT(q[4].x, WithinAbs(108.0, 1e-4)); REQUIRE_THAT(q[4].y, WithinAbs(208.0, 1e-4));
    REQUIRE_THAT(q[4].w, WithinAbs(112.0, 1e-4)); REQUIRE_THAT(q[4].h, WithinAbs(48.0, 1e-4));
    REQUIRE_THAT(q[4].u0, WithinAbs(0.25, 1e-6)); REQUIRE_THAT(q[4].u1, WithinAbs(0.75, 1e-6));
    REQUIRE_THAT(q[4].v0, WithinAbs(0.25, 1e-6)); REQUIRE_THAT(q[4].v1, WithinAbs(0.75, 1e-6));

    // Bottom-right corner (index 8): native 8×8, anchored to the target's bottom-right, top-right UV... etc.
    REQUIRE_THAT(q[8].x, WithinAbs(220.0, 1e-4)); REQUIRE_THAT(q[8].y, WithinAbs(256.0, 1e-4)); // 100+128-8, 200+64-8
    REQUIRE_THAT(q[8].w, WithinAbs(8.0, 1e-4));   REQUIRE_THAT(q[8].h, WithinAbs(8.0, 1e-4));
    REQUIRE_THAT(q[8].u0, WithinAbs(0.75, 1e-6)); REQUIRE_THAT(q[8].u1, WithinAbs(1.0, 1e-6));
    REQUIRE_THAT(q[8].v0, WithinAbs(0.75, 1e-6)); REQUIRE_THAT(q[8].v1, WithinAbs(1.0, 1e-6));
}

TEST_CASE("computeNineSlice: the 9 quads tile the target exactly (no gap/overlap)", "[nineslice]") {
    NinePatchQuad q[9];
    const float dx = 10.0f, dy = 20.0f, dw = 200.0f, dh = 90.0f;
    const int n = computeNineSlice(uniform8(), dx, dy, dw, dh, q);
    REQUIRE(n == 9);

    // Top row (indices 0,1,2) widths sum to dw and start exactly at dx, contiguous.
    REQUIRE_THAT(q[0].x, WithinAbs(dx, 1e-4));
    REQUIRE_THAT(q[0].x + q[0].w, WithinAbs(q[1].x, 1e-4));   // TL right edge == TC left edge
    REQUIRE_THAT(q[1].x + q[1].w, WithinAbs(q[2].x, 1e-4));   // TC right edge == TR left edge
    REQUIRE_THAT(q[2].x + q[2].w, WithinAbs(dx + dw, 1e-4));  // TR right edge == target right edge

    // Left column (indices 0,3,6) heights sum to dh and are contiguous.
    REQUIRE_THAT(q[0].y, WithinAbs(dy, 1e-4));
    REQUIRE_THAT(q[0].y + q[0].h, WithinAbs(q[3].y, 1e-4));
    REQUIRE_THAT(q[3].y + q[3].h, WithinAbs(q[6].y, 1e-4));
    REQUIRE_THAT(q[6].y + q[6].h, WithinAbs(dy + dh, 1e-4));
}

TEST_CASE("computeNineSlice: UV bands are contiguous and cover the whole source", "[nineslice]") {
    NinePatchQuad q[9];
    const int n = computeNineSlice(uniform8(), 0.0f, 0.0f, 300.0f, 150.0f, q);
    REQUIRE(n == 9);

    // Horizontal UV split shared by every row: 0 -> 0.25 -> 0.75 -> 1 (left / centre / right), no gap.
    REQUIRE_THAT(q[0].u0, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(q[0].u1, WithinAbs(q[1].u0, 1e-6));
    REQUIRE_THAT(q[1].u1, WithinAbs(q[2].u0, 1e-6));
    REQUIRE_THAT(q[2].u1, WithinAbs(1.0, 1e-6));
    // Vertical UV split down the left column: 0 -> 0.25 -> 0.75 -> 1, no gap.
    REQUIRE_THAT(q[0].v0, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(q[0].v1, WithinAbs(q[3].v0, 1e-6));
    REQUIRE_THAT(q[3].v1, WithinAbs(q[6].v0, 1e-6));
    REQUIRE_THAT(q[6].v1, WithinAbs(1.0, 1e-6));
}

TEST_CASE("computeNineSlice: target smaller than corners squeezes them, drops the centre", "[nineslice]") {
    NinePatchQuad q[9];
    // Target 10 wide but corners want 8+8=16 -> horizontal squeeze (scale 10/16); tall enough vertically.
    const int n = computeNineSlice(uniform8(), 0.0f, 0.0f, 10.0f, 64.0f, q);

    // No centre COLUMN (width 0) -> only 2 columns survive × 3 rows = 6 quads.
    REQUIRE(n == 6);

    // The two surviving columns still tile the 10px width exactly and never overlap.
    // First quad is the (squeezed) top-left corner at x=0; the right corner ends exactly at dw.
    REQUIRE_THAT(q[0].x, WithinAbs(0.0, 1e-4));
    REQUIRE_THAT(q[0].w, WithinAbs(5.0, 1e-4));            // 8 * (10/16)
    REQUIRE_THAT(q[1].x + q[1].w, WithinAbs(10.0, 1e-4));  // right column reaches the target's right edge
    // The squeezed corner keeps its FULL UV block (art is compressed, not clipped) — seam-free.
    REQUIRE_THAT(q[0].u0, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(q[0].u1, WithinAbs(0.25, 1e-6));
}

TEST_CASE("computeNineSlice: degenerate source or target -> 0 quads", "[nineslice]") {
    NinePatchQuad q[9];
    REQUIRE(computeNineSlice(NinePatch{0.0f, 32.0f, 8, 8, 8, 8}, 0, 0, 100, 100, q) == 0);   // srcW=0
    REQUIRE(computeNineSlice(NinePatch{32.0f, 0.0f, 8, 8, 8, 8}, 0, 0, 100, 100, q) == 0);   // srcH=0
    REQUIRE(computeNineSlice(uniform8(), 0, 0, 0.0f, 100.0f, q) == 0);                        // dw=0
    REQUIRE(computeNineSlice(uniform8(), 0, 0, 100.0f, -5.0f, q) == 0);                       // dh<0
}

TEST_CASE("computeNineSlice: zero margins -> a single centre quad covering the target", "[nineslice]") {
    NinePatchQuad q[9];
    // No border at all: the whole thing is the centre band, one quad over the full [0,1] UV.
    const int n = computeNineSlice(NinePatch{16.0f, 16.0f, 0, 0, 0, 0}, 5.0f, 6.0f, 40.0f, 30.0f, q);
    REQUIRE(n == 1);
    REQUIRE_THAT(q[0].x, WithinAbs(5.0, 1e-4)); REQUIRE_THAT(q[0].y, WithinAbs(6.0, 1e-4));
    REQUIRE_THAT(q[0].w, WithinAbs(40.0, 1e-4)); REQUIRE_THAT(q[0].h, WithinAbs(30.0, 1e-4));
    REQUIRE_THAT(q[0].u0, WithinAbs(0.0, 1e-6)); REQUIRE_THAT(q[0].u1, WithinAbs(1.0, 1e-6));
    REQUIRE_THAT(q[0].v0, WithinAbs(0.0, 1e-6)); REQUIRE_THAT(q[0].v1, WithinAbs(1.0, 1e-6));
}
