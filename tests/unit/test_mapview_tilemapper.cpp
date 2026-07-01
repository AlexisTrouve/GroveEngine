/**
 * Unit Tests: grove::mapview tiling path (T3) — TileMapper + MapView tile-chunk emit.
 *
 * WHAT  : Locks the two halves of the productized tiling path. (1) TileMapper — the pure value->tile-id band
 *         mapper (the recipe counterpart of Palette, mirroring its banded semantics). (2) MapView's tile
 *         emit — driving a MapView whose lens declares a TileLayer produces one TileChunkDraw per visible
 *         chunk, with the right dims / world origin / z-order and the exact tile ids the mapper dictates,
 *         including correct z-slice extraction from a multi-layer (D>1) chunk.
 *
 * WHY   : tiling used to be demo-local (a hand-written elevToTile inside capture_mapview_tiles). Making it a
 *         core output means the mapping is data-driven and testable HEADLESS — no GPU, no PNG eyeballing.
 *         The subtle link is the z-slice extraction (chunk blob is row-major then z-major); it gets its own
 *         lock. fail-franc: a tile layer over a field not in the schema draws nothing (never zero-filled).
 *
 * HOW   : Catch2. A tiny in-RAM IChunkProvider serves a field "h" whose stored raw value is a caller-supplied
 *         function of the global cell coords, so every emitted tile id is predictable and asserted exactly.
 */

#include <cmath>
#include <functional>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Lens.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Projection.h"
#include "grove/mapview/TileMapper.h"
#include "grove/mapview/WorldDocument.h"

using namespace grove::mapview;

// ------------------------------------------------------------------------------------------------
// A deterministic in-RAM world: field "h" at global cell (gx,gy,gz) holds raw = rawFn(gx,gy,gz). Chunks are
// W×H×D, blob order row-major then z-major (the format's order), so tests can predict every cell's value.
// ------------------------------------------------------------------------------------------------
struct GridWorld final : IChunkProvider {
    int W{4}, H{4}, D{1};
    std::function<uint32_t(int, int, int)> rawFn;

    bool has(ChunkCoord) const override { return true; }  // infinite; the cull bounds what is generated
    ChunkData load(ChunkCoord c) override {
        ChunkData d;
        d.coord = c;
        d.cellCount = static_cast<uint32_t>(W * H * D);
        std::vector<uint32_t> v(static_cast<size_t>(W) * H * D);
        for (int lz = 0; lz < D; ++lz)
            for (int ly = 0; ly < H; ++ly)
                for (int lx = 0; lx < W; ++lx) {
                    const int gx = c.x * W + lx, gy = c.y * H + ly, gz = c.z * D + lz;
                    v[static_cast<size_t>(lz) * W * H + static_cast<size_t>(ly) * W + lx] = rawFn(gx, gy, gz);
                }
        d.fields.emplace_back("h", std::move(v));
        return d;
    }
};

// Schema: field "h", 16-bit int, scale 1 -> decoded physical value == the stored raw (easy to reason about).
static std::vector<FieldDecl> schemaH() {
    return { FieldDecl{"h", Encoding::Int, 16, 1.0, 0.0} };
}

TEST_CASE("mapview T3 - TileMapper banded mirrors Palette::banded semantics", "[mapview][tile][unit]") {
    const TileMapper m = TileMapper::banded({{10.0, 1}, {20.0, 2}, {30.0, 3}}, /*fallback*/ 7);
    REQUIRE(m.map(5.0) == 1);       // < 10
    REQUIRE(m.map(9.999) == 1);
    REQUIRE(m.map(10.0) == 2);      // NOT < 10 -> next band (< 20)
    REQUIRE(m.map(15.0) == 2);
    REQUIRE(m.map(20.0) == 3);      // NOT < 20 -> next band (< 30)
    REQUIRE(m.map(29.999) == 3);
    REQUIRE(m.map(30.0) == 3);      // above the last upper bound -> last id
    REQUIRE(m.map(1000.0) == 3);
    // nodata + empty -> fallback (0 default = transparent tile).
    REQUIRE(m.map(std::numeric_limits<double>::quiet_NaN()) == 7);
    REQUIRE(TileMapper::banded({}, 5).map(3.0) == 5);
    REQUIRE(TileMapper::banded({}).map(3.0) == 0);
}

