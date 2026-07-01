/**
 * Unit Tests: grove::mapview MapView orchestrator (map-viewer engine, slice S1d, the S1 capstone).
 *
 * WHAT  : Drives the whole per-frame pipeline end to end, headless: a manifest-less schema + GridSpec + an
 *         injected provider + SquareLayout + TopDownProjection + a Lens, through MapView.update() ->
 *         drainCells(). Asserts the emitted CellDraws: count, projected positions, palette colours, per-layer
 *         z-order, filtering, absent-field skipping (fail-franc), chunk culling, and z-slice selection.
 *
 * WHY    : This is where S1a (geometry) + S1b (streaming) + S1c (recipe) combine — the integration that the
 *         viewer app (S2) will sit directly on top of. Locking it headless means the app only has to wire
 *         the camera + submitSpriteBatch, not re-verify the pipeline.
 *
 * HOW    : Catch2; a SimpleProvider holds prepared ChunkData; colours/positions checked with WithinAbs.
 */

#include <cmath>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/mapview/ChunkCache.h"      // ChunkCoordHash
#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Projection.h"

using namespace grove::mapview;
using Catch::Matchers::WithinAbs;

// A provider backed by an in-memory map of prepared chunks.
struct SimpleProvider final : IChunkProvider {
    std::unordered_map<ChunkCoord, ChunkData, ChunkCoordHash> chunks;
    bool has(ChunkCoord c) const override { return chunks.find(c) != chunks.end(); }
    ChunkData load(ChunkCoord c) override { return chunks.at(c); }
};

// Build a chunk with the given fields (name -> raw values), row-major then z-major.
static ChunkData makeChunk(ChunkCoord coord, uint32_t cellCount,
                           std::vector<std::pair<std::string, std::vector<uint32_t>>> fields) {
    ChunkData c;
    c.coord = coord;
    c.cellCount = cellCount;
    c.fields = std::move(fields);
    return c;
}

// Find the emitted cell whose centre is (x,y) on a given layer; nullptr if none.
static const CellDraw* findCell(const std::vector<CellDraw>& cells, double x, double y, int32_t layer) {
    for (const auto& d : cells) {
        if (std::abs(d.x - x) < 1e-9 && std::abs(d.y - y) < 1e-9 && d.layer == layer) return &d;
    }
    return nullptr;
}

static const std::vector<FieldDecl> kSchema = {
    FieldDecl{"elevation", Encoding::Int, 16, 1.0, 0.0},  // raw == physical for positive values
    FieldDecl{"biome", Encoding::Uint, 5},
};

TEST_CASE("mapview S1d - single elevation layer emits one coloured cell per cell", "[mapview][core][unit]") {
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] =
        makeChunk({0, 0, 0}, 4, {{"elevation", {10, 20, 5, 100}}, {"biome", {0, 1, 2, 0}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 16);
    mv.setLens(Lens{"elev", {Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}),
                                   Filter::always(), 100, 1.0f}}});
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});
    mv.update();

    REQUIRE(mv.cellCount() == 4);
    // Cells are at the cell centres; colour is grey = elevation/100.
    const CellDraw* c00 = findCell(mv.cells(), 0.5, 0.5, 100);  // idx0, value 10
    const CellDraw* c11 = findCell(mv.cells(), 1.5, 1.5, 100);  // idx3, value 100
    REQUIRE(c00 != nullptr);
    REQUIRE(c11 != nullptr);
    REQUIRE_THAT(c00->color.r, WithinAbs(0.10f, 1e-5));
    REQUIRE_THAT(c11->color.r, WithinAbs(1.00f, 1e-5));
    REQUIRE_THAT(c00->w, WithinAbs(1.0, 1e-9));
}

TEST_CASE("mapview S1d - a layer filter drops the cells that fail it", "[mapview][core][unit]") {
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk({0, 0, 0}, 4, {{"elevation", {10, 20, 5, 100}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 16);
    // Only elevation >= 20 (values 20 and 100).
    mv.setLens(Lens{"hi", {Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}),
                                 Filter::cmp(Filter::Op::Ge, 20.0), 0, 1.0f}}});
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});
    mv.update();

    REQUIRE(mv.cellCount() == 2);
}

TEST_CASE("mapview S1d - a layer over a field absent in the chunk emits nothing", "[mapview][core][unit]") {
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk({0, 0, 0}, 4, {{"elevation", {1, 2, 3, 4}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 16);
    // "biome" is in the schema but NOT present in this chunk -> fail-franc, no draw.
    mv.setLens(Lens{"biome", {Layer{"biome", Palette::categorical({Rgba{1, 0, 0, 1}}, Rgba{}), Filter::always(), 0, 1.0f}}});
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});
    mv.update();

    REQUIRE(mv.cellCount() == 0);
}

