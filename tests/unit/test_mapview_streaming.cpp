/**
 * Unit Tests: grove::mapview streaming — cull + LRU chunk cache (map-viewer engine, slice S1b).
 *
 * WHAT  : Locks step 1–3 of the per-frame pipeline (mapview.md §4): chunksInViewport (cull a world rect to the
 *         intersecting chunk coords) and ChunkCache (load visible via an injected provider, evict LRU under a
 *         budget). Verifies the two cache invariants that matter: never reload a resident chunk, and never
 *         evict a currently-visible one.
 *
 * WHY    : Cull is what bounds cost to screen×zoom (not world size); the cache is what keeps an infinite world
 *         within bounded RAM. Both are pure given an injected provider, so a mock proves them headless.
 *
 * HOW    : Catch2 + a MockProvider that counts load() calls (so "no reload" / "evicted then reloaded" are
 *         observable facts, not assumptions).
 */

#include <unordered_set>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "grove/mapview/ChunkCache.h"
#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Cull.h"

using namespace grove::mapview;

// Set membership helper for the cull assertions.
static bool contains(const std::vector<ChunkCoord>& v, ChunkCoord c) {
    for (const auto& x : v) if (x == c) return true;
    return false;
}

// A provider over a fixed set of available chunks; counts how many times load() actually ran.
struct MockProvider final : IChunkProvider {
    std::unordered_set<ChunkCoord, ChunkCoordHash> available;
    int loadCalls{0};

    bool has(ChunkCoord c) const override { return available.find(c) != available.end(); }
    ChunkData load(ChunkCoord c) override {
        ++loadCalls;
        ChunkData d;
        d.coord = c;
        d.cellCount = 1;
        d.fields.emplace_back("v", std::vector<uint32_t>{static_cast<uint32_t>(c.x * 1000 + c.y)});
        return d;
    }
};

TEST_CASE("mapview S1b - cull returns exactly the chunks intersecting the viewport", "[mapview][streaming][unit]") {
    // cell 1×1, chunk 4×4 -> each chunk spans 4 world units. Viewport [0,0]-[5,5] touches chunks (0,0) and (1,*).
    const auto v = chunksInViewport(0.0, 0.0, 5.0, 5.0, 1.0, 1.0, 4, 4, 0);
    REQUIRE(v.size() == 4);
    REQUIRE(contains(v, ChunkCoord{0, 0, 0}));
    REQUIRE(contains(v, ChunkCoord{1, 0, 0}));
    REQUIRE(contains(v, ChunkCoord{0, 1, 0}));
    REQUIRE(contains(v, ChunkCoord{1, 1, 0}));

    // A viewport fully inside one chunk yields exactly that chunk.
    const auto one = chunksInViewport(0.5, 0.5, 3.5, 3.5, 1.0, 1.0, 4, 4, 0);
    REQUIRE(one.size() == 1);
    REQUIRE(one[0] == ChunkCoord{0, 0, 0});
}

TEST_CASE("mapview S1b - cull handles negative coordinates and the z layer", "[mapview][streaming][unit]") {
    const auto v = chunksInViewport(-5.0, -1.0, -1.0, -0.5, 1.0, 1.0, 4, 4, 7);
    // x in [-5,-1] -> chunk x floor(-5/4)=-2 .. floor(-1/4)=-1 ; y in [-1,-0.5] -> floor(-1/4)=-1
    REQUIRE(contains(v, ChunkCoord{-2, -1, 7}));
    REQUIRE(contains(v, ChunkCoord{-1, -1, 7}));
    for (const auto& c : v) REQUIRE(c.z == 7);
}

TEST_CASE("mapview S1b - cache loads visible chunks once and skips absent ones", "[mapview][streaming][unit]") {
    MockProvider provider;
    provider.available = {ChunkCoord{0, 0, 0}, ChunkCoord{1, 0, 0}};  // (2,0) deliberately NOT available
    ChunkCache cache(provider, 8);

    cache.ensureResident({ChunkCoord{0, 0, 0}, ChunkCoord{1, 0, 0}, ChunkCoord{2, 0, 0}});
    REQUIRE(cache.residentCount() == 2);                 // (2,0) absent -> not resident
    REQUIRE(provider.loadCalls == 2);
    REQUIRE(cache.get(ChunkCoord{0, 0, 0}) != nullptr);
    REQUIRE(cache.get(ChunkCoord{2, 0, 0}) == nullptr);  // absent -> null, not a zeroed chunk

    // Re-requesting resident chunks must NOT reload them.
    cache.ensureResident({ChunkCoord{0, 0, 0}, ChunkCoord{1, 0, 0}});
    REQUIRE(provider.loadCalls == 2);
}

TEST_CASE("mapview S1b - cache evicts the LRU non-visible chunk past budget", "[mapview][streaming][unit]") {
    MockProvider provider;
    for (int x = 0; x < 5; ++x) provider.available.insert(ChunkCoord{x, 0, 0});
    ChunkCache cache(provider, 2);

    cache.ensureResident({ChunkCoord{0, 0, 0}});         // resident {0}
    cache.ensureResident({ChunkCoord{1, 0, 0}});         // resident {0,1} (at budget)
    cache.ensureResident({ChunkCoord{2, 0, 0}});         // load 2 -> over budget -> evict LRU back = 0
    REQUIRE(cache.residentCount() == 2);
    REQUIRE(cache.isResident(ChunkCoord{0, 0, 0}) == false);   // evicted
    REQUIRE(cache.isResident(ChunkCoord{2, 0, 0}) == true);
    REQUIRE(provider.loadCalls == 3);

    // Coming back to the evicted chunk reloads it (proving it was truly gone).
    cache.ensureResident({ChunkCoord{0, 0, 0}});
    REQUIRE(provider.loadCalls == 4);
}

TEST_CASE("mapview S1b - cache never evicts a currently-visible chunk", "[mapview][streaming][unit]") {
    MockProvider provider;
    provider.available = {ChunkCoord{0, 0, 0}, ChunkCoord{1, 0, 0}};
    ChunkCache cache(provider, 1);                        // budget 1, but 2 visible

    cache.ensureResident({ChunkCoord{0, 0, 0}, ChunkCoord{1, 0, 0}});
    // Both are visible, so the budget is deliberately exceeded rather than evict a visible chunk.
    REQUIRE(cache.residentCount() == 2);
    REQUIRE(cache.get(ChunkCoord{0, 0, 0}) != nullptr);
    REQUIRE(cache.get(ChunkCoord{1, 0, 0}) != nullptr);
}
