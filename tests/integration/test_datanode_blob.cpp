/**
 * Unit Tests: JsonDataNode BINARY PAYLOAD (blobs) + throw-proof serialize().
 *
 * WHAT : Locks the first-class binary payload added to make the bulk fast path (render:sprite:batch) stop
 *        abusing a JSON string, and to close the replay/network crash. Covers: setBlob/getBlob round-trip,
 *        serialize() never throwing on non-UTF8 bytes (a blob OR the legacy blob-in-a-string), the blob
 *        coming back base64-encoded + decodable, and base64 encode/decode symmetry across lengths.
 *
 * WHY  : A raw byte buffer is not valid UTF-8, so `m_data.dump()` threw when a payload carried binary —
 *        exactly the trap surfaced verifying the flat-blob commit (ReplaySink capturePayload -> crash). The
 *        blob rides BESIDE the json (raw, zero-copy in-process); serialize() base64s it so the debug/replay/
 *        network text stays valid. These tests are the proof both hold, not just compile.
 */

#include <catch2/catch_test_macros.hpp>

#include "grove/JsonDataNode.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace grove;

TEST_CASE("JsonDataNode blob - setBlob/getBlob round-trips raw bytes", "[datanode][blob][unit]") {
    JsonDataNode n("m");
    const std::vector<uint8_t> raw = {0x00, 0xFF, 0x10, 0x80, 0x00, 0x7F};  // NUL + high bytes = NOT valid UTF-8
    n.setBlob("payload", raw.data(), raw.size());

    const std::vector<uint8_t>* got = n.getBlob("payload");
    REQUIRE(got != nullptr);
    REQUIRE(*got == raw);
    REQUIRE(n.getBlob("absent") == nullptr);   // fail-franc: absent is nullptr, never an empty vector
}

TEST_CASE("JsonDataNode serialize - throw-proof on a non-UTF8 blob + base64 round-trips", "[datanode][blob][unit]") {
    JsonDataNode n("m");
    n.setInt("count", 3);                                                   // a normal json header field
    const std::vector<uint8_t> raw = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x80, 0xFF};  // invalid UTF-8
    n.setBlob("spriteData", raw.data(), raw.size());

    std::string s;
    REQUIRE_NOTHROW(s = n.serialize());        // THE closed hole: serialize() must never throw on binary

    const json j = json::parse(s);
    REQUIRE(j.at("count") == 3);               // the json header rode along
    REQUIRE(j.contains("__blobs__"));
    const std::string b64 = j.at("__blobs__").at("spriteData").get<std::string>();
    REQUIRE(JsonDataNode::base64Decode(b64) == raw);   // the exact bytes come back
}

TEST_CASE("JsonDataNode serialize - throw-proof on a LEGACY non-UTF8 string", "[datanode][blob][unit]") {
    // The pre-existing hack: raw bytes smuggled in a JSON string. A plain dump() THREW on this -> replay crash.
    JsonDataNode n("m");
    const char bytes[] = {char(0xFF), char(0x00), char(0x80), char(0x41)};
    n.setString("spriteData", std::string(bytes, sizeof(bytes)));
    REQUIRE_NOTHROW(n.serialize());            // error_handler::replace -> U+FFFD substitution, never a throw
}

TEST_CASE("JsonDataNode base64 - encode/decode symmetric across lengths (padding)", "[datanode][blob][unit]") {
    for (size_t len = 0; len <= 16; ++len) {   // exercises every mod-3 padding case (0/1/2 trailing bytes)
        std::vector<uint8_t> raw(len);
        for (size_t i = 0; i < len; ++i) raw[i] = static_cast<uint8_t>(i * 37 + 5);
        REQUIRE(JsonDataNode::base64Decode(JsonDataNode::base64Encode(raw)) == raw);
    }
}
