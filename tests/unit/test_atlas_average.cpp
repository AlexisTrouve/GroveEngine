/**
 * Unit Tests: per-layer average colour of a tileset array (tilemap LOD, slice S0) — pure, GPU-free.
 *
 * WHAT  : reduce each layer of a texture2DArray blob (the output of AtlasSlice::sliceToArray) to ONE
 *         RGBA8 colour — the colour that layer reads as when it is too small to see. That table is
 *         what fills the zoom-out LOD texture, so a tileset's dezoomed colours ARE its art.
 *
 * WHY   : the zoom-out band cannot sample the atlas (the R16UI index texture is POINT/CLAMP — tile
 *         ids can never be filtered), so the LOD colour must be precomputed per tile type. Deriving
 *         it from the tileset removes the second source of truth a hand-authored palette would be.
 *         The averaging is the bug-prone part (alpha weighting), so it is pinned by an exact oracle
 *         here, mirroring LodColor / AtlasSlice.
 *
 * HOW   : feed known RGBA8 layers and assert the exact expected colour. Colour literals follow the
 *         engine convention: RGBA8 bytes [R,G,B,A] little-endian == 0xAABBGGRR (alpha = high byte).
 *         Values are chosen to divide evenly so the asserts stay exact, free of rounding ambiguity.
 */

#include <catch2/catch_test_macros.hpp>

#include "Resources/AtlasAverage.h"

#include <cstdint>
#include <vector>

using namespace grove::atlas;

TEST_CASE("Atlas average: a solid layer averages to its own colour, per layer", "[atlas][tilemap][unit]") {
    // Two 2x2 solid layers, contiguous (the sliceToArray layout). Each must reduce to itself, and
    // layer 1 must not be polluted by layer 0 (the per-layer stride is the easy thing to get wrong).
    const uint32_t A = 0xFF3050C8u, B = 0xFF804020u;
    std::vector<uint32_t> arr = { A, A, A, A,   B, B, B, B };

    std::vector<uint32_t> avg = averageLayers(arr.data(), 2, 2, 2);

    REQUIRE(avg.size() == 2u);
    REQUIRE(avg[0] == A);
    REQUIRE(avg[1] == B);
}

TEST_CASE("Atlas average: an opaque checkerboard averages component-wise", "[atlas][tilemap][unit]") {
    // Fully opaque -> the alpha weighting reduces to a plain mean. 0x20/0x60 -> 0x40, 0x40/0x80 ->
    // 0x60, 0x60/0x40 -> 0x50 (all exact halves, no rounding).
    const uint32_t c1 = 0xFF204060u, c2 = 0xFF608040u;
    std::vector<uint32_t> arr = { c1, c2, c2, c1 };

    std::vector<uint32_t> avg = averageLayers(arr.data(), 2, 2, 1);

    REQUIRE(avg.size() == 1u);
    REQUIRE(avg[0] == 0xFF406050u);
}

TEST_CASE("Atlas average: RGB is alpha-weighted, so transparent texels do not tint", "[atlas][tilemap][unit]") {
    // Half opaque RED, half FULLY TRANSPARENT green. The transparent texel carries a colour that a
    // straight mean would smear in (muddy half-red half-green); alpha weighting must ignore it
    // entirely and keep the hue pure red, only dropping the coverage to ~half.
    // WHY this matters: a straight mean darkens the RGB, then compositing darkens it AGAIN.
    const uint32_t opaqueRed        = 0xFF0000FFu;   // A=FF, B=00, G=00, R=FF
    const uint32_t transparentGreen = 0x0000FF00u;   // A=00, B=00, G=FF, R=00
    std::vector<uint32_t> arr = { opaqueRed, transparentGreen };

    std::vector<uint32_t> avg = averageLayers(arr.data(), 2, 1, 1);

    REQUIRE(avg.size() == 1u);
    REQUIRE(avg[0] == 0x800000FFu);   // hue untouched (pure red), alpha = mean(255, 0) = 128
}

TEST_CASE("Atlas average: a fully transparent layer averages to transparent", "[atlas][tilemap][unit]") {
    // Total weight 0 -> no hue can be recovered. Must be transparent black, NOT a divide-by-zero and
    // NOT the RGB of invisible texels (tile id 0 / empty is transparent everywhere else too).
    std::vector<uint32_t> arr = { 0x0000FF00u, 0x00FF0000u, 0x000000FFu, 0x00FFFFFFu };

    std::vector<uint32_t> avg = averageLayers(arr.data(), 2, 2, 1);

    REQUIRE(avg.size() == 1u);
    REQUIRE(avg[0] == 0x00000000u);
}

TEST_CASE("Atlas average: no layers yields no colours", "[atlas][tilemap][unit]") {
    // Guards the failed-load path: sliceToArray reports 0 layers when the tile size exceeds the
    // image, and the caller must get an empty table (which then resolves to the built-in palette).
    std::vector<uint32_t> avg = averageLayers(nullptr, 2, 2, 0);
    REQUIRE(avg.empty());
}
