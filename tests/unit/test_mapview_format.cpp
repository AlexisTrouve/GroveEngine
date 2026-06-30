/**
 * Unit Tests: grove::mapview world-document format (map-viewer engine, slice S0a).
 *
 * WHAT  : Locks the pure, in-RAM chunk codec — the contract that every producer (Theomen, future
 *         games) writes and the viewer reads. Round-trips a SPARSE chunk (one field present, one
 *         absent) through serialize/deserialize and asserts: exact bit round-trip, presence mask,
 *         and the fail-franc rule that ABSENT reads back as absent, never as 0.
 *
 * WHY    : This is the frozen interface that parallelizes the build (engine core ‖ Theomen adapter)
 *         — if it is wrong, three repos break. So the negative controls matter as much as the happy
 *         path: a wrong bit-width or a corrupt blob must fail LOUDLY, never return silent garbage.
 *
 * HOW    : Pure std-only + Catch2, no GPU/SDL/IIO — runs headless (also on WSL for the sanitizer
 *         sweep later). Raw values are asserted directly (the codec's job); one decode check covers
 *         the physical interpretation (int16 elevation with scale/offset).
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/mapview/ChunkCodec.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/WorldDocument.h"

using namespace grove::mapview;
using Catch::Matchers::WithinAbs;

// The schema both writer and reader agree on: one present field + one that this chunk omits.
static std::vector<FieldDecl> twoFieldSchema() {
    return {
        FieldDecl{"elevation", Encoding::Int, 16, 0.5, -5000.0},  // int16 -> metres
        FieldDecl{"temperature", Encoding::Float32, 32},          // absent in the test chunk
    };
}

TEST_CASE("mapview S0a - sparse chunk round-trips; absent reads back absent, not zero", "[mapview][format][unit]") {
    const auto schema = twoFieldSchema();

    // A 4x4x1 chunk: elevation present with known raw values, temperature ABSENT.
    ChunkData chunk;
    chunk.coord = {3, -7, 0};
    chunk.cellCount = 16;
    std::vector<uint32_t> elevRaw = {0, 1, 2, 3, 100, 200, 4095, 65535,
                                     32768, 12345, 7, 8, 9, 10, 11, 12};
    chunk.fields.emplace_back("elevation", elevRaw);
    // temperature intentionally not added.

    const std::vector<uint8_t> blob = serializeChunk(chunk, schema);
    const ChunkData back = deserializeChunk(blob, schema);

    // Header round-trip.
    REQUIRE(back.coord == ChunkCoord{3, -7, 0});
    REQUIRE(back.cellCount == 16);

    // Presence mask: elevation present, temperature absent.
    REQUIRE(back.has("elevation"));
    REQUIRE_FALSE(back.has("temperature"));

    // Exact bit round-trip of the present field.
    REQUIRE(back.get("elevation") != nullptr);
    REQUIRE(*back.get("elevation") == elevRaw);

    // Fail-franc: an absent field is ABSENT, not a zero-filled vector.
    REQUIRE(back.get("temperature") == nullptr);
}

TEST_CASE("mapview S0a - physical decode applies scale/offset (int16 -> metres)", "[mapview][format][unit]") {
    const FieldDecl elev{"elevation", Encoding::Int, 16, 0.5, -5000.0};

    // raw 11000 (positive in 16-bit signed) -> 11000*0.5 - 5000 = +500 m
    REQUIRE_THAT(decodePhysical(elev, 11000), WithinAbs(500.0, 1e-9));
    // raw 0 -> -5000 m ; raw at the bottom of the range stays well-defined
    REQUIRE_THAT(decodePhysical(elev, 0), WithinAbs(-5000.0, 1e-9));
    // raw 65535 == -1 in 16-bit signed -> -1*0.5 - 5000 = -5000.5 m
    REQUIRE_THAT(decodePhysical(elev, 65535), WithinAbs(-5000.5, 1e-9));
}

TEST_CASE("mapview S0a - codec negative control: wrong width unpack fails loudly", "[mapview][format][unit]") {
    std::vector<uint32_t> vals(16, 1234);          // fits in 16 bits
    const auto packed = codec::packBits(vals, 16); // 16*16/8 = 32 bytes
    // Unpacking at a WIDER declared width needs more bytes than exist -> loud throw, no garbage.
    REQUIRE_THROWS_AS(codec::unpackBits(packed, 24, 16), std::out_of_range);
    // The correct width still round-trips.
    REQUIRE(codec::unpackBits(packed, 16, 16) == vals);
}

TEST_CASE("mapview S0a - codec negative control: out-of-range value is rejected", "[mapview][format][unit]") {
    std::vector<uint32_t> vals = {5, 99};          // 99 needs 7 bits
    REQUIRE_THROWS_AS(codec::packBits(vals, 4), std::out_of_range);
}

TEST_CASE("mapview S0a - document negative control: schema bit-width disagreement fails loudly", "[mapview][format][unit]") {
    // Writer stores elevation as a 16-bit field.
    std::vector<FieldDecl> writeSchema = {FieldDecl{"elevation", Encoding::Int, 16}};
    ChunkData chunk;
    chunk.cellCount = 16;
    chunk.fields.emplace_back("elevation", std::vector<uint32_t>(16, 1234));
    const auto blob = serializeChunk(chunk, writeSchema);

    // Reader's schema disagrees on the width (8 bits) -> the stored byte length cannot match
    // what 8 bits implies, so deserialize must throw rather than unpack misaligned garbage.
    std::vector<FieldDecl> readSchema = {FieldDecl{"elevation", Encoding::Int, 8}};
    REQUIRE_THROWS_AS(deserializeChunk(blob, readSchema), std::runtime_error);
}

TEST_CASE("mapview S0a - corrupt/truncated blob fails loudly", "[mapview][format][unit]") {
    const auto schema = twoFieldSchema();
    ChunkData chunk;
    chunk.cellCount = 4;
    chunk.fields.emplace_back("elevation", std::vector<uint32_t>{1, 2, 3, 4});
    auto blob = serializeChunk(chunk, schema);

    // Bad magic.
    auto badMagic = blob;
    badMagic[0] = 'X';
    REQUIRE_THROWS_AS(deserializeChunk(badMagic, schema), std::runtime_error);

    // Truncated mid-blob -> reader runs past the end and throws.
    std::vector<uint8_t> truncated(blob.begin(), blob.begin() + blob.size() / 2);
    REQUIRE_THROWS(deserializeChunk(truncated, schema));
}
