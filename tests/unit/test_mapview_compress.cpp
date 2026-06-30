/**
 * Unit Tests: grove::mapview per-chunk compression (map-viewer engine, slice S0b).
 *
 * WHAT  : Locks the injected-compressor path — a chunk's body deflated with the vendored miniz (zlib)
 *         round-trips byte-identically to the raw form, and every failure mode (no decompressor for a
 *         compressed chunk, corrupt/truncated payload) fails LOUDLY rather than yielding garbage.
 *
 * WHY    : Compression is a per-chunk flag in the contract; it must be transparent (same ChunkData out)
 *         and the core must stay dependency-free (only this test links miniz.c — the format test does not).
 *
 * HOW    : Pure std-only + Catch2 + miniz.c (linked by the target). The same Compressor is passed to
 *         serialize and deserialize; correctness is asserted against the uncompressed round-trip.
 */

#include <catch2/catch_test_macros.hpp>

#include "grove/mapview/Compression.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/WorldDocument.h"

using namespace grove::mapview;

// A two-field schema: a 16-bit signed elevation + a 5-bit biome category.
static std::vector<FieldDecl> schemaOf() {
    return {
        FieldDecl{"elevation", Encoding::Int, 16, 0.5, -5000.0},
        FieldDecl{"biome", Encoding::Uint, 5},
    };
}

TEST_CASE("mapview S0b - compressed chunk round-trips identically to raw", "[mapview][compress][unit]") {
    const auto schema = schemaOf();

    ChunkData chunk;
    chunk.coord = {1, 2, 0};
    chunk.cellCount = 64;
    std::vector<uint32_t> elev(64), biome(64);
    for (uint32_t i = 0; i < 64; ++i) {
        elev[i] = (i * 577u) & 0xFFFFu;  // within 16 bits
        biome[i] = i % 19;               // within 5 bits
    }
    chunk.fields.emplace_back("elevation", elev);
    chunk.fields.emplace_back("biome", biome);

    const Compressor z = codec::zlibCompressor();
    const std::vector<uint8_t> raw    = serializeChunk(chunk, schema);       // uncompressed (S0a)
    const std::vector<uint8_t> packed = serializeChunk(chunk, schema, &z);   // compressed (S0b)

    // The two encodings differ on the wire (flag + deflated body)...
    REQUIRE(raw != packed);

    // ...but decode to the same values.
    const ChunkData a = deserializeChunk(raw, schema);
    const ChunkData b = deserializeChunk(packed, schema, &z);
    REQUIRE(b.coord == ChunkCoord{1, 2, 0});
    REQUIRE(b.cellCount == 64);
    REQUIRE(b.get("elevation") != nullptr);
    REQUIRE(b.get("biome") != nullptr);
    REQUIRE(*b.get("elevation") == elev);
    REQUIRE(*b.get("biome") == biome);
    REQUIRE(*a.get("elevation") == *b.get("elevation"));
    REQUIRE(*a.get("biome") == *b.get("biome"));
}

TEST_CASE("mapview S0b - a compressed chunk without a decompressor fails loudly", "[mapview][compress][unit]") {
    const auto schema = schemaOf();
    ChunkData chunk;
    chunk.cellCount = 16;
    chunk.fields.emplace_back("elevation", std::vector<uint32_t>(16, 1234));  // biome absent
    const Compressor z = codec::zlibCompressor();
    const auto packed = serializeChunk(chunk, schema, &z);

    // Reading a flag==1 chunk with no decompressor is an error, never a silent skip.
    REQUIRE_THROWS_AS(deserializeChunk(packed, schema), std::runtime_error);
    // With the decompressor it reads back fine (and the absent field stays absent).
    const ChunkData back = deserializeChunk(packed, schema, &z);
    REQUIRE(*back.get("elevation") == std::vector<uint32_t>(16, 1234));
    REQUIRE(back.get("biome") == nullptr);
}

TEST_CASE("mapview S0b - corrupt / truncated compressed payload fails loudly", "[mapview][compress][unit]") {
    const auto schema = schemaOf();
    ChunkData chunk;
    chunk.cellCount = 32;
    std::vector<uint32_t> elev(32);
    for (uint32_t i = 0; i < 32; ++i) elev[i] = (i * 131u) & 0xFFFFu;
    chunk.fields.emplace_back("elevation", elev);
    const Compressor z = codec::zlibCompressor();
    const auto good = serializeChunk(chunk, schema, &z);

    // (a) Content corruption: flip bytes across the deflated payload -> zlib check fails.
    auto flipped = good;
    for (size_t i = good.size() / 2; i < good.size(); ++i) flipped[i] ^= 0xA5;
    REQUIRE_THROWS(deserializeChunk(flipped, schema, &z));

    // (b) Truncation: drop the tail -> the reader runs past the stored compressedLen and throws.
    std::vector<uint8_t> truncated(good.begin(), good.end() - 4);
    REQUIRE_THROWS(deserializeChunk(truncated, schema, &z));
}
