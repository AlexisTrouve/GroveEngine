#pragma once

/**
 * grove::mapview::ChunkProvider — the provider axis ③ (S1b).
 *
 * WHAT  : IChunkProvider is the source of chunk data, injected by the host. mapview defines the interface;
 *         the host implements the actual load (from disk, from a generator, over the network — its choice).
 *
 * WHY   : This is what makes infinity free and the core pure. A finite world (Theomen) returns chunks inside
 *         its bounds and reports "absent" outside; an infinite world GENERATES chunks on demand — same
 *         interface, and the viewer never knows which. Keeping I/O behind this interface keeps the MapView
 *         core a pure compute object (headless-testable with a mock).
 *
 * HOW   : Header-only, std-only. `has` answers availability at the source (cheap — read a header / check
 *         bounds); `load` returns the decoded ChunkData by value (precondition: has(c) is true). A chunk the
 *         source does not have is ABSENT (the layer simply does not draw it) — never a zeroed placeholder.
 */

#include "grove/mapview/Coord.h"
#include "grove/mapview/WorldDocument.h"

namespace grove {
namespace mapview {

struct IChunkProvider {
    virtual ~IChunkProvider() = default;

    // Is this chunk available at the source? (Cheap; no full decode.)
    virtual bool has(ChunkCoord c) const = 0;
    // Load and decode the chunk. Precondition: has(c) == true.
    virtual ChunkData load(ChunkCoord c) = 0;
};

} // namespace mapview
} // namespace grove
