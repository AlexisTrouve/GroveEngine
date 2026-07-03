/**
 * Unit Tests: grove::mapview::WorldCheck — the world-document validator (slice S3 support tool).
 *
 * WHAT  : Locks the semantic validator. The PURE half (checkManifest/checkChunk) is exercised in RAM:
 *         a clean world produces no findings, and each mistake the reader accepts silently (bad topology,
 *         non-positive cell/chunk size, inverted bounds, empty/duplicate field names, bits=0, scale=0,
 *         coord/cellCount mismatch, out-of-bounds chunk, empty chunk, NaN floats) produces exactly the
 *         expected Error/Warning. The DISK half (checkWorldDir) is exercised through real temp dirs:
 *         a good doc (raw AND zlib) is clean, a cellCount-corrupt doc errors, a stray file warns, and a
 *         missing manifest is a fatal error.
 *
 * WHY    : This is the fail-franc philosophy turned into a headless, deterministic tool so a producer
 *          (Theomen) can prove a `.world` is correct without launching the GPU viewer and eyeballing it.
 *          The tests are the proof the tool actually BITES on each documented mistake, not just compiles.
 *
 * HOW    : Catch2 + nlohmann (Manifest) + miniz.c (the compressed disk case) + std::filesystem, mirroring
 *          the S0c disk test. Each disk case uses a fresh temp subdir, cleaned first for a deterministic run.
 */

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "grove/mapview/Compression.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/Manifest.h"
#include "grove/mapview/WorldCheck.h"
#include "grove/mapview/WorldCheckDisk.h"
#include "grove/mapview/WorldDocument.h"
#include "grove/mapview/WorldDocumentDisk.h"

using namespace grove::mapview;

// --- Fixtures ---------------------------------------------------------------------------------------

// A fresh, empty temp directory for one case (removed first so the run is deterministic + self-contained).
static std::string freshDir(const std::string& tag) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("grove_worldcheck_" + tag);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

// A minimal VALID manifest: an 8×8 square world tiled into 4×4 chunks (16 cells each), one int16 field.
static Manifest goodManifest() {
    Manifest m;
    m.coordinate.topology  = "square";
    m.coordinate.cellSize  = {{1.0, 1.0}};
    m.coordinate.boundsMin = {{0, 0, 0}};
    m.coordinate.boundsMax = {{7, 7, 0}};   // inclusive → 8×8 cells → chunk indices 0..1 in x,y
    m.coordinate.chunkDims = {{4, 4, 1}};   // 16 cells / chunk
    m.fields = {FieldDecl{"elevation", Encoding::Int, 16, 0.25, 0.0}};
    return m;
}

// One full, valid 16-cell chunk at (cx,cy,0) carrying the elevation field.
static ChunkData goodChunk(std::int32_t cx, std::int32_t cy) {
    ChunkData d;
    d.coord = ChunkCoord{cx, cy, 0};
    d.cellCount = 16;
    d.fields.emplace_back("elevation", std::vector<std::uint32_t>(16, 100));
    return d;
}

// Write the four chunks that tile the good world to `dir` (raw if comp==nullptr, else zlib).
static void writeGoodDoc(const std::string& dir, const Compressor* comp) {
    const Manifest m = goodManifest();
    std::vector<ChunkData> chunks;
    for (std::int32_t cy = 0; cy < 2; ++cy)
        for (std::int32_t cx = 0; cx < 2; ++cx) chunks.push_back(goodChunk(cx, cy));
    disk::writeWorldDocument(dir, m, chunks, comp);
}

// ============================ PURE — checkManifest ===================================================

TEST_CASE("worldcheck - a clean manifest yields no findings", "[mapview][worldcheck][unit]") {
    Report r;
    checkManifest(goodManifest(), r);
    REQUIRE(r.errors() == 0);
    REQUIRE(r.warnings() == 0);
    REQUIRE(r.clean());
}

