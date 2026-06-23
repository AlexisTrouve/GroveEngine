/**
 * Unit tests for grove::assets::AssetManager (phase 1 — pure logic). Drives it through a MockTextureProvider
 * so the registry + on-demand load + preload + priority/LRU eviction under a VRAM budget are verified with no
 * bgfx. Each texture is a fixed 100 "bytes" unless said otherwise.
 */

#include <catch2/catch_test_macros.hpp>
#include "Assets/AssetManager.h"
#include <unordered_map>
#include <string>

using namespace grove::assets;

namespace {
// Fake GPU: hands out incrementing ids, remembers each id's size, counts loads/unloads.
struct MockProvider : ITextureProvider {
    uint32_t nextId = 1;
    int loads = 0, unloads = 0;
    uint64_t sizeForNext = 100;
    std::unordered_map<uint32_t, uint64_t> sizes;
    uint32_t load(const std::string& path) override { (void)path; ++loads; uint32_t id = nextId++; sizes[id] = sizeForNext; return id; }
    void     unload(uint32_t texId) override { ++unloads; sizes.erase(texId); }
    uint64_t bytes(uint32_t texId) const override { auto it = sizes.find(texId); return it != sizes.end() ? it->second : 0; }
};
} // namespace

TEST_CASE("AssetManager: unknown id resolves to 0, no load", "[assets][unit]") {
    MockProvider gpu; AssetManager am(&gpu, 1000);
    REQUIRE(am.resolve("nope") == 0);
    REQUIRE(gpu.loads == 0);
}

TEST_CASE("AssetManager: on-demand load + cache hit", "[assets][unit]") {
    MockProvider gpu; AssetManager am(&gpu, 1000);
    am.registerAsset("iron", "iron.png");
    const uint32_t t1 = am.resolve("iron");
    REQUIRE(t1 != 0);
    REQUIRE(gpu.loads == 1);
    REQUIRE(am.isResident("iron"));
    REQUIRE(am.residentBytes() == 100);
    // Second resolve = cache hit, no extra load, same handle.
    REQUIRE(am.resolve("iron") == t1);
    REQUIRE(gpu.loads == 1);
}

TEST_CASE("AssetManager: evicts to stay under the VRAM budget", "[assets][unit]") {
    MockProvider gpu; AssetManager am(&gpu, 250);   // fits 2 of 100, not 3
    for (const char* id : {"a","b","c"}) am.registerAsset(id, std::string(id)+".png");
    am.resolve("a"); am.resolve("b"); am.resolve("c");
    REQUIRE(am.residentCount() == 2);
    REQUIRE(am.residentBytes() <= 250);
    REQUIRE(am.isResident("c"));          // the just-loaded one is never the eviction victim
    REQUIRE(gpu.unloads == 1);
}

TEST_CASE("AssetManager: priority protects high-priority assets from eviction", "[assets][unit]") {
    MockProvider gpu; AssetManager am(&gpu, 250);
    am.registerAsset("keep", "keep.png", /*priority*/ 10);
    am.registerAsset("low",  "low.png",  /*priority*/ 0);
    am.registerAsset("new",  "new.png",  /*priority*/ 0);
    am.resolve("keep"); am.resolve("low"); am.resolve("new");   // over budget -> evict lowest priority
    REQUIRE(am.isResident("keep"));       // high priority survives
    REQUIRE(am.isResident("new"));
    REQUIRE_FALSE(am.isResident("low"));  // low priority got evicted
}

TEST_CASE("AssetManager: LRU among equal priority", "[assets][unit]") {
    MockProvider gpu; AssetManager am(&gpu, 250);
    for (const char* id : {"a","b","c"}) am.registerAsset(id, std::string(id)+".png");
    am.resolve("a"); am.resolve("b");
    am.resolve("a");                      // touch a -> b is now the oldest
    am.resolve("c");                      // over budget -> evict the oldest equal-priority (b)
    REQUIRE(am.isResident("a"));
    REQUIRE(am.isResident("c"));
    REQUIRE_FALSE(am.isResident("b"));
}

TEST_CASE("AssetManager: setPriority is dynamic and affects eviction", "[assets][unit]") {
    MockProvider gpu; AssetManager am(&gpu, 250);
    for (const char* id : {"a","b","c"}) am.registerAsset(id, std::string(id)+".png");
    am.resolve("a"); am.resolve("b");
    am.setPriority("a", 100);             // bump a after it's resident
    am.resolve("c");                      // over budget -> b (now lowest priority) is evicted, not a
    REQUIRE(am.isResident("a"));
    REQUIRE_FALSE(am.isResident("b"));
}

TEST_CASE("AssetManager: preloadGroup loads a whole group", "[assets][unit]") {
    MockProvider gpu; AssetManager am(&gpu, 100000);
    am.registerAsset("ui1", "ui1.png", 0, "ui");
    am.registerAsset("ui2", "ui2.png", 0, "ui");
    am.registerAsset("world1", "w1.png", 0, "world");
    am.preloadGroup("ui");
    REQUIRE(am.isResident("ui1"));
    REQUIRE(am.isResident("ui2"));
    REQUIRE_FALSE(am.isResident("world1"));   // other groups untouched
    REQUIRE(gpu.loads == 2);
}

TEST_CASE("AssetManager: unload frees a resident asset", "[assets][unit]") {
    MockProvider gpu; AssetManager am(&gpu, 1000);
    am.registerAsset("x", "x.png");
    am.resolve("x");
    REQUIRE(am.isResident("x"));
    am.unload("x");
    REQUIRE_FALSE(am.isResident("x"));
    REQUIRE(am.residentBytes() == 0);
    REQUIRE(gpu.unloads == 1);
}