TEST_CASE("mapview S1d - two layers each emit per cell with their own z-order", "[mapview][core][unit]") {
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] =
        makeChunk({0, 0, 0}, 4, {{"elevation", {10, 20, 5, 100}}, {"biome", {0, 1, 2, 0}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 16);
    mv.setLens(Lens{"both", {
        Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}), Filter::always(), 100, 1.0f},
        Layer{"biome", Palette::categorical({Rgba{1, 0, 0, 1}, Rgba{0, 1, 0, 1}, Rgba{0, 0, 1, 1}}, Rgba{}), Filter::always(), 200, 1.0f},
    }});
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});
    mv.update();

    REQUIRE(mv.cellCount() == 8);
    int onElev = 0, onBiome = 0;
    for (const auto& d : mv.cells()) { if (d.layer == 100) ++onElev; else if (d.layer == 200) ++onBiome; }
    REQUIRE(onElev == 4);
    REQUIRE(onBiome == 4);
}

TEST_CASE("mapview S1d - cull restricts emission to visible chunks", "[mapview][core][unit]") {
    SimpleProvider provider;
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk({0, 0, 0}, 4, {{"elevation", {1, 2, 3, 4}}});
    provider.chunks[ChunkCoord{5, 0, 0}] = makeChunk({5, 0, 0}, 4, {{"elevation", {9, 9, 9, 9}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 16);
    mv.setLens(Lens{"e", {Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {10.0, Rgba{1, 1, 1, 1}}}), Filter::always(), 0, 1.0f}}});

    // Viewport over chunk (0,0) only (chunk (5,0) is far away).
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});
    mv.update();
    REQUIRE(mv.cellCount() == 4);
    REQUIRE(mv.residentChunks() == 1);  // only chunk (0,0) was streamed
}

TEST_CASE("mapview S1d - z-slice selects only cells on the active layer of a deep chunk", "[mapview][core][unit]") {
    const std::vector<FieldDecl> schema = {FieldDecl{"elevation", Encoding::Int, 16, 1.0, 0.0}};
    SimpleProvider provider;
    // chunkDims 2x2x2 -> 8 cells; idx 0..3 are z-slice 0, idx 4..7 are z-slice 1.
    provider.chunks[ChunkCoord{0, 0, 0}] =
        makeChunk({0, 0, 0}, 8, {{"elevation", {0, 10, 20, 30, 40, 50, 60, 70}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(schema, GridSpec{2, 2, 2, 1.0, 1.0}, layout, proj, provider, 16);
    mv.setLens(Lens{"e", {Layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}), Filter::always(), 0, 1.0f}}});
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});

    mv.setZSlice(0);
    mv.update();
    REQUIRE(mv.cellCount() == 4);   // only the bottom slice

    mv.setZSlice(1);
    mv.update();
    REQUIRE(mv.cellCount() == 4);   // only the top slice
    // Top slice holds the higher elevations (40..70) -> at least one bright cell.
    bool sawBright = false;
    for (const auto& d : mv.cells()) if (d.color.r >= 0.39f) sawBright = true;
    REQUIRE(sawBright);
}

