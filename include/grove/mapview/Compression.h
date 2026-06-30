#pragma once

/**
 * grove::mapview::codec — zlib compression backend for the world-document (slice S0b).
 *
 * WHAT  : A ready-made Compressor (zlibCompressor()) that plugs into serializeChunk/deserializeChunk
 *         to deflate a chunk's body. Backed by the vendored miniz (zlib). Optional: the format core
 *         (WorldDocument.h) has NO dependency on this — only consumers that want compression include it.
 *
 * WHY   : Compression is off the hot path (§3.5): a chunk is decompressed ONCE when the camera nears,
 *         never per frame, so zlib's speed is irrelevant and its in-tree availability beats vendoring a
 *         new LZ4. Keeping it in a separate header preserves the core's "zero dependency" property — the
 *         S0a format test still links nothing, builds on a bare toolchain / WSL.
 *
 * HOW   : Thin wrappers over miniz's single-call mz_compress/mz_uncompress. MINIZ_NO_ZLIB_COMPATIBLE_NAMES
 *         is set BEFORE including miniz.h so its `#define compress mz_compress` (and friends) never leak in
 *         and clobber identifiers named `compress`. We size the compressed buffer with mz_compressBound and
 *         decompress into a buffer sized by the stored uncompressedLen — and fail franc on any rc != MZ_OK
 *         or size mismatch (a corrupt blob must throw, never yield silent garbage).
 *
 * BUILD : the consuming target must also compile miniz.c
 *         (deps/bgfx/bimg/3rdparty/tinyexr/deps/miniz/miniz.c) and add its dir to the include path.
 */

#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif
#include "miniz.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "grove/mapview/WorldDocument.h"  // for grove::mapview::Compressor

namespace grove {
namespace mapview {
namespace codec {

// Deflate `in` to a zlib stream. THROWS std::runtime_error on a miniz failure.
inline std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& in) {
    const mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(in.size()));
    std::vector<uint8_t> out(bound ? static_cast<size_t>(bound) : 1u);
    mz_ulong outLen = static_cast<mz_ulong>(out.size());
    const int rc = mz_compress(out.data(), &outLen, in.data(), static_cast<mz_ulong>(in.size()));
    if (rc != MZ_OK) {
        throw std::runtime_error("zlibCompress: mz_compress failed (rc=" + std::to_string(rc) + ")");
    }
    out.resize(static_cast<size_t>(outLen));
    return out;
}

// Inflate `in` into exactly `expectedLen` bytes. THROWS std::runtime_error on a miniz failure or if the
// decoded length is not exactly `expectedLen` (the corrupt-blob negative control).
inline std::vector<uint8_t> zlibDecompress(const std::vector<uint8_t>& in, size_t expectedLen) {
    if (expectedLen == 0) {
        return {};  // empty body: nothing to inflate (a chunk with no present fields)
    }
    std::vector<uint8_t> out(expectedLen);
    mz_ulong outLen = static_cast<mz_ulong>(expectedLen);
    const int rc = mz_uncompress(out.data(), &outLen, in.data(), static_cast<mz_ulong>(in.size()));
    if (rc != MZ_OK) {
        throw std::runtime_error("zlibDecompress: mz_uncompress failed (rc=" + std::to_string(rc) + ")");
    }
    if (static_cast<size_t>(outLen) != expectedLen) {
        throw std::runtime_error("zlibDecompress: decoded " + std::to_string(outLen) +
                                 " bytes, expected " + std::to_string(expectedLen));
    }
    return out;
}

// A Compressor wired to the miniz/zlib functions above — pass &z to serialize/deserialize.
inline Compressor zlibCompressor() {
    Compressor c;
    c.compressFn   = [](const std::vector<uint8_t>& in) { return zlibCompress(in); };
    c.decompressFn = [](const std::vector<uint8_t>& in, size_t n) { return zlibDecompress(in, n); };
    return c;
}

} // namespace codec
} // namespace mapview
} // namespace grove
