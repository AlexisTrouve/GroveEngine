/**
 * Robustness Tests: grove::mapview save/load round-trip stability (map-viewer engine, S0 hardening).
 *
 * WHAT  : Hunts corruption in the world-document round-trip — especially the kind that does NOT show on
 *         the first save/load but DRIFTS on the 2nd/3rd, and the kind that only appears on awkward configs
 *         (byte-unaligned bit widths, Z>1, non-power-of-two non-square chunks, many fields, sparse chunks).
 *         For each of three distinct "formats" (config profiles) it runs save→load THREE times in a row and
 *         asserts two invariants every iteration: (1) BINARY IDEMPOTENCE — the re-serialized blob is byte-
 *         identical to the previous one (a deterministic save must never drift); (2) VALUE FIDELITY — what
 *         comes back equals the ORIGINAL exactly (raw values + presence + coord), compared against the
 *         original (not the previous round) so any cumulative drift is caught.
 *
 * WHY    : The S0 unit tests each do ONE round-trip on ONE profile — they prove nominal correctness, not
 *         repeated stability nor robustness to twisted configs. This is the regression lock for "works once,
 *         drifts later" and "works at 4×4, breaks at 17 bits". Keep it in the suite; re-run any time to
 *         re-verify the format after a change (MapViewRoundtripUnit).
 *
 * HOW    : Catch2 + nlohmann (manifest) + miniz.c (the compressed path) + std::filesystem. Each profile is
 *         exercised on all three transports — RAM raw, RAM zlib, and disk (manifest + blobs, raw + zlib) —
 *         with deterministically generated per-field values that respect each field's bit width.
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

// ------------------------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------------------------

// A self-contained "format" to stress: a manifest (schema + chunk dims) + the chunks that fill it.
struct Profile {
    std::string            tag;
    Manifest               manifest;
    std::vector<ChunkData> chunks;
};

// Deterministic raw value for (field, cell), masked to the field's storage width so packBits accepts it.
// COMMENT: any uint32 is a valid raw pattern for a 32-bit field (float32/uint32) — we compare raw bits, not
// floats, so even NaN/inf patterns round-trip. Narrower fields are masked to < 2^bits.
static uint32_t genRaw(const FieldDecl& f, uint32_t fieldSalt, uint32_t cell) {
    uint32_t h = fieldSalt * 2654435761u + cell * 40503u + 0x9E3779B9u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    const uint8_t bits = f.storageBits();
    if (bits >= 32) return h;
    return h & ((1u << bits) - 1u);
}

// Build one chunk, filling only the fields flagged present (sparsity). Values vary by coord so distinct
// chunks hold distinct data (a more revealing corruption probe than identical chunks).
static ChunkData buildChunk(ChunkCoord coord, uint32_t cellCount,
                            const std::vector<FieldDecl>& fields, const std::vector<bool>& present) {
    ChunkData c;
    c.coord = coord;
    c.cellCount = cellCount;
    const uint32_t coordSalt = static_cast<uint32_t>(coord.x * 73856093 ^ coord.y * 19349663 ^ coord.z * 83492791);
    for (size_t fi = 0; fi < fields.size(); ++fi) {
        if (!present[fi]) continue;
        std::vector<uint32_t> vals(cellCount);
        for (uint32_t cell = 0; cell < cellCount; ++cell) {
            vals[cell] = genRaw(fields[fi], static_cast<uint32_t>(fi) + 1u + coordSalt, cell);
        }
        c.fields.emplace_back(fields[fi].name, std::move(vals));
    }
    return c;
}

// Set-equivalence of two chunks (coord, cellCount, same present fields with identical raw values).
static void requireSameChunk(const ChunkData& a, const ChunkData& b) {
    REQUIRE(a.coord == b.coord);
    REQUIRE(a.cellCount == b.cellCount);
    REQUIRE(a.fields.size() == b.fields.size());
    for (const auto& kv : a.fields) {
        REQUIRE(b.has(kv.first));
        REQUIRE(*b.get(kv.first) == kv.second);
    }
}

// A fresh, empty temp dir for a disk case (removed first so the run is deterministic).
static std::string freshDir(const std::string& tag) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("grove_mapview_rt_" + tag);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

// RAM transport: serialize→deserialize THREE times. Asserts binary idempotence + fidelity vs the original.
static void cycleRam(const ChunkData& original, const std::vector<FieldDecl>& schema, const Compressor* comp) {
    std::vector<uint8_t> prevBlob = serializeChunk(original, schema, comp);
    for (int iter = 0; iter < 3; ++iter) {
        const ChunkData loaded = deserializeChunk(prevBlob, schema, comp);
        requireSameChunk(original, loaded);                       // value fidelity vs ORIGINAL
        const std::vector<uint8_t> blob = serializeChunk(loaded, schema, comp);
        REQUIRE(blob == prevBlob);                                // binary idempotence (no drift)
        prevBlob = blob;
    }
}

// Disk transport: write the whole document, then read-all→write-all THREE times in place. Asserts the
// manifest text and every chunk file are byte-identical across iterations, and every chunk stays faithful.
static void cycleDisk(const Profile& p, const Compressor* comp) {
    const std::string dir = freshDir(p.tag + (comp ? "_z" : "_raw"));
    disk::writeWorldDocument(dir, p.manifest, p.chunks, comp);

    const std::filesystem::path manifestPath = std::filesystem::path(dir) / "manifest.json";
    const std::string manifest0 = disk::readFileText(manifestPath);
    std::vector<std::vector<uint8_t>> blobs0;
    for (const auto& ch : p.chunks) {
        blobs0.push_back(disk::readFileBytes(std::filesystem::path(dir) / p.manifest.chunksDir / disk::chunkFileName(ch.coord)));
    }

    for (int iter = 0; iter < 3; ++iter) {
        const Manifest rm = disk::readManifest(dir);
        std::vector<ChunkData> loaded;
        for (const auto& ch : p.chunks) {
            loaded.push_back(disk::readChunk(dir, rm, ch.coord, comp));
        }
        for (size_t i = 0; i < p.chunks.size(); ++i) {
            requireSameChunk(p.chunks[i], loaded[i]);             // value fidelity vs ORIGINAL
        }
        disk::writeWorldDocument(dir, rm, loaded, comp);          // rewrite in place
        REQUIRE(disk::readFileText(manifestPath) == manifest0);   // manifest idempotent
        for (size_t i = 0; i < p.chunks.size(); ++i) {
            const auto blob = disk::readFileBytes(std::filesystem::path(dir) / rm.chunksDir / disk::chunkFileName(p.chunks[i].coord));
            REQUIRE(blob == blobs0[i]);                           // chunk blob idempotent
        }
    }
}

// Run a profile through every transport: RAM raw, RAM zlib, disk raw, disk zlib.
static void exerciseProfile(const Profile& p) {
    const Compressor z = codec::zlibCompressor();
    for (const auto& ch : p.chunks) {
        cycleRam(ch, p.manifest.fields, nullptr);
        cycleRam(ch, p.manifest.fields, &z);
    }
    cycleDisk(p, nullptr);
    cycleDisk(p, &z);
}

// ------------------------------------------------------------------------------------------------
// The three "formats" (profiles)
// ------------------------------------------------------------------------------------------------

// A — small, square, dense: the baseline (4×4×1, 2 fields, all present).
static Profile makeProfileA() {
    Profile p;
    p.tag = "A_small_dense";
    p.manifest.coordinate.topology = "square";
    p.manifest.coordinate.cellSize = {{1.0, 1.0}};
    p.manifest.coordinate.boundsMin = {{-100, -100, 0}};
    p.manifest.coordinate.boundsMax = {{100, 100, 0}};
    p.manifest.coordinate.chunkDims = {{4, 4, 1}};
    p.manifest.fields = {
        FieldDecl{"elevation", Encoding::Int, 16, 0.5, -5000.0},
        FieldDecl{"temperature", Encoding::Float32, 0},
    };
    const uint32_t cc = 4u * 4u * 1u;
    const std::vector<bool> all(p.manifest.fields.size(), true);
    for (ChunkCoord coord : {ChunkCoord{0, 0, 0}, ChunkCoord{1, 0, 0}, ChunkCoord{-2, 3, 0}}) {
        p.chunks.push_back(buildChunk(coord, cc, p.manifest.fields, all));
    }
    return p;
}

// B — big 2D, non-power-of-two, MANY fields, SPARSE: 100×100×1, 6 mixed encodings, some chunks omit fields.
static Profile makeProfileB() {
    Profile p;
    p.tag = "B_big_sparse";
    p.manifest.coordinate.topology = "square";
    p.manifest.coordinate.cellSize = {{2.0, 2.0}};
    p.manifest.coordinate.boundsMin = {{0, 0, 0}};
    p.manifest.coordinate.boundsMax = {{999, 999, 0}};
    p.manifest.coordinate.chunkDims = {{100, 100, 1}};   // non-power-of-two
    p.manifest.fields = {
        FieldDecl{"is_coastal", Encoding::Bit, 0},
        FieldDecl{"biome", Encoding::Uint, 5},
        FieldDecl{"elevation", Encoding::Int, 16, 0.5, -5000.0},
        FieldDecl{"forest", Encoding::Unorm8, 0},
        FieldDecl{"rainfall", Encoding::Unorm16, 0},
        FieldDecl{"temperature", Encoding::Float32, 0},
    };
    const uint32_t cc = 100u * 100u * 1u;
    const std::vector<bool> all(6, true);
    std::vector<bool> sparse = {true, true, true, false, false, true};  // omit forest + rainfall
    p.chunks.push_back(buildChunk(ChunkCoord{0, 0, 0}, cc, p.manifest.fields, all));
    p.chunks.push_back(buildChunk(ChunkCoord{5, -1, 0}, cc, p.manifest.fields, sparse));
    return p;
}

// C — volumetric (Z>1), non-square, non-power-of-two, BYTE-UNALIGNED bit widths: 24×10×4, uint3/12/17.
static Profile makeProfileC() {
    Profile p;
    p.tag = "C_volumetric_oddbits";
    p.manifest.coordinate.topology = "square";
    p.manifest.coordinate.cellSize = {{1.0, 1.0}};
    p.manifest.coordinate.boundsMin = {{-50, -50, 0}};
    p.manifest.coordinate.boundsMax = {{50, 50, 15}};
    p.manifest.coordinate.chunkDims = {{24, 10, 4}};     // non-square, non-pow2, Z=4
    p.manifest.fields = {
        FieldDecl{"depth", Encoding::Uint, 3},           // 3 bits — crosses byte boundaries
        FieldDecl{"density", Encoding::Uint, 12},        // 12 bits
        FieldDecl{"material", Encoding::Uint, 17},       // 17 bits — spans 3 bytes per value
    };
    const uint32_t cc = 24u * 10u * 4u;
    const std::vector<bool> all(3, true);
    for (ChunkCoord coord : {ChunkCoord{0, 0, 0}, ChunkCoord{0, 0, 1}, ChunkCoord{-1, -1, 3}}) {
        p.chunks.push_back(buildChunk(coord, cc, p.manifest.fields, all));
    }
    return p;
}

// ------------------------------------------------------------------------------------------------
// Cases
// ------------------------------------------------------------------------------------------------

TEST_CASE("mapview robustness - profile A (small/square/dense) survives save+load x3", "[mapview][robustness][unit]") {
    exerciseProfile(makeProfileA());
}

TEST_CASE("mapview robustness - profile B (big 2D / non-pow2 / many fields / sparse) survives save+load x3", "[mapview][robustness][unit]") {
    exerciseProfile(makeProfileB());
}

TEST_CASE("mapview robustness - profile C (volumetric Z / non-square / byte-unaligned bits) survives save+load x3", "[mapview][robustness][unit]") {
    exerciseProfile(makeProfileC());
}
