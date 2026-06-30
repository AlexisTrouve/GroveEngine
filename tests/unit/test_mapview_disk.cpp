/**
 * Unit Tests: grove::mapview disk + manifest (map-viewer engine, slice S0c).
 *
 * WHAT  : Locks the on-disk half of the world-document — the JSON manifest round-trips through
 *         parse/emit, and a full document (manifest + sparse chunk blobs, raw and compressed) round-trips
 *         through a real temp directory. Reading a chunk back yields the same values, and absent fields
 *         stay absent; a missing chunk and a malformed manifest fail loudly.
 *
 * WHY    : S0c completes S0 ("format + reader"): the contract is now writable to and readable from disk,
 *         which is what unblocks the Theomen adapter (S3, writes it) and the viewer app (S2, reads it).
 *
 * HOW    : Catch2 + nlohmann (manifest) + miniz.c (the compressed case) + std::filesystem. Each case uses
 *         a fresh temp subdir, cleaned first so the run is deterministic and self-contained.
 */

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "grove/mapview/Compression.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/Manifest.h"
#include "grove/mapview/WorldDocument.h"
#include "grove/mapview/WorldDocumentDisk.h"

using namespace grove::mapview;

// A fresh, empty temp directory for one test case (removed first so the run is deterministic).
static std::string freshDir(const std::string& tag) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("grove_mapview_" + tag);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

// A two-field schema shared by the cases (an int16 elevation + a float32 temperature).
static Manifest demoManifest() {
    Manifest m;
    m.coordinate.topology = "square";
    m.coordinate.cellSize = {{1.0, 1.0}};
    m.coordinate.boundsMin = {{0, 0, 0}};
    m.coordinate.boundsMax = {{255, 255, 0}};
    m.coordinate.chunkDims = {{4, 4, 1}};
    m.fields = {
        FieldDecl{"elevation", Encoding::Int, 16, 0.5, -5000.0},
        FieldDecl{"temperature", Encoding::Float32, 0},
    };
    m.chunksDir = "chunks";
    return m;
}

// Compare two schemas by their MEANING (storage width, not the raw `bits` field which a fixed-width
// encoding omits in JSON).
static void requireSameSchema(const std::vector<FieldDecl>& a, const std::vector<FieldDecl>& b) {
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].name == b[i].name);
        REQUIRE(a[i].encoding == b[i].encoding);
        REQUIRE(a[i].storageBits() == b[i].storageBits());
        REQUIRE(a[i].scale == b[i].scale);
        REQUIRE(a[i].offset == b[i].offset);
    }
}

TEST_CASE("mapview S0c - manifest round-trips through JSON", "[mapview][disk][unit]") {
    const Manifest m = demoManifest();
    const Manifest back = parseManifest(emitManifest(m));

    REQUIRE(back.formatVersion == 1);
    REQUIRE(back.coordinate.topology == "square");
    REQUIRE(back.coordinate.cellSize == std::array<double, 2>{{1.0, 1.0}});
    REQUIRE(back.coordinate.boundsMax == std::array<int32_t, 3>{{255, 255, 0}});
    REQUIRE(back.coordinate.chunkDims == std::array<int32_t, 3>{{4, 4, 1}});
    REQUIRE(back.chunksDir == "chunks");
    requireSameSchema(back.fields, m.fields);
}

TEST_CASE("mapview S0c - world-document round-trips through a temp directory", "[mapview][disk][unit]") {
    const std::string dir = freshDir("s0c_raw");
    const Manifest m = demoManifest();

    // Two sparse 4x4 chunks: each has elevation, neither has temperature.
    std::vector<ChunkData> chunks;
    for (int cx = 0; cx < 2; ++cx) {
        ChunkData c;
        c.coord = {cx, 0, 0};
        c.cellCount = 16;
        std::vector<uint32_t> elev(16);
        for (uint32_t i = 0; i < 16; ++i) elev[i] = static_cast<uint32_t>((cx * 1000 + i * 37) & 0xFFFF);
        c.fields.emplace_back("elevation", elev);
        chunks.push_back(std::move(c));
    }

    disk::writeWorldDocument(dir, m, chunks);

    const Manifest rm = disk::readManifest(dir);
    requireSameSchema(rm.fields, m.fields);
    REQUIRE(rm.coordinate.chunkDims == std::array<int32_t, 3>{{4, 4, 1}});

    for (int cx = 0; cx < 2; ++cx) {
        REQUIRE(disk::hasChunk(dir, rm, ChunkCoord{cx, 0, 0}));
        const ChunkData c = disk::readChunk(dir, rm, ChunkCoord{cx, 0, 0});
        REQUIRE(c.cellCount == 16);
        REQUIRE(c.has("elevation"));
        REQUIRE_FALSE(c.has("temperature"));   // absent stays absent across disk
        std::vector<uint32_t> expect(16);
        for (uint32_t i = 0; i < 16; ++i) expect[i] = static_cast<uint32_t>((cx * 1000 + i * 37) & 0xFFFF);
        REQUIRE(*c.get("elevation") == expect);
    }
}

TEST_CASE("mapview S0c - compressed world-document round-trips on disk", "[mapview][disk][unit]") {
    const std::string dir = freshDir("s0c_zlib");
    const Manifest m = demoManifest();
    const Compressor z = codec::zlibCompressor();

    ChunkData c;
    c.coord = {-3, 7, 0};                       // negative coord keeps its sign in the filename
    c.cellCount = 16;
    std::vector<uint32_t> elev(16);
    for (uint32_t i = 0; i < 16; ++i) elev[i] = static_cast<uint32_t>((i * 911) & 0xFFFF);
    c.fields.emplace_back("elevation", elev);

    disk::writeWorldDocument(dir, m, {c}, &z);

    const Manifest rm = disk::readManifest(dir);
    const ChunkData back = disk::readChunk(dir, rm, ChunkCoord{-3, 7, 0}, &z);
    REQUIRE(back.coord == ChunkCoord{-3, 7, 0});
    REQUIRE(*back.get("elevation") == elev);
    REQUIRE_FALSE(back.has("temperature"));
}

TEST_CASE("mapview S0c - missing chunk and malformed manifest fail loudly", "[mapview][disk][unit]") {
    const std::string dir = freshDir("s0c_neg");
    const Manifest m = demoManifest();
    disk::writeWorldDocument(dir, m, {});       // manifest only, no chunks

    // A chunk that was never written: hasChunk false, readChunk throws (not a silent empty).
    REQUIRE_FALSE(disk::hasChunk(dir, m, ChunkCoord{5, 5, 0}));
    REQUIRE_THROWS_AS(disk::readChunk(dir, m, ChunkCoord{5, 5, 0}), std::runtime_error);

    // A manifest naming an unknown encoding must throw (fail-franc, no silent default).
    const std::string badManifest = R"({
        "formatVersion": 1,
        "coordinate": {"topology":"square","cellSize":[1,1],
                       "bounds":{"min":[0,0,0],"max":[1,1,0]},"chunkDims":[4,4,1]},
        "fields": [{"name":"x","encoding":"float64"}],
        "chunks": "chunks"
    })";
    REQUIRE_THROWS(parseManifest(badManifest));
}
