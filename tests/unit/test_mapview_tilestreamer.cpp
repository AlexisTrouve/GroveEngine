/**
 * Unit Tests: grove::mapview::TileChunkStreamer — the retained add/remove diff for live tiling.
 *
 * WHAT  : Locks the enter/leave bookkeeping that lets the interactive viewer draw tiles as the camera pans:
 *         only newly-visible chunks are added, only just-departed chunks are removed, still-visible chunks are
 *         left untouched (no re-upload), and toggling tiling off flushes everything. This is the hard part of
 *         "live tiling" and it is pure logic, so it is proven headless here — the viewer just publishes the
 *         deltas this brick returns.
 *
 * WHY    : A retained tilemap keeps chunks until told to remove them; get the diff wrong and chunks either
 *         vanish while still on screen (removed too eagerly) or pile up forever (never removed) — a leak that
 *         a static capture would never reveal. The per-frame delta must be exactly {entered} / {left}.
 *
 * HOW    : Catch2. Build small TileChunkDraw sets by chunk coord and feed successive frames to sync(),
 *         asserting the add/remove deltas and the resident set. chunkId() is the stable key add/remove share.
 */

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "grove/mapview/CellDraw.h"
#include "grove/mapview/TileChunkStreamer.h"

using namespace grove::mapview;

// A TileChunkDraw is fully identified (for streaming) by its chunk coord; the rest doesn't affect the diff.
static TileChunkDraw chunk(int cx, int cy) {
    TileChunkDraw tc;
    tc.chunkX = cx;
    tc.chunkY = cy;
    tc.width = 1; tc.height = 1;
    tc.tiles = {1};
    return tc;
}

TEST_CASE("mapview tilestream - first frame adds every visible chunk", "[mapview][tile][unit]") {
    TileChunkStreamer s;
    const auto d = s.sync({chunk(0, 0), chunk(1, 0), chunk(0, 1)});
    REQUIRE(d.added.size() == 3);
    REQUIRE(d.removed.empty());
    REQUIRE(s.residentCount() == 3);
    REQUIRE(s.isResident(TileChunkStreamer::chunkId(1, 0)));
}

TEST_CASE("mapview tilestream - a stable set re-synced adds/removes nothing", "[mapview][tile][unit]") {
    TileChunkStreamer s;
    s.sync({chunk(0, 0), chunk(1, 0)});
    const auto d = s.sync({chunk(1, 0), chunk(0, 0)});  // same set, different order
    REQUIRE(d.added.empty());                            // still-visible chunks are NOT re-uploaded
    REQUIRE(d.removed.empty());
    REQUIRE(s.residentCount() == 2);
}

TEST_CASE("mapview tilestream - a pan adds the entered chunk and removes the departed one", "[mapview][tile][unit]") {
    TileChunkStreamer s;
    s.sync({chunk(0, 0), chunk(1, 0)});          // resident: col 0 and col 1
    const auto d = s.sync({chunk(1, 0), chunk(2, 0)});  // panned east one chunk: 0 leaves, 2 enters
    REQUIRE(d.added.size() == 1);
    REQUIRE(d.added[0].chunkX == 2);
    REQUIRE(d.removed.size() == 1);
    REQUIRE(d.removed[0] == TileChunkStreamer::chunkId(0, 0));
    REQUIRE(s.residentCount() == 2);
    REQUIRE(s.isResident(TileChunkStreamer::chunkId(2, 0)));
    REQUIRE_FALSE(s.isResident(TileChunkStreamer::chunkId(0, 0)));
}

TEST_CASE("mapview tilestream - syncing an empty set flushes everything as removes", "[mapview][tile][unit]") {
    TileChunkStreamer s;
    s.sync({chunk(0, 0), chunk(1, 0), chunk(2, 0)});
    const auto d = s.sync({});                    // tiling toggled off / nothing visible
    REQUIRE(d.added.empty());
    REQUIRE(d.removed.size() == 3);               // every resident chunk comes back to be removed
    REQUIRE(s.residentCount() == 0);
}

TEST_CASE("mapview tilestream - chunkId is stable and collision-free over a coord range", "[mapview][tile][unit]") {
    // Add/remove match only if the id is a pure function of the coord; distinct coords must not collide.
    REQUIRE(TileChunkStreamer::chunkId(3, 7) == TileChunkStreamer::chunkId(3, 7));
    REQUIRE(TileChunkStreamer::chunkId(3, 7) != TileChunkStreamer::chunkId(7, 3));
    REQUIRE(TileChunkStreamer::chunkId(-1, 0) != TileChunkStreamer::chunkId(0, -1));
    REQUIRE(TileChunkStreamer::chunkId(0, 0) != 0);  // never 0 (a common "no id" sentinel)
}