TEST_CASE("worldcheck - manifest errors fire per bad declaration", "[mapview][worldcheck][unit]") {
    SECTION("unsupported topology") {
        Manifest m = goodManifest();
        m.coordinate.topology = "hex";
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 1);
    }
    SECTION("non-positive cell size") {
        Manifest m = goodManifest();
        m.coordinate.cellSize = {{0.0, 1.0}};
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 1);
    }
    SECTION("inverted bounds") {
        Manifest m = goodManifest();
        m.coordinate.boundsMin = {{5, 0, 0}};
        m.coordinate.boundsMax = {{0, 7, 0}};   // min.x > max.x
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 1);
    }
    SECTION("non-positive chunk dims") {
        Manifest m = goodManifest();
        m.coordinate.chunkDims = {{0, 4, 1}};
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 1);
    }
    SECTION("empty field name") {
        Manifest m = goodManifest();
        m.fields = {FieldDecl{"", Encoding::Int, 16}};
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 1);
    }
    SECTION("duplicate field names") {
        Manifest m = goodManifest();
        m.fields = {FieldDecl{"h", Encoding::Int, 16}, FieldDecl{"h", Encoding::Int, 16}};
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 1);
    }
    SECTION("variable-width integer with bits=0 (forgot to set bits)") {
        Manifest m = goodManifest();
        m.fields = {FieldDecl{"u", Encoding::Uint, 0}};
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 1);
    }
    SECTION("variable-width integer with bits>32") {
        Manifest m = goodManifest();
        m.fields = {FieldDecl{"u", Encoding::Uint, 40}};
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 1);
    }
}

TEST_CASE("worldcheck - manifest warnings for tolerated-but-suspicious", "[mapview][worldcheck][unit]") {
    SECTION("no fields") {
        Manifest m = goodManifest();
        m.fields.clear();
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 0);
        REQUIRE(r.warnings() == 1);
    }
    SECTION("stray bits on a fixed-width encoding") {
        Manifest m = goodManifest();
        m.fields = {FieldDecl{"b", Encoding::Bit, 5}};   // Bit derives width; bits=5 is ignored
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 0);
        REQUIRE(r.warnings() == 1);
    }
    SECTION("scale=0 collapses the field") {
        Manifest m = goodManifest();
        m.fields = {FieldDecl{"z", Encoding::Uint, 8, 0.0, 3.0}};
        Report r; checkManifest(m, r);
        REQUIRE(r.errors() == 0);
        REQUIRE(r.warnings() == 1);
    }
}

// ============================ PURE — checkChunk ======================================================

TEST_CASE("worldcheck - a clean chunk yields no findings", "[mapview][worldcheck][unit]") {
    const Manifest m = goodManifest();
    Report r;
    checkChunk(m, ChunkCoord{0, 0, 0}, goodChunk(0, 0), r);
    REQUIRE(r.clean());
}

TEST_CASE("worldcheck - chunk errors fire per corruption", "[mapview][worldcheck][unit]") {
    const Manifest m = goodManifest();

    SECTION("blob coord disagrees with the storage coord") {
        ChunkData d = goodChunk(1, 0);              // blob says (1,0,0)
        Report r;
        checkChunk(m, ChunkCoord{0, 0, 0}, d, r);   // but stored as (0,0,0)
        REQUIRE(r.errors() == 1);
    }
    SECTION("cellCount disagrees with chunkDims") {
        ChunkData d;
        d.coord = ChunkCoord{0, 0, 0};
        d.cellCount = 9;                            // manifest implies 16
        d.fields.emplace_back("elevation", std::vector<std::uint32_t>(9, 1));
        Report r;
        checkChunk(m, ChunkCoord{0, 0, 0}, d, r);
        REQUIRE(r.errors() == 1);
    }
}

