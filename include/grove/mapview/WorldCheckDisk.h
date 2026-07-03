#pragma once

/**
 * grove::mapview::disk — the disk-driving half of the world-document validator (slice S3 support tool).
 *
 * WHAT  : checkWorldDir(dir) loads a `.world` off disk (manifest + every chunk blob) and runs the pure
 *         WorldCheck passes over it, returning a Report. It also enumerates the chunks directory and
 *         parses each chunk filename back to a coord so the "blob coord vs. filename coord" cross-check
 *         (C1) has both sides. Read failures (missing/corrupt manifest or chunk) become Diagnostics too,
 *         not exceptions the caller must juggle.
 *
 * WHY   : WorldCheck.h is kept pure (no filesystem, no compression) so it unit-tests in RAM. This header
 *         is the one transport-coupled piece — the mirror of WorldDocumentDisk.h, which it reuses. A
 *         consumer that reads a `.world` from somewhere else (a packed region file, a network) can write
 *         its own driver against the same pure checks.
 *
 * HOW   : std::filesystem enumerate + disk::readManifest/readChunk (from WorldDocumentDisk.h). A single
 *         zlib decompressor is injected into readChunk; it is consulted only when a chunk's own
 *         compressionFlag says it is compressed, so one path validates both raw and compressed documents.
 *         Every read is wrapped: a throw (the format's fail-franc) is turned into an Error diagnostic and
 *         validation continues to the next chunk, so ONE bad chunk does not hide the rest.
 */

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "grove/mapview/Compression.h"
#include "grove/mapview/Coord.h"
#include "grove/mapview/Manifest.h"
#include "grove/mapview/WorldCheck.h"
#include "grove/mapview/WorldDocument.h"
#include "grove/mapview/WorldDocumentDisk.h"

namespace grove {
namespace mapview {
namespace disk {

namespace detail {

// Parse an integer that must consume the WHOLE token (no trailing garbage) — a partial parse of
// "12x" would silently accept a malformed name, which we refuse.
inline bool parseWholeInt(const std::string& s, long long& out) {
    if (s.empty()) return false;
    std::size_t pos = 0;
    try {
        out = std::stoll(s, &pos);
    } catch (...) {
        return false;
    }
    return pos == s.size();
}

// Parse a chunk blob filename ("c_<x>_<y>_<z>.gmvc", the sign-preserving name from chunkFileName())
// back to a ChunkCoord. Returns false for any name that is not exactly that shape or whose numbers do
// not fit the coord types — the caller flags such a file as unexpected rather than trusting it.
inline bool parseChunkFileName(const std::string& name, ChunkCoord& out) {
    const std::string prefix = "c_";
    const std::string suffix = ".gmvc";
    if (name.size() <= prefix.size() + suffix.size()) return false;
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;

    // The "<x>_<y>_<z>" middle. Split on '_' into exactly three integer tokens (negatives keep their '-').
    const std::string mid = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    std::vector<std::string> toks;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= mid.size(); ++i) {
        if (i == mid.size() || mid[i] == '_') {
            toks.push_back(mid.substr(start, i - start));
            start = i + 1;
        }
    }
    if (toks.size() != 3) return false;

    long long x = 0, y = 0, z = 0;
    if (!parseWholeInt(toks[0], x) || !parseWholeInt(toks[1], y) || !parseWholeInt(toks[2], z)) return false;
    // Range-check against the coord's storage widths (x/y int32, z int16) — an out-of-range value is
    // not a coord this format could have written.
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
        z < INT16_MIN || z > INT16_MAX) {
        return false;
    }
    out.x = static_cast<std::int32_t>(x);
    out.y = static_cast<std::int32_t>(y);
    out.z = static_cast<std::int16_t>(z);
    return true;
}

} // namespace detail

// Validate a `.world` directory end-to-end. Never throws: a read failure is reported as a Diagnostic.
// A missing/unparseable manifest, or a missing chunks directory, is fatal for the run (returns early
// with what was found so far); a single bad chunk is not (it is reported and the scan continues).
inline Report checkWorldDir(const std::string& dir) {
    namespace fs = std::filesystem;
    Report r;

    // 1. Manifest — fatal if unreadable (there is nothing to validate a chunk against without it).
    Manifest m;
    try {
        m = disk::readManifest(dir);
    } catch (const std::exception& e) {
        r.add(Severity::Error, "io", "manifest.json", std::string("cannot read manifest: ") + e.what());
        return r;
    }
    r.fieldCount = m.fields.size();
    checkManifest(m, r);

    // 2. Chunks directory — fatal if absent (the manifest points at chunks that are not there).
    const fs::path chunkRoot = fs::path(dir) / m.chunksDir;
    std::error_code ec;
    if (!fs::is_directory(chunkRoot, ec)) {
        r.add(Severity::Error, "io", m.chunksDir, "chunks directory not found");
        return r;
    }

    // 3. One injected zlib decompressor, used only for chunks that flag themselves compressed (raw
    //    chunks ignore it) → a single scan validates a raw OR a compressed document.
    const Compressor z = codec::zlibCompressor();

    std::size_t chunkFiles = 0;
    for (const auto& entry : fs::directory_iterator(chunkRoot, ec)) {
        std::error_code fec;
        if (!entry.is_regular_file(fec)) continue;
        const std::string name = entry.path().filename().string();

        ChunkCoord coord;
        if (!detail::parseChunkFileName(name, coord)) {
            r.add(Severity::Warning, "io", name, "unexpected file in chunks dir (not a c_x_y_z.gmvc chunk)");
            continue;
        }
        ++chunkFiles;

        ChunkData c;
        try {
            c = disk::readChunk(dir, m, coord, &z);
        } catch (const std::exception& e) {
            r.add(Severity::Error, "io",
                  "chunk " + grove::mapview::detail::coordStr(coord.x, coord.y, coord.z),
                  std::string("unreadable: ") + e.what());
            continue;
        }
        r.totalCells += c.cellCount;
        checkChunk(m, coord, c, r);
    }
    r.chunkCount = chunkFiles;

    // 4. A manifest whose chunks dir holds no actual chunk blobs renders an empty world.
    if (chunkFiles == 0) {
        r.add(Severity::Warning, "io", m.chunksDir, "no chunk files found");
    }
    return r;
}

} // namespace disk
} // namespace mapview
} // namespace grove
