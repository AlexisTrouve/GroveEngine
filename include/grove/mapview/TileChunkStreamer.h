#pragma once

/**
 * grove::mapview::TileChunkStreamer — diff successive visible tile-chunk sets into retained add/remove deltas.
 *
 * WHAT  : A host driving the tiling path culls a NEW set of visible TileChunkDraws each frame (from
 *         MapView::tileChunks()). A retained tilemap keeps a chunk uploaded until told to remove it, so the
 *         host must publish render:tilemap:add only for newly-visible chunks and render:tilemap:remove only
 *         for chunks that just left the viewport — chunks still visible stay uploaded untouched (that is the
 *         whole point of the retained fast lane: upload once, draw for free until it scrolls away).
 *
 * WHY   : This enter/leave bookkeeping is the ACTUAL hard part of "live tiling in the viewer" (a static
 *         capture just adds everything once). It is also PURE logic — no GPU, no IIO — so it belongs in its
 *         own brick that unit-tests headless, leaving the viewer to just publish the deltas the brick returns.
 *
 * HOW   : Header-only, std-only. Tracks the set of resident chunk keys (a stable id per chunk). sync(visible)
 *         returns {added: the TileChunkDraws to upload, removed: the ids to drop} and updates the tracked set.
 *         The id is derived from the chunk coord by a fixed packing (chunk coords in [-500,499] — plenty for a
 *         viewport's worth of chunks); the host uses the SAME id when it publishes add/remove, so removes match.
 */

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "grove/mapview/CellDraw.h"  // TileChunkDraw

namespace grove {
namespace mapview {

class TileChunkStreamer {
public:
    // The per-frame delta: chunks to upload (add) and resident ids to drop (remove).
    struct Delta {
        std::vector<TileChunkDraw> added;    // publish render:tilemap:add for each
        std::vector<int>           removed;  // publish render:tilemap:remove {id} for each
    };

    // Stable, collision-free id for a chunk coord (offset-packed; chunk coords in [-500,499]). The host must
    // publish add/remove with this exact id so a later remove matches the earlier add.
    static int chunkId(int chunkX, int chunkY) { return (chunkX + 500) * 1000 + (chunkY + 500) + 1; }

    // Diff `visible` against the resident set: return the add/remove delta and adopt `visible` as the new set.
    Delta sync(const std::vector<TileChunkDraw>& visible) {
        Delta d;
        std::unordered_set<int> nowIds;
        nowIds.reserve(visible.size() * 2);
        for (const TileChunkDraw& tc : visible) {
            const int id = chunkId(tc.chunkX, tc.chunkY);
            nowIds.insert(id);
            if (resident_.find(id) == resident_.end()) d.added.push_back(tc);  // newly visible -> upload
        }
        for (int id : resident_) {
            if (nowIds.find(id) == nowIds.end()) d.removed.push_back(id);      // left the viewport -> drop
        }
        resident_.swap(nowIds);  // the resident set is exactly what is visible now
        return d;
    }

    // Forget everything WITHOUT emitting removes (host has already torn down / is resetting). To flush WITH
    // removes, call sync({}) — every resident id comes back in Delta.removed.
    void clear() { resident_.clear(); }

    size_t residentCount() const { return resident_.size(); }
    bool   isResident(int id) const { return resident_.find(id) != resident_.end(); }

private:
    std::unordered_set<int> resident_;  // chunk ids currently uploaded to the retained tilemap
};

} // namespace mapview
} // namespace grove