TEST_CASE("mapview T3 - MapView emits a tile chunk with correct dims, origin, z-order and ids", "[mapview][tile][unit]") {
    GridWorld world;                                  // 4×4×1 chunks
    world.rawFn = [](int gx, int, int) { return static_cast<uint32_t>(gx); };  // value == global x

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(schemaH(), GridSpec{4, 4, 1, 1.0, 1.0}, layout, proj, world, /*budget*/ 16);

    // One tile layer: x < 2 -> tile 1, 2 <= x < 4 -> tile 2, 4 <= x < 6 -> tile 3.
    Lens lens;
    lens.name = "tiles";
    lens.tileLayers.push_back(TileLayer{"h", TileMapper::banded({{2.0, 1}, {4.0, 2}, {6.0, 3}}, 9), /*layerZ*/ 5});
    mv.setLens(lens);

    SECTION("a single visible chunk -> one tile chunk, tiles follow the mapper") {
        mv.setViewport(Viewport{0.0, 0.0, 3.5, 3.5});  // strictly inside chunk (0,0)'s [0,4) box
        mv.update();

        REQUIRE(mv.tileChunkCount() == 1);
        const TileChunkDraw& tc = mv.tileChunks()[0];
        REQUIRE(tc.chunkX == 0);
        REQUIRE(tc.chunkY == 0);
        REQUIRE(tc.width == 4);
        REQUIRE(tc.height == 4);
        REQUIRE(tc.worldX == 0.0);
        REQUIRE(tc.worldY == 0.0);
        REQUIRE(tc.tileW == 1.0);
        REQUIRE(tc.tileH == 1.0);
        REQUIRE(tc.layer == 5);
        REQUIRE(tc.tiles.size() == 16);
        // Row-major: each of the 4 rows is [x=0,1,2,3] -> [1,1,2,2].
        REQUIRE(tc.tiles[0] == 1);   // gx 0
        REQUIRE(tc.tiles[1] == 1);   // gx 1
        REQUIRE(tc.tiles[2] == 2);   // gx 2
        REQUIRE(tc.tiles[3] == 2);   // gx 3
        REQUIRE(tc.tiles[4] == 1);   // row 1, gx 0
        REQUIRE(tc.tiles[15] == 2);  // row 3, gx 3
    }

    SECTION("two visible chunks -> two tile chunks with the right world origins and ids") {
        mv.setViewport(Viewport{0.0, 0.0, 7.5, 3.5});  // spans chunk (0,0) and (1,0)
        mv.update();

        REQUIRE(mv.tileChunkCount() == 2);
        // Locate chunk (1,0): world corner at x=4, cells gx 4..7 -> all tile 3.
        const TileChunkDraw* right = nullptr;
        for (const TileChunkDraw& tc : mv.tileChunks())
            if (tc.chunkX == 1) right = &tc;
        REQUIRE(right != nullptr);
        REQUIRE(right->worldX == 4.0);
        REQUIRE(right->tiles.size() == 16);
        for (uint16_t id : right->tiles) REQUIRE(id == 3);
    }
}

TEST_CASE("mapview T3 - a tile layer over an unknown field draws nothing (fail-franc)", "[mapview][tile][unit]") {
    GridWorld world;
    world.rawFn = [](int, int, int) { return 0u; };
    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(schemaH(), GridSpec{4, 4, 1, 1.0, 1.0}, layout, proj, world, 16);

    Lens lens;
    lens.tileLayers.push_back(TileLayer{"nope", TileMapper::banded({{1.0, 1}}), 0});  // "nope" not in schema
    mv.setLens(lens);
    mv.setViewport(Viewport{0.0, 0.0, 3.5, 3.5});
    mv.update();

    REQUIRE(mv.tileChunkCount() == 0);  // absent -> no chunk emitted (never a zero-filled grid)
}

TEST_CASE("mapview T3 - the active z-slice selects the right layer of a D>1 chunk", "[mapview][tile][unit]") {
    GridWorld world;
    world.W = 2; world.H = 2; world.D = 2;
    world.rawFn = [](int, int, int gz) { return gz == 0 ? 10u : 20u; };  // z0 -> 10, z1 -> 20

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(schemaH(), GridSpec{2, 2, 2, 1.0, 1.0}, layout, proj, world, 16);

    Lens lens;
    lens.tileLayers.push_back(TileLayer{"h", TileMapper::banded({{15.0, 1}, {25.0, 2}}, 0), 0});
    mv.setLens(lens);
    mv.setViewport(Viewport{0.0, 0.0, 1.5, 1.5});  // chunk (0,0)

    SECTION("z-slice 0 reads the value-10 layer -> tile 1") {
        mv.setZSlice(0);
        mv.update();
        REQUIRE(mv.tileChunkCount() == 1);
        const TileChunkDraw& tc = mv.tileChunks()[0];
        REQUIRE(tc.tiles.size() == 4);
        for (uint16_t id : tc.tiles) REQUIRE(id == 1);
    }

    SECTION("z-slice 1 reads the value-20 layer -> tile 2 (proves the z-major slice offset)") {
        mv.setZSlice(1);
        mv.update();
        REQUIRE(mv.tileChunkCount() == 1);
        const TileChunkDraw& tc = mv.tileChunks()[0];
        REQUIRE(tc.tiles.size() == 4);
        for (uint16_t id : tc.tiles) REQUIRE(id == 2);
    }
}
