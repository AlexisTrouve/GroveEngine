#pragma once

/**
 * grove::mapview::WorldDocument — the in-RAM chunk model + chunk (de)serialization (slice S0a).
 *
 * WHAT  : ChunkData is the decoded, RAM-side representation of one chunk: its coord, cell count,
 *         and the PRESENT fields' raw values. serializeChunk/deserializeChunk move it to/from a
 *         self-contained binary blob (the on-disk chunk format, §3.4). Absent fields are simply
 *         not present — never stored, never zeroed.
 *
 * WHY   : Sparsity is doctrine: a generator that fills only `elevation` produces a perfectly valid
 *         chunk; the viewer draws elevation and leaves the rest blank. "Absent" must read back as
 *         ABSENT, not as 0 — else "no elevation" silently becomes "sea level", a fallback masking a
 *         hole. get() returns nullptr for an absent field precisely so a caller cannot mistake it
 *         for a zero value.
 *
 * HOW   : Header-only, std-only (no JSON, no renderer — that is S0c/S1). The blob is written with a
 *         DEFINED little-endian byte order via explicit byte puts (never memcpy of an int), matching
 *         the codec's portable bit order, so a chunk round-trips across machines. Layout:
 *
 *           magic "GMVC" (4) | version u16 | coord(x i32, y i32, z i16) | cellCount u32 |
 *           nFields u16 | presenceMask ceil(nFields/8) bytes (bit i = schema field i present) |
 *           compressionFlag u8 (0 = none; S0b sets it) |
 *           per PRESENT field, in schema order: packedLen u32 | packed bytes
 *
 *         deserialize cross-checks each present field's stored byte length against what the schema's
 *         declared width implies (the document-level negative control): a schema/blob bit-width
 *         disagreement throws loudly instead of unpacking garbage. A short/corrupt blob also throws.
 */

#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "grove/mapview/ChunkCodec.h"
#include "grove/mapview/Coord.h"
#include "grove/mapview/Field.h"

namespace grove {
namespace mapview {

// One chunk, decoded into RAM. Holds only the fields that are PRESENT (sparse).
struct ChunkData {
    ChunkCoord coord{};
    uint32_t   cellCount{0};
    // Present fields, raw bit values (one per cell), in insertion order.
    std::vector<std::pair<std::string, std::vector<uint32_t>>> fields;

    bool has(const std::string& name) const {
        for (const auto& f : fields) {
            if (f.first == name) return true;
        }
        return false;
    }

    // Raw values for a present field, or nullptr if ABSENT (fail-franc: never a zero vector).
    const std::vector<uint32_t>* get(const std::string& name) const {
        for (const auto& f : fields) {
            if (f.first == name) return &f.second;
        }
        return nullptr;
    }
};

// An injected per-chunk compressor. POURQUOI: the format core stays dependency-free — it knows
// nothing of miniz/zlib. A consumer that wants compression supplies these two functions (e.g.
// grove::mapview::codec::zlibCompressor() from Compression.h, backed by the vendored miniz);
// serialize/deserialize call them only when a compressor is provided. COMMENT: named *Fn to dodge
// miniz's zlib-compat `compress`/`uncompress` macros. Passing none = uncompressed (the S0a path).
struct Compressor {
    std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>         compressFn;
    std::function<std::vector<uint8_t>(const std::vector<uint8_t>&, size_t)> decompressFn;
};

namespace detail {

// --- Little-endian writers: defined byte order, never a memcpy of a wider int. -----------------
inline void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
inline void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
inline void putI16(std::vector<uint8_t>& b, int16_t v) { putU16(b, static_cast<uint16_t>(v)); }
inline void putI32(std::vector<uint8_t>& b, int32_t v) { putU32(b, static_cast<uint32_t>(v)); }

// --- Bounds-checked little-endian reader: a short/corrupt blob throws, never reads past end. ----
struct Reader {
    const uint8_t* p;
    size_t n;
    size_t i{0};