TEST_CASE("mapview S1h - a cross-field filter drops cells via another field", "[mapview][core][unit]") {
    SimpleProvider provider;
    // elevation {10,-5,20,30}, biome {2,2,5,2}. Keep cells where elevation>0 AND biome==2 -> idx0 + idx3.
    provider.chunks[ChunkCoord{0, 0, 0}] =
        makeChunk({0, 0, 0}, 4, {{"elevation", {10, /*-5 as raw is irrelevant; use 0 to fail >0*/ 0, 20, 30}},
                                 {"biome", {2, 2, 5, 2}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(kSchema, GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 16);
    Layer layer{"elevation", Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}),
                Filter::all({Filter::cmp(Filter::Op::Gt, 0.0),
                             Filter::cmpField("biome", Filter::Op::Eq, 2.0)}),
                0, 1.0f};
    mv.setLens(Lens{"land", {layer}});
    mv.setViewport(Viewport{0.0, 0.0, 2.0, 2.0});
    mv.update();

    // idx0 (elev10,biome2) pass; idx1 (elev0) fails >0; idx2 (biome5) fails ==2; idx3 (elev30,biome2) pass.
    REQUIRE(mv.cellCount() == 2);
}

TEST_CASE("mapview S1h - hillshade darkens a sloped cell vs the unshaded colour", "[mapview][core][unit]") {
    SimpleProvider provider;
    // 3x3 chunk, elevation = column index (0,1,2 per row) -> a constant slope dz/dx = 1 in the interior.
    provider.chunks[ChunkCoord{0, 0, 0}] =
        makeChunk({0, 0, 0}, 9, {{"elevation", {0, 1, 2, 0, 1, 2, 0, 1, 2}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    const std::vector<FieldDecl> schema = {FieldDecl{"elevation", Encoding::Int, 16, 1.0, 0.0}};

    // Constant-white palette so any colour change is purely the hillshade.
    const Palette white = Palette::ramp({{0.0, Rgba{1, 1, 1, 1}}, {1000.0, Rgba{1, 1, 1, 1}}});

    // (A) no hillshade -> the centre cell stays white.
    MapView a(schema, GridSpec{3, 3, 1, 1.0, 1.0}, layout, proj, provider, 16);
    a.setLens(Lens{"flat", {Layer{"elevation", white, Filter::always(), 0, 1.0f}}});
    a.setViewport(Viewport{0.0, 0.0, 3.0, 3.0});
    a.update();
    const CellDraw* aCentre = findCell(a.cells(), 1.5, 1.5, 0);
    REQUIRE(aCentre != nullptr);
    REQUIRE_THAT(aCentre->color.r, WithinAbs(1.0f, 1e-5));

    // (B) overhead hillshade on elevation -> the centre cell (dz/dx=1) dims to 1/sqrt(2).
    MapView b(schema, GridSpec{3, 3, 1, 1.0, 1.0}, layout, proj, provider, 16);
    Layer shaded{"elevation", white, Filter::always(), 0, 1.0f};
    shaded.hillshadeField = "elevation";
    shaded.hillshade = Hillshade(0.0, 0.0, 1.0);  // straight overhead
    b.setLens(Lens{"relief", {shaded}});
    b.setViewport(Viewport{0.0, 0.0, 3.0, 3.0});
    b.update();
    const CellDraw* bCentre = findCell(b.cells(), 1.5, 1.5, 0);
    REQUIRE(bCentre != nullptr);
    REQUIRE(bCentre->color.r < aCentre->color.r);                       // shaded is darker
    // Model-agnostic: the centre cell has dz/dx=1, so its shade must equal factor(1,0) for THIS light —
    // whatever the shading curve. (Locks the gradient, not the curve; the curve is locked in the hillshade test.)
    REQUIRE_THAT(bCentre->color.r, WithinAbs(static_cast<float>(Hillshade(0.0, 0.0, 1.0).factor(1.0, 0.0)), 1e-4));
}

TEST_CASE("mapview S1h - hillshade gradient samples ACROSS chunk boundaries", "[mapview][core][unit]") {
    const std::vector<FieldDecl> schema = {FieldDecl{"elevation", Encoding::Int, 16, 1.0, 0.0}};
    SimpleProvider provider;
    // Two adjacent 2x2 chunks; elevation = global x, continuous across the seam (chunk0: x 0,1; chunk1: x 2,3).
    provider.chunks[ChunkCoord{0, 0, 0}] = makeChunk({0, 0, 0}, 4, {{"elevation", {0, 1, 0, 1}}});
    provider.chunks[ChunkCoord{1, 0, 0}] = makeChunk({1, 0, 0}, 4, {{"elevation", {2, 3, 2, 3}}});

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    const Palette white = Palette::ramp({{0.0, Rgba{1, 1, 1, 1}}, {1000.0, Rgba{1, 1, 1, 1}}});

    MapView mv(schema, GridSpec{2, 2, 1, 1.0, 1.0}, layout, proj, provider, 16);
    Layer shaded{"elevation", white, Filter::always(), 0, 1.0f};
    shaded.hillshadeField = "elevation";
    shaded.hillshade = Hillshade(0.0, 0.0, 1.0);  // overhead
    mv.setLens(Lens{"relief", {shaded}});
    mv.setViewport(Viewport{0.0, 0.0, 4.0, 1.0});  // both chunks visible & resident
    mv.update();
    REQUIRE(mv.residentChunks() == 2);

    // The right-edge cell of chunk 0 is global (1,0) at centre (1.5,0.5). Its +x neighbour is global (2,0),
    // which lives in chunk 1. Cross-chunk sampling => dz/dx = (2-0)/2 = 1 (=> factor(1,0)). WITHOUT it the
    // missing neighbour would fall back to the centre (=1) => dz/dx = 0.5 (=> factor(0.5,0), brighter). So
    // matching factor(1,0) — not factor(0.5,0) — proves the cross-chunk sample (model-agnostic).
    const CellDraw* edge = findCell(mv.cells(), 1.5, 0.5, 0);
    REQUIRE(edge != nullptr);
    REQUIRE_THAT(edge->color.r, WithinAbs(static_cast<float>(Hillshade(0.0, 0.0, 1.0).factor(1.0, 0.0)), 1e-4));
}
