/**
 * Unit Tests: tilemap LOD color mip generation (Slice B) — the eye-free oracle.
 *
 * WHAT  : The zoom-out band samples a mipped color texture whose mips are 2x2 box-filters of the
 *         per-tile colors. The defining property: the COARSEST mip of a 2-color checkerboard must
 *         equal the average of those two colors. That is an exact, analytical oracle — no golden
 *         image, no eyeballing.
 *
 * WHY    : judging "does the LOD look smooth?" on screen is subjective and tiring. The box-filter
 *         is pure math; pin it down objectively here so the GPU side is just trilinear (trusted HW)
 *         + a smoothstep crossfade. This locks the part that was actually custom and bug-prone.
 *
 * HOW    : call buildLodMipChain (pure, GPU-free) and assert on the returned mip chain — the last
 *         texel is the 1x1 mip (the global average).
 */

#include <catch2/catch_test_macros.hpp>

#include "Passes/LodColor.h"

#include <vector>

using namespace grove::lod;

// Extract one RGBA8 component (shift 0/8/16/24 = R/G/B/A).
static int byteOf(uint32_t c, int shift) { return static_cast<int>((c >> shift) & 0xFFu); }

TEST_CASE("LOD box-filter: coarsest mip of a 2-color checkerboard = their average", "[lod][tilemap][unit]") {
    // 4x4 checkerboard of tile id 1 (grey) and id 3 (blue).
    const int W = 4, H = 4;
    std::vector<uint16_t> tiles(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            tiles[static_cast<size_t>(y) * W + x] = ((x + y) & 1) ? 1 : 3;

    int mips = 0;
    std::vector<uint32_t> chain = buildLodMipChain(W, H, tiles.data(), mips);

    REQUIRE(mips == 3);                       // 4 -> 2 -> 1
    const uint32_t top = chain.back();        // the 1x1 mip = global average

    const uint32_t a = paletteColor(1);
    const uint32_t b = paletteColor(3);
    for (int shift = 0; shift < 32; shift += 8) {
        const int expected = (byteOf(a, shift) + byteOf(b, shift)) / 2;
        REQUIRE(byteOf(top, shift) >= expected - 1);   // ±1 for box-filter cascade rounding
        REQUIRE(byteOf(top, shift) <= expected + 1);
    }
}

TEST_CASE("LOD box-filter: a uniform grid keeps its exact color at every mip", "[lod][tilemap][unit]") {
    const int W = 8, H = 8;
    std::vector<uint16_t> tiles(static_cast<size_t>(W) * H, static_cast<uint16_t>(2));  // all green
    int mips = 0;
    std::vector<uint32_t> chain = buildLodMipChain(W, H, tiles.data(), mips);

    REQUIRE(mips == 4);                       // 8 -> 4 -> 2 -> 1
    const uint32_t green = paletteColor(2);
    for (uint32_t texel : chain) {
        REQUIRE(texel == green);              // averaging equal values must not drift
    }
}

TEST_CASE("LOD box-filter: empty tiles fade in alpha (transparent averages in)", "[lod][tilemap][unit]") {
    // Checkerboard of empty (id 0, alpha 0) and opaque (id 1, alpha 255) -> coarsest alpha ~ 127.
    const int W = 4, H = 4;
    std::vector<uint16_t> tiles(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            tiles[static_cast<size_t>(y) * W + x] = ((x + y) & 1) ? 0 : 1;

    int mips = 0;
    std::vector<uint32_t> chain = buildLodMipChain(W, H, tiles.data(), mips);

    const int topAlpha = byteOf(chain.back(), 24);
    REQUIRE(topAlpha >= 120);
    REQUIRE(topAlpha <= 136);                 // ~half of 255
}
