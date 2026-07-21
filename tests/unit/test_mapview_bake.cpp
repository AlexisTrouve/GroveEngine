/**
 * Unit Tests: grove::mapview MapView BAKED-MAP path (the "gen once" static-map layer).
 *
 * WHAT  : Locks the three contracts of MapView::bakeLensRGBA() + setCompileCells(), all headless (no GPU):
 *          1. PARITY — a baked RGBA8 texel is byte-identical (±1/255 rounding) to the composite of the per-layer
 *             CellDraws the sprite path emits for the same cell. This is THE guarantee that swapping the per-frame
 *             sprite grid for one baked quad does not change what the user sees.
 *          2. O(1)-IN-CELLS — with compileCells(false), update() emits ZERO cells and streams ZERO chunks, for a
 *             SMALL world and a LARGER one alike. That structural invariant (not a wall-clock number) is the real
 *             proof the per-frame cost stopped scaling with the world size.
 *          3. FALLBACK — a world with no bounded extent (GridSpec.worldW/worldH == 0) can't be baked -> the bake
 *             returns false so the host keeps the sprite path (never silently produces a wrong/empty texture).
 *
 * WHY   : the baked path replaces the per-cell compile for a static --load map; without a parity lock a
 *         compositing/rounding regression would silently diverge the baked map from the sprite map (exactly the
 *         "looks fine in code" the doctrine forbids — this test IS the proof, run every ctest).
 *
 * HOW   : Catch2; the same SimpleProvider/makeChunk scaffold as test_mapview_core.cpp. The expected texel is
 *         recomputed here with the identical premultiplied-alpha "over" the bake uses, from the CellDraws update()
 *         produced — so the two independent code paths must agree.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "grove/mapview/ChunkCache.h"      // ChunkCoordHash
#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Projection.h"

using namespace grove::mapview;

// A provider backed by an in-memory map of prepared chunks (identical to the core test's).
namespace {
struct SimpleProvider final : IChunkProvider {
    std::unordered_map<ChunkCoord, ChunkData, ChunkCoordHash> chunks;
    bool has(ChunkCoord c) const override { return chunks.find(c) != chunks.end(); }
    ChunkData load(ChunkCoord c) override { return chunks.at(c); }
};

ChunkData makeChunk(ChunkCoord coord, uint32_t cellCount,
                    std::vector<std::pair<std::string, std::vector<uint32_t>>> fields) {
    ChunkData c;
    c.coord = coord;
    c.cellCount = cellCount;
    c.fields = std::move(fields);
    return c;
}

const std::vector<FieldDecl> kSchema = {
    FieldDecl{"elevation", Encoding::Int, 16, 1.0, 0.0},  // raw == physical for positive values
    FieldDecl{"biome", Encoding::Uint, 5},
};

// Round-to-nearest 8-bit quantise + clamp — MUST match MapView::bakeLensRGBA's to8 (and packRGBA8).
uint8_t to8(float v) {
    const int i = static_cast<int>(v * 255.0f + 0.5f);
    return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
}

// A multi-layer lens: an elevation ramp base (layerZ 0, with hillshade) + a categorical biome overlay (layerZ 10,
//   index 0 transparent so some cells show only the base -> exercises the transparency-through path too).
Lens twoLayerLens() {
    Layer base{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}),
               Filter::always(), /*layerZ*/ 0, 1.0f};
    base.hillshadeField = "elevation";
    base.hillshade = Hillshade::fromAzimuthAltitude(2.36, 0.95, 0.30);   // a real relief light (like the viewer)

    // table[0] = transparent (ocean/unclassified), 1..3 = solid colours.
    Layer biome{"biome", Palette::categorical({Rgba{0, 0, 0, 0}, Rgba{1, 0, 0, 1}, Rgba{0, 1, 0, 1}, Rgba{0, 0, 1, 1}}, Rgba{}),
                Filter::always(), /*layerZ*/ 10, 1.0f};
    return Lens{"terrain+biome", {base, biome}};
}

