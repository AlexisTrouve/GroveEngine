/**
 * Unit Tests: grove::mapview WorldDocumentProvider — the disk↔core bridge (the "file is the interface" seam).
 *
 * WHAT  : Locks the missing link between the on-disk world-document (WorldDocumentDisk.h) and the pure
 *         orchestrator (MapView, which only knows IChunkProvider). Writes a real document to a temp dir, then
 *         drives a MapView entirely from WorldDocumentProvider — proving pixels-worth of CellDraws can be
 *         produced from a FILE, not just an in-RAM generator. Covers: chunk values round-trip THROUGH the
 *         provider seam, schema/GridSpec derive from the manifest, MapView compiles the resident chunks into
 *         the expected cell count, an absent chunk yields NOTHING (fail-franc, not a zeroed placeholder), and
 *         the compressed path requires its compressor (loud failure, never a silent skip).
 *
 * WHY    : The whole mp4/VLC thesis rests on this bridge. Before it, the viewer bypassed its own format with
 *         an in-memory provider, so disk→provider→MapView was never exercised end to end — exactly the kind
 *         of never-wired chain the doctrine warns about. This is the E2E-in-a-unit-test that closes it.
 *
 * HOW    : Catch2 + miniz.c (compressed case) + std::filesystem, mirroring test_mapview_disk. Each case uses a
 *         fresh temp subdir. The world is a small finite 4×4-chunk document with a single Int16 elevation field
 *         so decoded values are exact integers; a strictly-interior viewport makes the resident-chunk set (and
 *         thus the cell count) deterministic, independent of cull boundary rounding.
 */

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "grove/mapview/Compression.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/Filter.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Lens.h"
#include "grove/mapview/Manifest.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Palette.h"
#include "grove/mapview/Projection.h"
#include "grove/mapview/WorldDocument.h"
#include "grove/mapview/WorldDocumentDisk.h"
#include "grove/mapview/WorldDocumentProvider.h"

using namespace grove::mapview;

// A fresh, empty temp directory for one case (removed first so the run is deterministic + self-contained).
static std::string freshDir(const std::string& tag) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("grove_mvprovider_" + tag);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

// A finite document: 4×4-cell chunks, one Int16 elevation field (scale 0.25). Deterministic raw values.
static Manifest demoManifest() {
    Manifest m;
    m.coordinate.topology = "square";
    m.coordinate.cellSize = {{1.0, 1.0}};
    m.coordinate.boundsMin = {{0, 0, 0}};
    m.coordinate.boundsMax = {{7, 3, 0}};                 // 2×1 chunks of 4×4 cells
    m.coordinate.chunkDims = {{4, 4, 1}};
    m.fields = { FieldDecl{"elevation", Encoding::Int, 16, 0.25, 0.0} };
    m.chunksDir = "chunks";
    return m;
}

// Raw elevation value for chunk (cx,cy) cell index i — arbitrary but reproducible (used to write AND expect).
static uint32_t rawElev(int cx, int cy, uint32_t i) {
    return static_cast<uint32_t>((cx * 1000 + cy * 100 + i * 41) & 0xFFFF);
}

// Build the two on-disk chunks (0,0) and (1,0), each 4×4 with elevation present.
static std::vector<ChunkData> demoChunks() {
    std::vector<ChunkData> chunks;
    for (int cx = 0; cx < 2; ++cx) {
        ChunkData c;
        c.coord = {cx, 0, 0};
        c.cellCount = 16;
        std::vector<uint32_t> elev(16);
        for (uint32_t i = 0; i < 16; ++i) elev[i] = rawElev(cx, 0, i);
        c.fields.emplace_back("elevation", std::move(elev));
        chunks.push_back(std::move(c));
    }
    return chunks;
}

// A one-layer terrain lens (continuous ramp over the field) — enough to compile cells from the provider.
static Lens terrainLens() {
    const std::vector<std::pair<double, Rgba>> stops = {
        {0.0,     Rgba{0.0f, 0.0f, 0.0f, 1.0f}},
        {16384.0, Rgba{1.0f, 1.0f, 1.0f, 1.0f}},          // raw ~0xFFFF * 0.25 ≈ 16384 m at the top
    };
    Layer layer{"elevation", Palette::ramp(stops), Filter::always(), 0, 1.0f};
    return Lens{"terrain", {layer}, {}, {}};
}

