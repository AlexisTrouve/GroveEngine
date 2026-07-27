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
#include "Resources/MipChain.h"
#include "Passes/SectorMesh.h"

#include <vector>
#include <cmath>

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

// ============================================================================
// Game/tileset-supplied LOD colour table. The built-in palette wraps modulo 8, so a game with more
// than 8 tile types (or with real art) sees unrelated colours zoomed out. A table fixes that. These
// are the success criteria the consuming project (DAOS) asked for, turned into oracles.
// ============================================================================

TEST_CASE("LOD table: mip0 is the table's colour, texel by texel", "[lod][tilemap][unit][palette]") {
    // 25 distinct ids — the case the built-in 8-colour palette cannot express (it would wrap 3x).
    const int N = 25;
    std::vector<uint32_t> table(N);
    for (int i = 0; i < N; ++i)
        table[i] = 0xFF000000u | static_cast<uint32_t>(i * 10 + 1);   // distinct R per entry

    const int W = 5, H = 5;
    std::vector<uint16_t> tiles(static_cast<size_t>(W) * H);
    for (int i = 0; i < W * H; ++i) tiles[i] = static_cast<uint16_t>(i + 1);   // ids 1..25

    int mips = 0;
    std::vector<uint32_t> chain = buildLodMipChain(W, H, tiles.data(), mips, table.data(), N);

    for (int i = 0; i < W * H; ++i) {
        REQUIRE(chain[i] == table[i]);          // mip0[t] == table[id-1], exactly
    }
}