// Composite the CellDraws that fall on cell (gx,gy) into an expected straight-alpha RGBA8 texel, using the SAME
//   premultiplied "over" (ascending layerZ) the bake uses. Cell centre for SquareLayout(1,1) is (gx+0.5,gy+0.5).
void expectedTexel(const std::vector<CellDraw>& cells, int gx, int gy, uint8_t out[4]) {
    std::vector<const CellDraw*> here;
    for (const CellDraw& d : cells) {
        if (static_cast<int>(std::floor(d.x)) == gx && static_cast<int>(std::floor(d.y)) == gy) here.push_back(&d);
    }
    std::stable_sort(here.begin(), here.end(), [](const CellDraw* a, const CellDraw* b) { return a->layer < b->layer; });
    float pr = 0, pg = 0, pb = 0, pa = 0;
    for (const CellDraw* d : here) {
        float a = d->color.a < 0 ? 0 : (d->color.a > 1 ? 1 : d->color.a);
        pr = d->color.r * a + pr * (1 - a);
        pg = d->color.g * a + pg * (1 - a);
        pb = d->color.b * a + pb * (1 - a);
        pa = a + pa * (1 - a);
    }
    if (pa > 0.0f) { const float inv = 1.0f / pa; out[0] = to8(pr * inv); out[1] = to8(pg * inv); out[2] = to8(pb * inv); out[3] = to8(pa); }
    else { out[0] = out[1] = out[2] = out[3] = 0; }
}
}  // namespace

TEST_CASE("mapview bake - a baked texel matches the composite of the sprite CellDraws (parity)", "[mapview][bake][unit]") {
    // One 4x4 chunk = the whole world (worldW/H = 4 so the bake has a bounded extent).
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk(
        {0, 0, 0}, 16,
        {{"elevation", {10, 20, 30, 40,  15, 25, 35, 45,  5, 55, 65, 75,  80, 60, 40, 20}},
         {"biome",     {0,  1,  2,  3,   1,  0,  3,  2,   2, 3,  0,  1,   3,  2,  1,  0}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{4, 4, 1, 1.0, 1.0, /*worldW*/ 4, /*worldH*/ 4}, layout, proj, provider, 16);
    mv.setLens(twoLayerLens());

    // Sprite path: emit the per-layer CellDraws for the whole world.
    mv.setViewport(Viewport{0.0, 0.0, 4.0, 4.0});
    mv.update();
    const std::vector<CellDraw> cells = mv.cells();   // copy: the bake below re-runs the pipeline internally
    REQUIRE(cells.size() == 32);                       // 16 cells x 2 layers (biome index 0 still emits an a=0 draw)

    // Baked path: compile the whole world into one RGBA8 buffer.
    std::vector<uint8_t> buf;
    int w = 0, h = 0;
    double mnx = -1, mny = -1, spx = -1, spy = -1;
    REQUIRE(mv.bakeLensRGBA(buf, w, h, mnx, mny, spx, spy));
    REQUIRE(w == 4);
    REQUIRE(h == 4);
    REQUIRE(buf.size() == static_cast<size_t>(w) * h * 4);
    // AABB: top-left corner at world (0,0), span = worldCells * cellSize.
    REQUIRE(mnx == 0.0);
    REQUIRE(mny == 0.0);
    REQUIRE(spx == 4.0);
    REQUIRE(spy == 4.0);

    // Every texel must equal the composite of that cell's sprite CellDraws (±1/255 for float->8-bit rounding).
    int checked = 0;
    for (int gy = 0; gy < h; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            uint8_t exp[4];
            expectedTexel(cells, gx, gy, exp);
            const uint8_t* got = &buf[(static_cast<size_t>(gy) * w + gx) * 4];
            for (int c = 0; c < 4; ++c) {
                INFO("cell (" << gx << "," << gy << ") channel " << c << " got=" << int(got[c]) << " exp=" << int(exp[c]));
                REQUIRE(std::abs(int(got[c]) - int(exp[c])) <= 1);
            }
            ++checked;
        }
    }
    REQUIRE(checked == 16);
}

TEST_CASE("mapview bake - transparent-biome cells fall through to the elevation base (no overlay hole)", "[mapview][bake][unit]") {
    SimpleProvider provider;
    // biome all 0 (transparent) -> the baked texel must be the OPAQUE elevation base, never a transparent hole.
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk(
        {0, 0, 0}, 4, {{"elevation", {50, 50, 50, 50}}, {"biome", {0, 0, 0, 0}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0, 2, 2}, layout, proj, provider, 16);
    mv.setLens(twoLayerLens());

    std::vector<uint8_t> buf;
    int w = 0, h = 0; double a, b, c, d;
    REQUIRE(mv.bakeLensRGBA(buf, w, h, a, b, c, d));
    for (int i = 0; i < w * h; ++i) {
        // elevation 50/100 = grey 0.5 (~128), opaque; hillshade may dim it but alpha stays 255 (base is opaque).
        REQUIRE(buf[i * 4 + 3] == 255);          // opaque -> the base showed through the transparent biome
        REQUIRE(buf[i * 4 + 0] > 40);            // some grey, not a black hole
    }
}

TEST_CASE("mapview bake - compileCells(false) emits zero cells + streams zero chunks, independent of world size", "[mapview][bake][unit]") {
    // The O(1)-in-cells invariant: baked mode does NO per-cell work whether the world is tiny or larger.
    auto emittedWith = [](int worldChunksPerSide, bool compile) -> std::pair<size_t, size_t> {
        SimpleProvider provider;
        const int cps = worldChunksPerSide;
        for (int cy = 0; cy < cps; ++cy)
            for (int cx = 0; cx < cps; ++cx)
                provider.chunks[ChunkCoord{cx, cy, 0}] = makeChunk({cx, cy, 0}, 4, {{"elevation", {1, 2, 3, 4}}});
        SquareLayout layout(1.0, 1.0);
        TopDownProjection proj;
        MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0, 2 * cps, 2 * cps}, layout, proj, provider, 256);
        mv.setLens(Lens{"e", {Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {4.0, Rgba{1, 1, 1, 1}}}),
                                    Filter::always(), 0, 1.0f}}});
        mv.setCompileCells(compile);
        mv.setViewport(Viewport{0.0, 0.0, static_cast<double>(2 * cps), static_cast<double>(2 * cps)});
        mv.update();
        return {mv.cellCount(), mv.residentChunks()};
    };

    // Sprite mode: cell count scales with the world (2x2 world -> 4 cells; 6x6 world -> 36 cells).
    REQUIRE(emittedWith(1, /*compile*/ true).first == 4);
    REQUIRE(emittedWith(3, /*compile*/ true).first == 36);

    // Baked mode: ZERO cells AND zero chunks streamed, for BOTH sizes -> per-frame work independent of world size.
    const auto small = emittedWith(1, /*compile*/ false);
    const auto large = emittedWith(3, /*compile*/ false);
    REQUIRE(small.first == 0);
    REQUIRE(large.first == 0);
    REQUIRE(small.second == 0);   // no chunk streamed (cull/ensureResident skipped)
    REQUIRE(large.second == 0);
}