TEST_CASE("mapview provider - chunk values round-trip through the provider seam", "[mapview][provider][unit]") {
    const std::string dir = freshDir("roundtrip");
    const Manifest m = demoManifest();
    disk::writeWorldDocument(dir, m, demoChunks());

    // The provider opens the directory and reads the manifest on its own (no host-side JSON).
    WorldDocumentProvider provider(dir);

    // Schema + geometry derive straight from the manifest.
    REQUIRE(provider.schema().size() == 1);
    REQUIRE(provider.schema()[0].name == "elevation");
    const GridSpec g = provider.gridSpec();
    REQUIRE(g.chunkW == 4);
    REQUIRE(g.chunkH == 4);
    REQUIRE(g.cellW == 1.0);

    // has()/load() answer straight off disk; decoded raw values match what was written.
    for (int cx = 0; cx < 2; ++cx) {
        REQUIRE(provider.has(ChunkCoord{cx, 0, 0}));
        const ChunkData c = provider.load(ChunkCoord{cx, 0, 0});
        REQUIRE(c.cellCount == 16);
        REQUIRE(c.has("elevation"));
        std::vector<uint32_t> expect(16);
        for (uint32_t i = 0; i < 16; ++i) expect[i] = rawElev(cx, 0, i);
        REQUIRE(*c.get("elevation") == expect);
    }

    // The chainon: a MapView driven ONLY by the file-backed provider compiles the resident chunks into cells.
    SquareLayout layout(g.cellW, g.cellH);
    TopDownProjection proj;
    MapView mv(provider.schema(), g, layout, proj, provider, /*chunkBudget*/ 8);
    mv.setLens(terrainLens());

    // Viewport strictly INSIDE chunks (0,0)+(1,0) → both resident → 2 chunks × 16 cells = 32 CellDraws.
    mv.setViewport(Viewport{0.5, 0.5, 7.5, 3.5});
    mv.update();
    REQUIRE(mv.residentChunks() == 2);
    REQUIRE(mv.cellCount() == 32);
}

TEST_CASE("mapview provider - an absent chunk draws nothing (fail-franc, not zeroed)", "[mapview][provider][unit]") {
    const std::string dir = freshDir("absent");
    const Manifest m = demoManifest();
    disk::writeWorldDocument(dir, m, demoChunks());       // only chunks (0,0) and (1,0) exist on disk

    WorldDocumentProvider provider(dir);
    REQUIRE_FALSE(provider.has(ChunkCoord{10, 10, 0}));   // never written

    SquareLayout layout(1.0, 1.0);
    TopDownProjection proj;
    MapView mv(provider.schema(), provider.gridSpec(), layout, proj, provider, 8);
    mv.setLens(terrainLens());

    // A viewport over the empty region (world ~40..44) — no blob there → no chunk resident → zero cells.
    // (Absent must read as ABSENT, never a zeroed placeholder that would paint phantom sea-level terrain.)
    mv.setViewport(Viewport{40.5, 40.5, 43.5, 43.5});
    mv.update();
    REQUIRE(mv.residentChunks() == 0);
    REQUIRE(mv.cellCount() == 0);
}

TEST_CASE("mapview provider - compressed document requires its compressor", "[mapview][provider][unit]") {
    const std::string dir = freshDir("zlib");
    const Manifest m = demoManifest();
    const Compressor z = codec::zlibCompressor();
    disk::writeWorldDocument(dir, m, demoChunks(), &z);   // chunk bodies deflated

    // With the matching compressor: loads and round-trips exactly.
    {
        WorldDocumentProvider provider(dir, z);
        const ChunkData c = provider.load(ChunkCoord{0, 0, 0});
        std::vector<uint32_t> expect(16);
        for (uint32_t i = 0; i < 16; ++i) expect[i] = rawElev(0, 0, i);
        REQUIRE(*c.get("elevation") == expect);
    }
    // Without a compressor: a compressed chunk fails LOUDLY on load, never a silent empty/skip.
    {
        WorldDocumentProvider provider(dir);              // default {} = no decompressor
        REQUIRE(provider.has(ChunkCoord{0, 0, 0}));       // presence is just a file check → still true
        REQUIRE_THROWS(provider.load(ChunkCoord{0, 0, 0}));
    }
}