    void need(size_t k) const {
        if (i + k > n) throw std::out_of_range("WorldDocument: truncated/corrupt chunk blob");
    }
    uint8_t  u8()  { need(1); return p[i++]; }
    uint16_t u16() { need(2); uint16_t v = static_cast<uint16_t>(p[i] | (p[i + 1] << 8)); i += 2; return v; }
    uint32_t u32() {
        need(4);
        uint32_t v = static_cast<uint32_t>(p[i]) | (static_cast<uint32_t>(p[i + 1]) << 8) |
                     (static_cast<uint32_t>(p[i + 2]) << 16) | (static_cast<uint32_t>(p[i + 3]) << 24);
        i += 4;
        return v;
    }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    void read(uint8_t* out, size_t k) { need(k); std::memcpy(out, p + i, k); i += k; }
};

} // namespace detail

// Serialize one chunk to a self-contained binary blob, per `schema` (the manifest's ordered fields).
// Pass a Compressor to zlib the body (off the hot path — §3.5); pass nullptr (default) for raw S0a output.
// THROWS std::invalid_argument if a present field is not in the schema, or its value count != cellCount.
inline std::vector<uint8_t> serializeChunk(const ChunkData& chunk, const std::vector<FieldDecl>& schema,
                                           const Compressor* comp = nullptr) {
    // Decide compression upfront: the flag is part of the header, written before the body.
    const bool doCompress = (comp != nullptr && static_cast<bool>(comp->compressFn));

    std::vector<uint8_t> blob;
    blob.push_back('G');
    blob.push_back('M');
    blob.push_back('V');
    blob.push_back('C');
    detail::putU16(blob, 1);                 // version
    detail::putI32(blob, chunk.coord.x);
    detail::putI32(blob, chunk.coord.y);
    detail::putI16(blob, chunk.coord.z);
    detail::putU32(blob, chunk.cellCount);
    detail::putU16(blob, static_cast<uint16_t>(schema.size()));

    // Presence mask over schema order (bit i set => schema[i] is present in this chunk).
    const size_t maskBytes = (schema.size() + 7) / 8;
    std::vector<uint8_t> mask(maskBytes, 0);
    for (size_t fi = 0; fi < schema.size(); ++fi) {
        if (chunk.has(schema[fi].name)) {
            mask[fi >> 3] |= static_cast<uint8_t>(1u << (fi & 7));
        }
    }
    blob.insert(blob.end(), mask.begin(), mask.end());
    blob.push_back(doCompress ? 1 : 0);      // compressionFlag

    // Validate every present field is declared by the schema (no orphan fields on disk).
    for (const auto& f : chunk.fields) {
        bool inSchema = false;
        for (const auto& d : schema) {
            if (d.name == f.first) { inSchema = true; break; }
        }
        if (!inSchema) {
            throw std::invalid_argument("serializeChunk: field '" + f.first + "' is not in the schema");
        }
    }

    // Build the body (the bulk numbers) separately, so it can be emitted raw OR compressed as a unit.
    // Per present field, in schema order: packedLen u32 | packed bytes.
    std::vector<uint8_t> body;
    for (const auto& d : schema) {
        const std::vector<uint32_t>* vals = chunk.get(d.name);
        if (!vals) continue;  // absent => not stored
        if (vals->size() != chunk.cellCount) {
            throw std::invalid_argument("serializeChunk: field '" + d.name + "' has " +
                                        std::to_string(vals->size()) + " values, expected " +
                                        std::to_string(chunk.cellCount));
        }
        std::vector<uint8_t> packed = codec::packBits(*vals, d.storageBits());
        detail::putU32(body, static_cast<uint32_t>(packed.size()));
        body.insert(body.end(), packed.begin(), packed.end());
    }

    if (!doCompress) {
        blob.insert(blob.end(), body.begin(), body.end());
    } else {
        // Compressed body section: uncompressedLen u32 | compressedLen u32 | compressed bytes.
        // POURQUOI both lengths: uncompressedLen sizes the decode buffer; compressedLen keeps the
        // chunk self-delimiting (so many chunks can later share one region file, S0c).
        std::vector<uint8_t> packedBody = comp->compressFn(body);
        detail::putU32(blob, static_cast<uint32_t>(body.size()));
        detail::putU32(blob, static_cast<uint32_t>(packedBody.size()));
        blob.insert(blob.end(), packedBody.begin(), packedBody.end());
    }
    return blob;
}

// Deserialize a chunk blob using `schema`. Pass the same Compressor used to write it if the chunk is
// compressed (else it throws — a compressed chunk with no decompressor is an error, not a silent skip).
// THROWS on a corrupt/truncated blob, a schema field-count mismatch, an unknown compression flag, or —
// the document-level negative control — when a present field's stored byte length disagrees with the
// width the schema declares.
inline ChunkData deserializeChunk(const std::vector<uint8_t>& blob, const std::vector<FieldDecl>& schema,
                                  const Compressor* comp = nullptr) {
    detail::Reader r{blob.data(), blob.size(), 0};

    uint8_t magic[4];
    r.read(magic, 4);
    if (magic[0] != 'G' || magic[1] != 'M' || magic[2] != 'V' || magic[3] != 'C') {
        throw std::runtime_error("deserializeChunk: bad magic (not a GMVC chunk blob)");
    }
    const uint16_t version = r.u16();
    if (version != 1) {
        throw std::runtime_error("deserializeChunk: unsupported version " + std::to_string(version));
    }

    ChunkData c;
    c.coord.x = r.i32();
    c.coord.y = r.i32();
    c.coord.z = r.i16();
    c.cellCount = r.u32();

    const uint16_t nFields = r.u16();
    if (nFields != schema.size()) {
        throw std::runtime_error("deserializeChunk: schema field-count mismatch (blob " +
                                 std::to_string(nFields) + ", schema " + std::to_string(schema.size()) + ")");
    }

    const size_t maskBytes = (static_cast<size_t>(nFields) + 7) / 8;
    std::vector<uint8_t> mask(maskBytes);
    r.read(mask.data(), maskBytes);

    const uint8_t compressionFlag = r.u8();

    // The field-parse loop, identical whether the body is read raw (from `r`) or from the decompressed
    // buffer. Cross-checks each present field's stored length against the schema width — the
    // document-level negative control against a wrong bit-width (no silent garbage).
    auto parseFields = [&](detail::Reader& src) {
        for (size_t fi = 0; fi < schema.size(); ++fi) {
            const bool present = (mask[fi >> 3] >> (fi & 7)) & 1u;
            if (!present) continue;

            const FieldDecl& d = schema[fi];
            const uint32_t packedLen = src.u32();
            const uint8_t bits = d.storageBits();
            const size_t expected = codec::bytesForBits(static_cast<size_t>(c.cellCount) * bits);
            if (packedLen != expected) {
                throw std::runtime_error("deserializeChunk: field '" + d.name +
                                         "' blob/schema bit-width mismatch (blob " + std::to_string(packedLen) +
                                         " bytes, schema implies " + std::to_string(expected) + ")");
            }
            std::vector<uint8_t> packed(packedLen);
            src.read(packed.data(), packedLen);
            c.fields.emplace_back(d.name, codec::unpackBits(packed, bits, c.cellCount));
        }
    };

    if (compressionFlag == 0) {
        parseFields(r);
    } else if (compressionFlag == 1) {
        if (comp == nullptr || !static_cast<bool>(comp->decompressFn)) {
            throw std::runtime_error("deserializeChunk: chunk is compressed but no decompressor was provided");
        }
        const uint32_t uncompLen = r.u32();
        const uint32_t compLen = r.u32();
        std::vector<uint8_t> packedBody(compLen);
        r.read(packedBody.data(), compLen);  // throws if the blob is truncated (compLen > remaining)
        std::vector<uint8_t> body = comp->decompressFn(packedBody, uncompLen);
        if (body.size() != uncompLen) {
            throw std::runtime_error("deserializeChunk: decompressed body size mismatch");
        }
        detail::Reader br{body.data(), body.size(), 0};
        parseFields(br);
    } else {
        throw std::runtime_error("deserializeChunk: unknown compression flag " + std::to_string(compressionFlag));
    }
    return c;
}

} // namespace mapview
} // namespace grove