TEST_CASE("mapview bake - a re-bake reflects the CURRENT lens (lens swap -> new colours)", "[mapview][bake][unit]") {
    // The viewer marks the bake dirty on a lens change; bakeLensRGBA must then produce the NEW lens's pixels.
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk(
        {0, 0, 0}, 4, {{"elevation", {10, 40, 70, 100}}, {"biome", {1, 2, 3, 1}}});
    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0, 2, 2}, layout, proj, provider, 16);

    // Lens A: a plain elevation grey ramp (no overlay).
    mv.setLens(Lens{"grey", {Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}),
                                   Filter::always(), 0, 1.0f}}});
    std::vector<uint8_t> bufA; int w, h; double a, b, c, d;
    REQUIRE(mv.bakeLensRGBA(bufA, w, h, a, b, c, d));

    // Lens B: the multi-layer terrain+biome lens -> the coloured biome overlay must change the texels.
    mv.setLens(twoLayerLens());
    std::vector<uint8_t> bufB;
    REQUIRE(mv.bakeLensRGBA(bufB, w, h, a, b, c, d));

    REQUIRE(bufA.size() == bufB.size());
    REQUIRE(bufA != bufB);   // the swapped lens re-baked to different pixels (not a stale cache)
}

TEST_CASE("mapview bake - an unbounded world (worldW/H == 0) cannot be baked -> false", "[mapview][bake][unit]") {
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk({0, 0, 0}, 4, {{"elevation", {1, 2, 3, 4}}});
    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    // worldW/worldH default 0 (unknown extent) -> no finite grid to bake.
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 16);
    mv.setLens(twoLayerLens());

    std::vector<uint8_t> buf;
    int w = 7, h = 7; double a, b, c, d;
    REQUIRE_FALSE(mv.bakeLensRGBA(buf, w, h, a, b, c, d));   // caller keeps the sprite path
}
