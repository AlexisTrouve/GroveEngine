#pragma once

/**
 * grove::mapview::WorldDocumentProvider — the reference file-backed IChunkProvider (the disk↔core bridge).
 *
 * WHAT  : Adapts a world-document ON DISK (a directory holding manifest.json + chunk blobs) to the
 *         IChunkProvider interface MapView streams from. Point it at a directory and MapView pulls chunks
 *         straight off disk — no in-RAM generator, no host-side JSON. Convenience accessors expose the
 *         manifest's field schema and a GridSpec so the host can build a MapView from the manifest alone.
 *
 * WHY   : This is the missing link that makes "the file is the interface" (the mp4/VLC model) real end to
 *         end. The pure core (MapView) only knows IChunkProvider; the reference transport (WorldDocumentDisk.h)
 *         only knows bytes↔files — NEITHER bridges the two, so a file-backed viewer had to hand-roll the glue.
 *         A generator-backed world implements IChunkProvider directly (e.g. an infinite ProceduralWorld); a
 *         file-backed one uses this. It sits with WorldDocumentDisk.h (the same opt-in <filesystem> tier, NOT
 *         the dependency-free pure-core promise) precisely because every file-backed host wants exactly it.
 *
 * HOW   : Header-only. Holds the manifest (read once at construction) + the document directory + an optional
 *         Compressor. has()/load() delegate to disk::hasChunk / disk::readChunk: a coord with no blob is
 *         ABSENT (has()==false), never a zeroed placeholder (fail-franc — "outside the world" ≠ "sea level").
 *         The Compressor is REQUIRED iff the chunks were written compressed (readChunk throws on a compressed
 *         chunk with no decompressor — never a silent skip). Non-copyable in spirit (holds the manifest by
 *         value): construct once, pass by reference (MapView takes IChunkProvider&, so it must outlive it).
 */

#include <string>
#include <utility>
#include <vector>

#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/Manifest.h"
#include "grove/mapview/MapView.h"          // GridSpec (the geometry a host feeds the orchestrator)
#include "grove/mapview/WorldDocument.h"    // Compressor, ChunkData
#include "grove/mapview/WorldDocumentDisk.h"

namespace grove {
namespace mapview {

// A file-backed IChunkProvider over a world-document directory.
class WorldDocumentProvider final : public IChunkProvider {
public:
    // Open a world-document at `dir`: reads its manifest immediately (throws if absent/malformed — fail-franc,
    // no silent empty world). Pass the SAME Compressor the document was written with iff its chunks are
    // compressed; the default {} means "uncompressed document" (a compressed chunk then fails loudly on load).
    explicit WorldDocumentProvider(std::string dir, Compressor comp = {})
        : dir_(std::move(dir)), comp_(std::move(comp)), manifest_(disk::readManifest(dir_)) {}

    // Build from an already-parsed manifest — avoids a second manifest read when the host already has one.
    WorldDocumentProvider(std::string dir, Manifest manifest, Compressor comp = {})
        : dir_(std::move(dir)), comp_(std::move(comp)), manifest_(std::move(manifest)) {}

    // Availability at the source = a chunk blob exists on disk (cheap: a filesystem existence check).
    bool has(ChunkCoord c) const override { return disk::hasChunk(dir_, manifest_, c); }

    // Decode the chunk from its blob. Precondition (IChunkProvider): has(c) == true. Uses the compressor only
    // when one was supplied — a raw document loads with nullptr, a compressed one with the real decompressor.
    ChunkData load(ChunkCoord c) override {
        const Compressor* cp = static_cast<bool>(comp_.decompressFn) ? &comp_ : nullptr;
        return disk::readChunk(dir_, manifest_, c, cp);
    }

    // --- Host conveniences: build a MapView from the manifest with no JSON of the host's own. ---
    const Manifest& manifest() const { return manifest_; }
    const std::vector<FieldDecl>& schema() const { return manifest_.fields; }
    GridSpec gridSpec() const {
        const auto& cd = manifest_.coordinate.chunkDims;   // {W,H,D} cells per chunk
        const auto& cs = manifest_.coordinate.cellSize;    // {x,y} world units per cell
        return GridSpec{cd[0], cd[1], cd[2], cs[0], cs[1]};
    }

private:
    std::string dir_;      // the world-document directory (holds manifest.json + <chunksDir>/)
    Compressor  comp_;     // optional per-chunk (de)compressor — set iff the document is compressed
    Manifest    manifest_; // read once at construction; backs has()/load() + the schema/grid accessors
};

} // namespace mapview
} // namespace grove