TEST_CASE("worldcheck - chunk warnings for suspicious-but-loadable", "[mapview][worldcheck][unit]") {
    const Manifest m = goodManifest();

    SECTION("chunk outside the bounds-implied grid") {
        Report r;
        checkChunk(m, ChunkCoord{5, 5, 0}, goodChunk(5, 5), r);  // grid is only 0..1
        REQUIRE(r.errors() == 0);
        REQUIRE(r.warnings() == 1);
    }
    SECTION("chunk with no present fields") {
        ChunkData d;
        d.coord = ChunkCoord{0, 0, 0};
        d.cellCount = 16;                           // right count, but no fields
        Report r;
        checkChunk(m, ChunkCoord{0, 0, 0}, d, r);
        REQUIRE(r.errors() == 0);
        REQUIRE(r.warnings() == 1);
    }
    SECTION("Float32 field carrying NaN") {
        Manifest fm = goodManifest();
        fm.fields = {FieldDecl{"temp", Encoding::Float32, 0}};
        ChunkData d;
        d.coord = ChunkCoord{0, 0, 0};
        d.cellCount = 16;
        std::vector<std::uint32_t> vals(16, 0x3F800000u);  // 1.0f
        vals[3] = 0x7FC00000u;                             // a quiet-NaN bit pattern
        d.fields.emplace_back("temp", std::move(vals));
        Report r;
        checkChunk(fm, ChunkCoord{0, 0, 0}, d, r);
        REQUIRE(r.errors() == 0);
        REQUIRE(r.warnings() == 1);
    }
}

// ============================ DISK — checkWorldDir ===================================================

TEST_CASE("worldcheck - a good world on disk is clean (raw)", "[mapview][worldcheck][disk]") {
    const std::string dir = freshDir("good_raw");
    writeGoodDoc(dir, nullptr);
    const Report r = disk::checkWorldDir(dir);
    REQUIRE(r.errors() == 0);
    REQUIRE(r.warnings() == 0);
    REQUIRE(r.chunkCount == 4);
    REQUIRE(r.fieldCount == 1);
    REQUIRE(r.totalCells == 64);
}

TEST_CASE("worldcheck - a good world on disk is clean (zlib)", "[mapview][worldcheck][disk]") {
    const std::string dir = freshDir("good_zlib");
    const Compressor z = codec::zlibCompressor();
    writeGoodDoc(dir, &z);
    const Report r = disk::checkWorldDir(dir);
    REQUIRE(r.errors() == 0);
    REQUIRE(r.warnings() == 0);
    REQUIRE(r.chunkCount == 4);
}

TEST_CASE("worldcheck - a cellCount-corrupt chunk is caught on disk", "[mapview][worldcheck][disk]") {
    const std::string dir = freshDir("bad_cellcount");
    Manifest m = goodManifest();                       // chunkDims imply 16 cells
    ChunkData d;
    d.coord = ChunkCoord{0, 0, 0};
    d.cellCount = 9;                                   // writer allows it (field.size()==cellCount)
    d.fields.emplace_back("elevation", std::vector<std::uint32_t>(9, 7));
    disk::writeWorldDocument(dir, m, {d}, nullptr);

    const Report r = disk::checkWorldDir(dir);
    REQUIRE(r.errors() >= 1);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("worldcheck - a stray file in the chunks dir warns", "[mapview][worldcheck][disk]") {
    const std::string dir = freshDir("stray");
    writeGoodDoc(dir, nullptr);
    { std::ofstream(std::filesystem::path(dir) / "chunks" / "notes.txt") << "not a chunk"; }

    const Report r = disk::checkWorldDir(dir);
    REQUIRE(r.errors() == 0);
    REQUIRE(r.warnings() >= 1);
    REQUIRE(r.chunkCount == 4);   // the stray file is not counted as a chunk
}

TEST_CASE("worldcheck - a missing manifest is a fatal error", "[mapview][worldcheck][disk]") {
    const std::string dir = freshDir("no_manifest");   // empty dir, no manifest.json
    const Report r = disk::checkWorldDir(dir);
    REQUIRE(r.errors() >= 1);
    REQUIRE_FALSE(r.ok());
}