TEST_CASE("LOD table: the coarsest mip is the average of the table colours", "[lod][tilemap][unit][palette]") {
    // The box-filter cascade must stay exact on top of a supplied table (same property as the
    // built-in palette): a 2-colour checkerboard averages to the midpoint at 1x1.
    const uint32_t C1 = 0xFF204060u, C2 = 0xFF608040u;
    std::vector<uint32_t> table = { C1, C2 };

    const int W = 4, H = 4;
    std::vector<uint16_t> tiles(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            tiles[static_cast<size_t>(y) * W + x] = ((x + y) & 1) ? 1 : 2;

    int mips = 0;
    std::vector<uint32_t> chain = buildLodMipChain(W, H, tiles.data(), mips, table.data(), 2);

    const uint32_t top = chain.back();
    for (int shift = 0; shift < 32; shift += 8) {
        const int expected = (byteOf(C1, shift) + byteOf(C2, shift)) / 2;
        REQUIRE(byteOf(top, shift) >= expected - 1);
        REQUIRE(byteOf(top, shift) <= expected + 1);
    }
}

TEST_CASE("LOD table: absent table leaves the chain byte-identical (non-regression)", "[lod][tilemap][unit][palette]") {
    // THE guarantee for every project already on the engine: publish nothing, change nothing.
    const int W = 4, H = 4;
    std::vector<uint16_t> tiles(static_cast<size_t>(W) * H);
    for (int i = 0; i < W * H; ++i) tiles[i] = static_cast<uint16_t>((i % 8) + 1);

    int mipsA = 0, mipsB = 0;
    std::vector<uint32_t> withTable = buildLodMipChain(W, H, tiles.data(), mipsA,
                                                       nullptr, 0);          // explicitly no table
    std::vector<uint32_t> historic  = buildLodMipChain(W, H, tiles.data(), mipsB);  // legacy call

    REQUIRE(mipsA == mipsB);
    REQUIRE(withTable == historic);
    for (int i = 0; i < W * H; ++i) {
        REQUIRE(historic[i] == paletteColor(tiles[i]));   // still the built-in 8 colours
    }
}

TEST_CASE("LOD table: an id beyond the table is transparent, never wrapped", "[lod][tilemap][unit][palette]") {
    // A wrap would invent a plausible WRONG colour; that tile has no atlas layer either, so a hole is
    // the honest signal. Locks the deliberate choice against a future "helpful" modulo.
    std::vector<uint32_t> table = { 0xFF0000FFu, 0xFF00FF00u };   // ids 1 and 2 only

    REQUIRE(lodColor(1, table.data(), 2) == 0xFF0000FFu);
    REQUIRE(lodColor(2, table.data(), 2) == 0xFF00FF00u);
    REQUIRE(lodColor(3, table.data(), 2) == 0x00000000u);   // out of range -> transparent
    REQUIRE(lodColor(0, table.data(), 2) == 0x00000000u);   // empty tile -> transparent
    REQUIRE(lodColor(3, nullptr, 0) == paletteColor(3));    // no table -> built-in palette
}

TEST_CASE("LOD table: an RGBA8 blob decodes to the engine's colour layout", "[lod][tilemap][unit][palette]") {
    // Wire format = raw R,G,B,A bytes (what a pixel buffer already holds), so a game needs no
    // swizzling. Decoding to 0xAABBGGRR is the bug-prone half of the topic handler.
    const uint8_t blob[] = { 255, 128, 0, 255,    0, 128, 255, 64 };
    std::vector<uint32_t> table = paletteFromBytes(blob, sizeof(blob));

    REQUIRE(table.size() == 2u);
    REQUIRE(table[0] == 0xFF0080FFu);   // R=255 G=128 B=0   A=255
    REQUIRE(table[1] == 0x40FF8000u);   // R=0   G=128 B=255 A=64

    // A trailing partial colour is dropped; a null/empty blob yields an empty table (-> built-in).
    REQUIRE(paletteFromBytes(blob, 6).size() == 1u);
    REQUIRE(paletteFromBytes(nullptr, 8).empty());
}

TEST_CASE("R8 fog mip: coarsest mip of a 0/255 visibility checkerboard = ~127", "[lod][tilemap][unit]") {
    // Scalar visibility -> box-filtering is meaningful (soft partial-reveal edge at zoom-out).
    const int W = 4, H = 4;
    std::vector<uint8_t> vis(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            vis[static_cast<size_t>(y) * W + x] = ((x + y) & 1) ? 255 : 0;

    int mips = 0;
    std::vector<uint8_t> chain = buildR8MipChain(vis.data(), W, H, mips);

    REQUIRE(mips == 3);
    const int top = chain.back();
    REQUIRE(top >= 120);
    REQUIRE(top <= 135);   // ~127
}

TEST_CASE("R8 fog mip: a uniform field keeps its exact value at every mip", "[lod][tilemap][unit]") {
    const int W = 8, H = 8;
    std::vector<uint8_t> vis(static_cast<size_t>(W) * H, static_cast<uint8_t>(200));
    int mips = 0;
    std::vector<uint8_t> chain = buildR8MipChain(vis.data(), W, H, mips);
    for (uint8_t v : chain) REQUIRE(v == 200);
}

// ============================================================================
// Sprite mip box-filter (grove::tex::buildRgba8MipChain) — the same eye-free oracle as the LOD color,
// but for ARBITRARY pixels (decoded sprite images): the coarsest mip of a 2-color checkerboard MUST be
// the average of those colors. This is the anti-aliasing fix for free unit sprites at strong zoom-out.
// ============================================================================

TEST_CASE("Sprite mip: coarsest mip of a 2-color checkerboard = their average", "[mip][unit]") {
    using namespace grove::tex;
    const int W = 4, H = 4;
    const uint32_t a = 0x10305070u, b = 0x90B0D0F0u;   // two distinct colors (all channels differ)
    std::vector<uint32_t> px(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            px[static_cast<size_t>(y) * W + x] = ((x + y) & 1) ? a : b;

    int mips = 0;
    std::vector<uint32_t> chain = buildRgba8MipChain(px.data(), W, H, mips);

    REQUIRE(mips == 3);                       // 4 -> 2 -> 1
    const uint32_t top = chain.back();        // 1x1 mip = global average
    for (int shift = 0; shift < 32; shift += 8) {
        const int expected = (byteOf(a, shift) + byteOf(b, shift)) / 2;
        REQUIRE(byteOf(top, shift) >= expected - 1);   // ±1 box-filter cascade rounding
        REQUIRE(byteOf(top, shift) <= expected + 1);
    }
}

TEST_CASE("Sprite mip: a uniform image keeps its exact color at every mip", "[mip][unit]") {
    using namespace grove::tex;
    const int W = 8, H = 8;
    const uint32_t c = 0xFF8040C0u;
    std::vector<uint32_t> px(static_cast<size_t>(W) * H, c);
    int mips = 0;
    std::vector<uint32_t> chain = buildRgba8MipChain(px.data(), W, H, mips);
    REQUIRE(mips == 4);                        // 8 -> 4 -> 2 -> 1
    for (uint32_t texel : chain) REQUIRE(texel == c);   // averaging equal values must not drift
}

TEST_CASE("Sprite mip: NPOT image mips down by floor halving, chain length sums the levels", "[mip][unit]") {
    using namespace grove::tex;
    const int W = 6, H = 4;                    // NPOT (the real .jpg sprite is 1280x853)
    std::vector<uint32_t> px(static_cast<size_t>(W) * H, 0xFF112233u);
    int mips = 0;
    std::vector<uint32_t> chain = buildRgba8MipChain(px.data(), W, H, mips);
    REQUIRE(mips == 3);                        // max(6,4)=6 -> 3 -> 1
    // levels: 6x4(24) + 3x2(6) + 1x1(1) = 31 texels (floor halving, odd dims clamp)
    REQUIRE(chain.size() == 24u + 6u + 1u);
}

// ============================================================================
// Ring-sector tessellation (grove::geom::appendSector) — the pie-wedge primitive's geometry, locked
// with an analytical oracle: every vertex sits on circle r0 or r1, and the wedge spans exactly [a0,a1].
// ============================================================================

TEST_CASE("Sector mesh: a quarter ring tessellates to a triangle list on the two circles", "[sector][unit]") {
    using namespace grove::geom;
    const float cx = 10.0f, cy = 20.0f, r0 = 4.0f, r1 = 8.0f;
    const float a0 = 0.0f, a1 = 1.5707963f;   // 0 .. pi/2
    const int steps = 6;

    std::vector<float> v;
    appendSector(v, cx, cy, r0, r1, a0, a1, steps);

    // Triangle LIST: 6 vertices per step, 2 floats per vertex.
    REQUIRE(v.size() == static_cast<size_t>(steps) * 6u * 2u);

    // Every vertex lies on the inner OR outer circle (distance to centre == r0 or r1).
    for (size_t i = 0; i + 1 < v.size(); i += 2) {
        const float dx = v[i] - cx, dy = v[i + 1] - cy;
        const float d = std::sqrt(dx * dx + dy * dy);
        const bool onR0 = std::fabs(d - r0) < 1e-3f;
        const bool onR1 = std::fabs(d - r1) < 1e-3f;
        REQUIRE((onR0 || onR1));
    }

    // The first quad starts exactly at a0 (+x axis here): inner (cx+r0, cy), outer (cx+r1, cy).
    REQUIRE(std::fabs(v[0] - (cx + r0)) < 1e-3f);   // first vertex = inner @ a0
    REQUIRE(std::fabs(v[1] - cy) < 1e-3f);
    REQUIRE(std::fabs(v[2] - (cx + r1)) < 1e-3f);   // second vertex = outer @ a0
    REQUIRE(std::fabs(v[3] - cy) < 1e-3f);
}

TEST_CASE("Sector mesh: a pie slice (r0=0) keeps the centre point, steps>=2", "[sector][unit]") {
    using namespace grove::geom;
    REQUIRE(sectorSteps(0.05f) == 2);              // thin wedge still gets 2 quads
    REQUIRE(sectorSteps(3.14159f) >= 20);          // a half-turn is well tessellated

    std::vector<float> v;
    appendSector(v, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 1.0f, 3);   // r0=0 -> centre at origin
    REQUIRE(v.size() == 3u * 6u * 2u);
    // With r0=0, the inner vertices collapse onto the centre (0,0).
    REQUIRE(std::fabs(v[0]) < 1e-3f);   // inner @ a0 = centre
    REQUIRE(std::fabs(v[1]) < 1e-3f);
}

TEST_CASE("Sector mesh: the pie cuts into N EQUAL wedges for any N (2,3,5,6,7,8)", "[sector][unit]") {
    using namespace grove::geom;
    const float kTwoPi = 6.2831853f;
    const float r0 = 30.0f, r1 = 100.0f;
    for (int N : {2, 3, 5, 6, 7, 8}) {
        const float span = kTwoPi / static_cast<float>(N);     // each wedge = a 1/N slice
        const int steps = sectorSteps(span);
        std::vector<float> v;
        appendSector(v, 0.0f, 0.0f, r0, r1, 0.0f, span, steps);
        REQUIRE(v.size() == static_cast<size_t>(steps) * 6u * 2u);
        for (size_t i = 0; i + 1 < v.size(); i += 2) {          // every vertex on the two circles
            const float d = std::sqrt(v[i] * v[i] + v[i + 1] * v[i + 1]);
            REQUIRE((std::fabs(d - r0) < 1e-3f || std::fabs(d - r1) < 1e-3f));
        }
        REQUIRE(std::fabs(static_cast<float>(N) * span - kTwoPi) < 1e-3f);   // N wedges tile the circle
    }
}
