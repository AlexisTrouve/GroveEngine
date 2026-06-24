/**
 * Unit tests for the ASYNC load path of grove::assets::AssetManager (phase 3 — pure logic). Decode happens
 * off-thread (the slow part), upload on the render thread. Here both the provider AND the decoder are MOCKS
 * driven by hand, so the async state machine (placeholder while loading, no duplicate requests, upload on
 * pump, failure latch) is verified deterministically with NO threads and NO bgfx.
 */

#include <catch2/catch_test_macros.hpp>
#include "Assets/AssetManager.h"
#include <unordered_map>
#include <string>
#include <vector>

using namespace grove::assets;

namespace {

// Fake GPU: same as the sync tests, plus upload() (the render-thread side of async load).
struct MockProvider : ITextureProvider {
    uint32_t nextId = 1;
    int loads = 0, unloads = 0, uploads = 0;
    std::unordered_map<uint32_t, uint64_t> sizes;
    uint32_t load(const std::string& path) override { (void)path; ++loads; uint32_t id = nextId++; sizes[id] = 100; return id; }
    void     unload(uint32_t texId) override { ++unloads; sizes.erase(texId); }
    uint64_t bytes(uint32_t texId) const override { auto it = sizes.find(texId); return it != sizes.end() ? it->second : 0; }
    uint32_t upload(const uint8_t* rgba, int w, int h) override {
        (void)rgba; ++uploads; uint32_t id = nextId++; sizes[id] = static_cast<uint64_t>(w) * h * 4; return id;
    }
};

// Deterministic decoder: request() just records the job; the test "finishes" a decode by hand with complete(),
// staging a DecodedImage that the next poll() drains. No thread, no timing — fully under the test's control.
struct MockDecoder : IAsyncDecoder {
    struct Job { std::string id, path; };
    std::vector<Job> requested;       // every request ever made (to assert request count)
    std::vector<DecodedImage> ready;  // staged results, drained by poll()
    int pendingCount = 0;

    void request(const std::string& id, const std::string& path) override { requested.push_back({id, path}); ++pendingCount; }
    void poll(std::vector<DecodedImage>& out) override { for (auto& d : ready) out.push_back(std::move(d)); ready.clear(); }
    size_t pending() const override { return static_cast<size_t>(pendingCount); }

    // Test helper: stage a finished decode for `id`. ok=false simulates a decode failure (missing/corrupt file).
    void complete(const std::string& id, bool ok, int w = 8, int h = 8) {
        DecodedImage d; d.id = id; d.ok = ok; d.w = w; d.h = h;
        if (ok) d.pixels.assign(static_cast<size_t>(w) * h * 4, 255);
        ready.push_back(std::move(d));
        if (pendingCount > 0) --pendingCount;
    }
};

} // namespace

TEST_CASE("AssetManager async: resolve returns placeholder + enqueues one decode, not resident yet", "[assets][unit][async]") {
    MockProvider gpu; MockDecoder dec;
    AssetManager am(&gpu, 100000);
    am.setAsyncDecoder(&dec);
    am.setPlaceholder(42);
    am.registerAsset("iron", "iron.png");

    // First touch: nothing resident -> kick an async decode, hand back the placeholder, NOT a real load.
    REQUIRE(am.resolve("iron") == 42);
    REQUIRE(dec.requested.size() == 1);
    REQUIRE(gpu.loads == 0);        // async path NEVER uses the synchronous load()
    REQUIRE(gpu.uploads == 0);      // upload only happens on pump
    REQUIRE_FALSE(am.isResident("iron"));
}

TEST_CASE("AssetManager async: repeated resolve while decoding does NOT enqueue duplicate requests", "[assets][unit][async]") {
    MockProvider gpu; MockDecoder dec;
    AssetManager am(&gpu, 100000);
    am.setAsyncDecoder(&dec);
    am.registerAsset("iron", "iron.png");

    am.resolve("iron");
    am.resolve("iron");
    am.resolve("iron");
    REQUIRE(dec.requested.size() == 1);   // one decode in flight, not three
}

TEST_CASE("AssetManager async: pump uploads a finished decode -> resident, resolve returns the real id", "[assets][unit][async]") {
    MockProvider gpu; MockDecoder dec;
    AssetManager am(&gpu, 100000);
    am.setAsyncDecoder(&dec);
    am.setPlaceholder(42);
    am.registerAsset("iron", "iron.png");

    REQUIRE(am.resolve("iron") == 42);    // placeholder while decoding

    dec.complete("iron", /*ok*/ true, 16, 16);   // decode finished off-thread
    am.pumpAsync();                                // render thread: drain + upload

    REQUIRE(gpu.uploads == 1);
    REQUIRE(am.isResident("iron"));
    REQUIRE(am.residentBytes() == 16ull * 16ull * 4ull);   // bytes come from the decoded dimensions
    const uint32_t tex = am.resolve("iron");
    REQUIRE(tex != 0);
    REQUIRE(tex != 42);                            // now the real texture, not the placeholder
    REQUIRE(dec.requested.size() == 1);            // resolving the resident asset enqueues nothing more
}

TEST_CASE("AssetManager async: a failed decode is latched -> no infinite re-request", "[assets][unit][async]") {
    MockProvider gpu; MockDecoder dec;
    AssetManager am(&gpu, 100000);
    am.setAsyncDecoder(&dec);
    am.registerAsset("broken", "broken.png");

    am.resolve("broken");
    dec.complete("broken", /*ok*/ false);   // decode failed (missing/corrupt)
    am.pumpAsync();

    REQUIRE_FALSE(am.isResident("broken"));
    REQUIRE(gpu.uploads == 0);
    // A later resolve must NOT re-enqueue the broken asset every frame (would be a hot decode loop).
    am.resolve("broken");
    am.resolve("broken");
    REQUIRE(dec.requested.size() == 1);
}

TEST_CASE("AssetManager async: pump without a finished decode is a no-op", "[assets][unit][async]") {
    MockProvider gpu; MockDecoder dec;
    AssetManager am(&gpu, 100000);
    am.setAsyncDecoder(&dec);
    am.registerAsset("iron", "iron.png");

    am.resolve("iron");
    am.pumpAsync();                  // nothing finished -> nothing uploaded
    REQUIRE(gpu.uploads == 0);
    REQUIRE_FALSE(am.isResident("iron"));
}

TEST_CASE("AssetManager async: pumpAsync is inert when no decoder is set (sync mode unchanged)", "[assets][unit][async]") {
    MockProvider gpu;
    AssetManager am(&gpu, 100000);   // no setAsyncDecoder -> classic synchronous behaviour
    am.registerAsset("iron", "iron.png");

    const uint32_t tex = am.resolve("iron");
    REQUIRE(tex != 0);
    REQUIRE(gpu.loads == 1);         // sync load(), not async
    REQUIRE(am.isResident("iron"));
    am.pumpAsync();                  // safe no-op with no decoder
    REQUIRE(gpu.uploads == 0);
}
