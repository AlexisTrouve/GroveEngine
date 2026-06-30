#pragma once

/**
 * grove::mapview::disk — reference on-disk reader/writer for a world-document (slice S0c).
 *
 * WHAT  : Writes a manifest.json + one binary blob per chunk to a directory, and reads them back —
 *         the "disk-packed" half of §3.6. A reader can load the manifest, then load any chunk on
 *         demand by coord (the seed of the S1 ChunkProvider).
 *
 * WHY   : This is the concrete I/O the pure core deliberately does NOT contain — the format (Manifest.h,
 *         WorldDocument.h) is string/bytes only, so it stays testable and transport-agnostic. This header
 *         is one reference transport (plain files); a host is free to replace it (a packed region file, a
 *         network provider) without touching the format. Keeping it separate also means a consumer that
 *         never touches disk never pays for <filesystem>.
 *
 * HOW   : std::filesystem + std::fstream, header-only. Chunk files are named c_<x>_<y>_<z>.gmvc (negative
 *         coords keep their sign). A missing chunk file throws (fail-franc — "outside bounds" is the
 *         ChunkProvider's policy in S1, not a silent empty here). The same Compressor* passed to write
 *         must be passed to read a compressed document.
 */

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "grove/mapview/Coord.h"
#include "grove/mapview/Manifest.h"
#include "grove/mapview/WorldDocument.h"

namespace grove {
namespace mapview {
namespace disk {

// The blob filename for a chunk coord (stable, sign-preserving).
inline std::string chunkFileName(ChunkCoord c) {
    return "c_" + std::to_string(c.x) + "_" + std::to_string(c.y) + "_" + std::to_string(c.z) + ".gmvc";
}

// --- Small file helpers (throw on failure — never a silent partial read/write). -----------------
inline void writeFileBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("writeFileBytes: cannot open '" + path.string() + "'");
    if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()),
                                  static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("writeFileBytes: write failed for '" + path.string() + "'");
}
inline std::vector<uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("readFileBytes: cannot open '" + path.string() + "'");
    const std::streamsize n = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(n));
    if (n > 0) in.read(reinterpret_cast<char*>(bytes.data()), n);
    if (!in) throw std::runtime_error("readFileBytes: read failed for '" + path.string() + "'");
    return bytes;
}
inline void writeFileText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("writeFileText: cannot open '" + path.string() + "'");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) throw std::runtime_error("writeFileText: write failed for '" + path.string() + "'");
}
inline std::string readFileText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("readFileText: cannot open '" + path.string() + "'");
    const std::streamsize n = in.tellg();
    in.seekg(0);
    std::string text(static_cast<size_t>(n), '\0');
    if (n > 0) in.read(text.data(), n);
    if (!in) throw std::runtime_error("readFileText: read failed for '" + path.string() + "'");
    return text;
}

// Write a full world-document: <dir>/manifest.json + <dir>/<chunksDir>/c_x_y_z.gmvc per chunk.
// Pass `comp` to deflate the chunk bodies (the manifest is always plain JSON).
inline void writeWorldDocument(const std::string& dir, const Manifest& manifest,
                               const std::vector<ChunkData>& chunks, const Compressor* comp = nullptr) {
    const std::filesystem::path root(dir);
    const std::filesystem::path chunkRoot = root / manifest.chunksDir;
    std::filesystem::create_directories(chunkRoot);

    writeFileText(root / "manifest.json", emitManifest(manifest));
    for (const auto& chunk : chunks) {
        const std::vector<uint8_t> blob = serializeChunk(chunk, manifest.fields, comp);
        writeFileBytes(chunkRoot / chunkFileName(chunk.coord), blob);
    }
}

// Read just the manifest of a world-document at `dir`.
inline Manifest readManifest(const std::string& dir) {
    return parseManifest(readFileText(std::filesystem::path(dir) / "manifest.json"));
}

// Does a chunk blob exist on disk for this coord?
inline bool hasChunk(const std::string& dir, const Manifest& manifest, ChunkCoord coord) {
    return std::filesystem::exists(std::filesystem::path(dir) / manifest.chunksDir / chunkFileName(coord));
}

// Read one chunk by coord. THROWS if the blob is missing (fail-franc) or corrupt; pass the same
// Compressor used to write a compressed document.
inline ChunkData readChunk(const std::string& dir, const Manifest& manifest, ChunkCoord coord,
                           const Compressor* comp = nullptr) {
    const std::filesystem::path path = std::filesystem::path(dir) / manifest.chunksDir / chunkFileName(coord);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("readChunk: no blob for chunk (" + std::to_string(coord.x) + "," +
                                 std::to_string(coord.y) + "," + std::to_string(coord.z) + ") at '" +
                                 path.string() + "'");
    }
    return deserializeChunk(readFileBytes(path), manifest.fields, comp);
}

} // namespace disk
} // namespace mapview
} // namespace grove
