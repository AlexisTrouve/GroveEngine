/**
 * Integration Tests: SceneCollector
 *
 * Comprehensive tests for scene collection from IIO messages including:
 * - All message types (sprite, tilemap, text, particle, camera, clear, debug)
 * - FramePacket construction with FrameAllocator
 * - String/array data copying
 * - Multiple frame cycles
 *
 * Uses real IntraIO for message routing
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../../modules/BgfxRenderer/Scene/SceneCollector.h"
#include "../../modules/BgfxRenderer/Scene/Camera.h"
#include "../../modules/BgfxRenderer/Frame/FrameAllocator.h"
#include "grove/IntraIO.h"
#include "grove/IntraIOManager.h"
#include "grove/JsonDataNode.h"
#include <nlohmann/json.hpp>

#include <memory>
#include <chrono>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <vector>

using namespace grove;
using Catch::Matchers::WithinAbs;

// Helper to create unique instance IDs per test
inline std::string uniqueId(const std::string& prefix) {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << prefix << "_" << now;
    return oss.str();
}

// ============================================================================
// Retained-mode sprites (render:sprite:add / :update / :remove) — the persistent,
// renderId-keyed path. Previously had ZERO test coverage; this validates the contract.
// ============================================================================

namespace {
// Minimal harness: a publisher + a collector wired through a real IntraIO instance.
struct RetainedFixture {
    std::shared_ptr<IntraIO> ioCollector;
    std::shared_ptr<IntraIO> ioPublisher;
    SceneCollector collector;
    RetainedFixture() {
        auto& mgr = IntraIOManager::getInstance();
        ioCollector = mgr.createInstance(uniqueId("rcv"));
        ioPublisher = mgr.createInstance(uniqueId("pub"));
        collector.setup(ioCollector.get());
    }
    void pump() { collector.collect(ioCollector.get(), 0.016f); }
};
} // namespace

TEST_CASE("SceneCollector - retained sprite: add then PERSISTS across frames", "[scene_collector][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto add = std::make_unique<JsonDataNode>("s");
    add->setInt("renderId", 7);
    add->setDouble("cx", 10.0);
    add->setDouble("cy", 20.0);
    fx.ioPublisher->publish("render:sprite:add", std::move(add));
    fx.pump();

    // Frame 1: sprite present.
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.spriteCount == 1);
        REQUIRE_THAT(p.sprites[0].x, WithinAbs(10.0f, 0.01f));
    }

    // Frame boundary: ephemeral vectors are cleared, retained must survive.
    fx.collector.clear();

    // Frame 2: WITHOUT re-publishing, the retained sprite must STILL be there.
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.spriteCount == 1);                       // persistence — the whole point
        REQUIRE_THAT(p.sprites[0].x, WithinAbs(10.0f, 0.01f));
    }
}

TEST_CASE("SceneCollector - retained sprite: update preserves unspecified fields", "[scene_collector][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Add a RED sprite (0xFF0000FF = r=1,g=0,b=0,a=1).
    auto add = std::make_unique<JsonDataNode>("s");
    add->setInt("renderId", 3);
    add->setDouble("cx", 10.0);
    add->setInt("color", static_cast<int>(0xFF0000FF));
    fx.ioPublisher->publish("render:sprite:add", std::move(add));
    fx.pump();

    // Update ONLY x — no color field. Color must be PRESERVED (red), like x/y/scale are.
    auto upd = std::make_unique<JsonDataNode>("s");
    upd->setInt("renderId", 3);
    upd->setDouble("cx", 20.0);
    fx.ioPublisher->publish("render:sprite:update", std::move(upd));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.spriteCount == 1);
    const auto& s = p.sprites[0];
    REQUIRE_THAT(s.x, WithinAbs(20.0f, 0.01f));   // updated
    REQUIRE_THAT(s.r, WithinAbs(1.0f, 0.01f));    // red preserved
    REQUIRE_THAT(s.g, WithinAbs(0.0f, 0.01f));    // red preserved (bug reset this to white)
    REQUIRE_THAT(s.b, WithinAbs(0.0f, 0.01f));    // red preserved
}

TEST_CASE("SceneCollector - retained sprite: remove deletes it", "[scene_collector][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto add = std::make_unique<JsonDataNode>("s");
    add->setInt("renderId", 5);
    add->setDouble("cx", 10.0);
    fx.ioPublisher->publish("render:sprite:add", std::move(add));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).spriteCount == 1);

    fx.collector.clear();
    auto rem = std::make_unique<JsonDataNode>("s");
    rem->setInt("renderId", 5);
    fx.ioPublisher->publish("render:sprite:remove", std::move(rem));
    fx.pump();

    REQUIRE(fx.collector.finalize(allocator).spriteCount == 0);  // gone
}

// ============================================================================
// render:sprite:batch — the bulk path. Accepts a FLAT float blob ("spriteData",
// stride 8, the perf path added by c5c9e4d) with a child-node fallback. Publishing
// THROUGH a real IntraIO also guards the b846225 IO changes (compiled topic matcher
// + toFullJson re-home): a batch message must arrive intact, non-UTF8 blob and all.
// ============================================================================
TEST_CASE("SceneCollector - sprite batch: flat float blob parses N sprites with colors", "[scene_collector][batch]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Pack 3 sprites into a flat blob, stride 8: x, y, scaleX, scaleY, rotation, textureId, layer, colorBits(uint32).
    auto pack = [](std::vector<float>& out, float x, float y, uint32_t rgba) {
        out.push_back(x);    out.push_back(y);
        out.push_back(1.0f); out.push_back(1.0f);   // scaleX, scaleY
        out.push_back(0.0f);                         // rotation
        out.push_back(0.0f);                         // textureId (0 = white/solid)
        out.push_back(5.0f);                         // layer
        float bits; std::memcpy(&bits, &rgba, sizeof(float)); out.push_back(bits);  // color BITS, not a numeric cast
    };
    std::vector<float> f;
    pack(f, 10.0f, 20.0f, 0xFF0000FFu);  // red
    pack(f, 30.0f, 40.0f, 0x00FF00FFu);  // green
    pack(f, 50.0f, 60.0f, 0x0000FFFFu);  // blue
    const std::string blob(reinterpret_cast<const char*>(f.data()), f.size() * sizeof(float));

    auto batch = std::make_unique<JsonDataNode>("b");
    batch->setString("spriteData", blob);
    fx.ioPublisher->publish("render:sprite:batch", std::move(batch));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.spriteCount == 3);                            // all three survived publish -> route -> parse
    // Order is preserved (the parser appends in blob order).
    REQUIRE_THAT(p.sprites[0].x, WithinAbs(10.0f, 0.01f));
    REQUIRE_THAT(p.sprites[0].y, WithinAbs(20.0f, 0.01f));
    REQUIRE_THAT(p.sprites[0].r, WithinAbs(1.0f, 0.01f));   // red
    REQUIRE_THAT(p.sprites[0].g, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(p.sprites[1].x, WithinAbs(30.0f, 0.01f));
    REQUIRE_THAT(p.sprites[1].g, WithinAbs(1.0f, 0.01f));   // green
    REQUIRE_THAT(p.sprites[2].x, WithinAbs(50.0f, 0.01f));
    REQUIRE_THAT(p.sprites[2].b, WithinAbs(1.0f, 0.01f));   // blue
}

TEST_CASE("SceneCollector - sprite batch: FIRST-CLASS blob (setBlob) delivers through IIO", "[scene_collector][batch]") {
    // The clean path: the batch rides as a first-class binary blob (setBlob), NOT a JSON string. Published
    // across two IntraIO instances, so this also proves the re-home (rehomed()) CARRIES the raw blob — the
    // whole reason the flat-blob hack got cleaned up. Same bytes/layout as the string case above.
    RetainedFixture fx;
    FrameAllocator allocator;

    auto pack = [](std::vector<float>& out, float x, float y, uint32_t rgba) {
        out.push_back(x);    out.push_back(y);
        out.push_back(1.0f); out.push_back(1.0f);
        out.push_back(0.0f); out.push_back(0.0f); out.push_back(5.0f);
        float bits; std::memcpy(&bits, &rgba, sizeof(float)); out.push_back(bits);
    };
    std::vector<float> f;
    pack(f, 11.0f, 22.0f, 0xFF0000FFu);  // red
    pack(f, 33.0f, 44.0f, 0x0000FFFFu);  // blue

    auto batch = std::make_unique<JsonDataNode>("b");
    batch->setBlob("spriteData", reinterpret_cast<const uint8_t*>(f.data()), f.size() * sizeof(float));
    fx.ioPublisher->publish("render:sprite:batch", std::move(batch));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.spriteCount == 2);                            // blob survived publish -> route -> re-home -> parse
    REQUIRE_THAT(p.sprites[0].x, WithinAbs(11.0f, 0.01f));
    REQUIRE_THAT(p.sprites[0].r, WithinAbs(1.0f, 0.01f));   // red
    REQUIRE_THAT(p.sprites[1].x, WithinAbs(33.0f, 0.01f));
    REQUIRE_THAT(p.sprites[1].b, WithinAbs(1.0f, 0.01f));   // blue
}

TEST_CASE("SceneCollector - retained tilemap: add persists + dirty cycle + update + remove (A4.1)", "[scene_collector][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Add a 2x2 chunk, id=9, tiles "1,2,3,4".
    auto add = std::make_unique<JsonDataNode>("tm");
    add->setInt("id", 9);
    add->setDouble("x", 50.0);
    add->setInt("width", 2);
    add->setInt("height", 2);
    add->setInt("tileW", 32);
    add->setInt("tileH", 32);
    add->setString("tileData", "1,2,3,4");
    fx.ioPublisher->publish("render:tilemap:add", std::move(add));
    fx.pump();

    // Frame 1: present, DIRTY (fresh add -> upload), tiles correct.
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.tilemapCount == 1);
        REQUIRE(p.tilemaps[0].id == 9u);
        REQUIRE(p.tilemaps[0].dirty == true);
        REQUIRE(p.tilemaps[0].width == 2);
        REQUIRE(p.tilemaps[0].tileCount == 4);
        REQUIRE(p.tilemaps[0].tiles != nullptr);
        REQUIRE(p.tilemaps[0].tiles[0] == 1);
        REQUIRE(p.tilemaps[0].tiles[3] == 4);
    }

    // Frame 2: persists WITHOUT re-publishing, and is now CLEAN (dirty cleared after frame 1).
    // This is the upload-once signal: a static retained chunk reports dirty=false.
    fx.collector.clear();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.tilemapCount == 1);            // persistence
        REQUIRE(p.tilemaps[0].dirty == false);   // no re-upload signalled
    }

    // Update: new tiles -> dirty again, grid replaced.
    fx.collector.clear();
    auto upd = std::make_unique<JsonDataNode>("tm");
    upd->setInt("id", 9);
    upd->setString("tileData", "5,6,7,8");
    fx.ioPublisher->publish("render:tilemap:update", std::move(upd));
    fx.pump();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.tilemapCount == 1);
        REQUIRE(p.tilemaps[0].dirty == true);    // update -> re-upload
        REQUIRE(p.tilemaps[0].tiles[0] == 5);
    }

    // Remove: gone.
    fx.collector.clear();
    auto rem = std::make_unique<JsonDataNode>("tm");
    rem->setInt("id", 9);
    fx.ioPublisher->publish("render:tilemap:remove", std::move(rem));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).tilemapCount == 0);
}

TEST_CASE("SceneCollector - render:tilemap:fog patches the fog sub-rect, sets fogDirty, leaves tiles clean", "[scene_collector][retained][fog]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Add a 4x4 chunk id=20, all tiles id 1, fog ALL HIDDEN (0).
    auto add = std::make_unique<JsonDataNode>("tm");
    add->setInt("id", 20); add->setInt("width", 4); add->setInt("height", 4);
    add->setInt("tileW", 1); add->setInt("tileH", 1);
    add->setString("tileData", "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1");
    add->setString("fogData",  "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
    fx.ioPublisher->publish("render:tilemap:add", std::move(add));
    fx.pump();
    { FramePacket p = fx.collector.finalize(allocator);
      REQUIRE(p.tilemapCount == 1);
      REQUIRE(p.tilemaps[0].fog != nullptr);
      REQUIRE(p.tilemaps[0].fog[5] == 0); }          // (x1,y1) hidden
    fx.collector.clear();

    // Reveal a 2x2 block at (1,1) via a FOG-ONLY update.
    auto fog = std::make_unique<JsonDataNode>("tm");
    fog->setInt("id", 20); fog->setInt("x", 1); fog->setInt("y", 1); fog->setInt("w", 2); fog->setInt("h", 2);
    fog->setString("fogData", "255,255,255,255");
    fx.ioPublisher->publish("render:tilemap:fog", std::move(fog));
    fx.pump();
    { FramePacket p = fx.collector.finalize(allocator);
      REQUIRE(p.tilemapCount == 1);
      const TilemapChunk& c = p.tilemaps[0];
      REQUIRE(c.fogDirty == true);                   // a fog-only update was signalled
      REQUIRE(c.dirty == false);                     // tiles were NOT re-dirtied
      REQUIRE(c.fogDirtyX == 1); REQUIRE(c.fogDirtyY == 1);
      REQUIRE(c.fogDirtyW == 2); REQUIRE(c.fogDirtyH == 2);
      REQUIRE(c.fog[5]  == 255);                      // (1,1) revealed   (gi = 1*4+1)
      REQUIRE(c.fog[6]  == 255);                      // (2,1) revealed
      REQUIRE(c.fog[0]  == 0);                        // (0,0) still hidden
      REQUIRE(c.tiles[0] == 1); }                     // tiles untouched
    fx.collector.clear();

    // Next frame: fogDirty cleared after consumption.
    REQUIRE(fx.collector.finalize(allocator).tilemaps[0].fogDirty == false);
}

TEST_CASE("SceneCollector - render:tilemap:add with layers[] builds a multi-layer chunk (Strategy A)", "[scene_collector][retained][multilayer]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // A 2x2 chunk id=30 with TWO layers: base (id 1, tileset 0) + overlay (one teal tile, tileset 7).
    auto add = std::make_unique<JsonDataNode>("tm", nlohmann::json{
        {"id", 30}, {"width", 2}, {"height", 2}, {"tileW", 1}, {"tileH", 1},
        {"layers", nlohmann::json::array({
            nlohmann::json{ {"tileData", "1,1,1,1"}, {"textureId", 0} },   // layer 0 = base
            nlohmann::json{ {"tileData", "0,3,0,0"}, {"textureId", 7} }    // layer 1 = overlay
        })}
    });
    fx.ioPublisher->publish("render:tilemap:add", std::move(add));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.tilemapCount == 1);
    const TilemapChunk& c = p.tilemaps[0];
    REQUIRE(c.layerCount == 2);
    REQUIRE(c.layers != nullptr);
    REQUIRE(c.layers[0].tiles[0] == 1);          // base grid
    REQUIRE(c.layers[0].textureId == 0);
    REQUIRE(c.layers[1].tiles[1] == 3);          // overlay's teal tile at index 1
    REQUIRE(c.layers[1].textureId == 7);
    // Layer 0 is also mirrored into the legacy single-tile path (LOD/partial/upload use it).
    REQUIRE(c.tiles != nullptr);
    REQUIRE(c.tiles[0] == 1);
    REQUIRE(c.textureId == 0);
}

TEST_CASE("SceneCollector - retained tilemap: partial update patches a sub-rect + dirty rect (A4.2)", "[scene_collector][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Add a 4x4 chunk, all id 1.
    auto add = std::make_unique<JsonDataNode>("tm");
    add->setInt("id", 1);
    add->setInt("width", 4);
    add->setInt("height", 4);
    add->setString("tileData", "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1");
    fx.ioPublisher->publish("render:tilemap:add", std::move(add));
    fx.pump();
    fx.collector.finalize(allocator);   // frame 1 (full upload)
    fx.collector.clear();

    // Partial update: a 2x2 block of id 2 at (1,1).
    auto upd = std::make_unique<JsonDataNode>("tm");
    upd->setInt("id", 1);
    upd->setInt("x", 1);
    upd->setInt("y", 1);
    upd->setInt("w", 2);
    upd->setInt("h", 2);
    upd->setString("tileData", "2,2,2,2");
    fx.ioPublisher->publish("render:tilemap:update", std::move(upd));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.tilemapCount == 1);
    const auto& c = p.tilemaps[0];
    REQUIRE(c.dirty == true);
    REQUIRE(c.dirtyX == 1);          // the dirty rect, not the whole grid
    REQUIRE(c.dirtyY == 1);
    REQUIRE(c.dirtyW == 2);
    REQUIRE(c.dirtyH == 2);
    // Grid patched inside the rect, untouched outside.
    REQUIRE(c.tiles[0] == 1);
    REQUIRE(c.tiles[1 * 4 + 1] == 2);
    REQUIRE(c.tiles[1 * 4 + 2] == 2);
    REQUIRE(c.tiles[2 * 4 + 2] == 2);
    REQUIRE(c.tiles[3 * 4 + 3] == 1);
}

TEST_CASE("SceneCollector - retained text: add + persist + update + remove", "[scene_collector][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Add: id=1, "Hello", red.
    auto add = std::make_unique<JsonDataNode>("t");
    add->setInt("renderId", 1);
    add->setDouble("x", 5.0);
    add->setString("text", "Hello");
    add->setInt("color", static_cast<int>(0xFF0000FFu));
    fx.ioPublisher->publish("render:text:add", std::move(add));
    fx.pump();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.textCount == 1);
        REQUIRE(p.texts[0].text != nullptr);
        REQUIRE(std::string(p.texts[0].text) == "Hello");
        REQUIRE_THAT(p.texts[0].x, WithinAbs(5.0f, 0.01f));
    }

    // Persist across a frame boundary (string must stay valid + correct).
    fx.collector.clear();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.textCount == 1);
        REQUIRE(std::string(p.texts[0].text) == "Hello");
    }

    // Update x only — text content and color must be preserved.
    auto upd = std::make_unique<JsonDataNode>("t");
    upd->setInt("renderId", 1);
    upd->setDouble("x", 50.0);
    fx.ioPublisher->publish("render:text:update", std::move(upd));
    fx.pump();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.textCount == 1);
        REQUIRE_THAT(p.texts[0].x, WithinAbs(50.0f, 0.01f));         // updated
        REQUIRE(std::string(p.texts[0].text) == "Hello");           // text preserved
        REQUIRE(p.texts[0].color == 0xFF0000FFu);                   // color preserved
    }

    // Remove.
    fx.collector.clear();
    auto rem = std::make_unique<JsonDataNode>("t");
    rem->setInt("renderId", 1);
    fx.ioPublisher->publish("render:text:remove", std::move(rem));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).textCount == 0);
}

TEST_CASE("SceneCollector - retained text carries align + bold (defaults + round-trip)", "[scene_collector][retained][text]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Default (no align/bold keys) -> align 0 (left) + bold 0, so pre-feature messages are unchanged.
    auto plain = std::make_unique<JsonDataNode>("t");
    plain->setInt("renderId", 1); plain->setString("text", "L");
    fx.ioPublisher->publish("render:text:add", std::move(plain));
    fx.pump();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.textCount == 1);
        REQUIRE(p.texts[0].align == 0);
        REQUIRE(p.texts[0].bold == 0);
    }

    // Explicit align=1 (center) + bold=true round-trips into the TextCommand. (Retained text persists across
    // clear(), so drop the first one — otherwise finalize would report both.)
    auto rem1 = std::make_unique<JsonDataNode>("t"); rem1->setInt("renderId", 1);
    fx.ioPublisher->publish("render:text:remove", std::move(rem1));
    fx.collector.clear();
    auto styled = std::make_unique<JsonDataNode>("t");
    styled->setInt("renderId", 2); styled->setString("text", "C");
    styled->setInt("align", 1); styled->setBool("bold", true);
    fx.ioPublisher->publish("render:text:add", std::move(styled));
    fx.pump();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.textCount == 1);
        REQUIRE(p.texts[0].align == 1);
        REQUIRE(p.texts[0].bold == 1);
    }

    // Update align to 2 (right) — carried through the retained update path.
    auto upd = std::make_unique<JsonDataNode>("t");
    upd->setInt("renderId", 2); upd->setInt("align", 2);
    fx.ioPublisher->publish("render:text:update", std::move(upd));
    fx.pump();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.textCount == 1);
        REQUIRE(p.texts[0].align == 2);
        REQUIRE(p.texts[0].bold == 1);   // bold preserved across the update
    }
}

// ============================================================================
// Sprite Parsing
// ============================================================================

TEST_CASE("SceneCollector - parse sprite all fields", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Create sprite message
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", 100.0);
    sprite->setDouble("cy", 200.0);
    sprite->setDouble("scaleX", 2.0);
    sprite->setDouble("scaleY", 3.0);
    sprite->setDouble("rotation", 1.57);
    sprite->setDouble("u0", 0.0);
    sprite->setDouble("v0", 0.0);
    sprite->setDouble("u1", 1.0);
    sprite->setDouble("v1", 1.0);
    sprite->setInt("color", 0xFF00FFAA);
    sprite->setInt("textureId", 42);
    sprite->setInt("layer", 10);

    ioPublisher->publish("render:sprite", std::move(sprite));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == 1);
    REQUIRE(packet.sprites != nullptr);

    const auto& s = packet.sprites[0];
    REQUIRE_THAT(s.x, WithinAbs(100.0f, 0.01f));
    REQUIRE_THAT(s.y, WithinAbs(200.0f, 0.01f));
    REQUIRE_THAT(s.scaleX, WithinAbs(2.0f, 0.01f));
    REQUIRE_THAT(s.scaleY, WithinAbs(3.0f, 0.01f));
    REQUIRE_THAT(s.rotation, WithinAbs(1.57f, 0.01f));
    REQUIRE_THAT(s.textureId, WithinAbs(42.0f, 0.01f));
    REQUIRE_THAT(s.layer, WithinAbs(10.0f, 0.01f));
}

// ============================================================================
// flipX / flipY on render:sprite — mirroring for paper-doll characters (a piece must face left or
// right depending on the walk direction). Implemented as a UV SWAP: the image mirrors INSIDE its
// quad, which is why it composes correctly with `rotation` (the picture is mirrored first, then the
// box turns) and needs neither a new instance field nor a shader change.
// ============================================================================

TEST_CASE("SceneCollector: flipX mirrors the sprite's U range", "[collector][sprite][flip]") {
    auto ioPublisher = IntraIOManager::getInstance().createInstance("flip_pub");
    auto ioCollector = IntraIOManager::getInstance().createInstance("flip_col");
    FrameAllocator allocator(1024 * 1024);
    SceneCollector collector;
    collector.setup(ioCollector.get());

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", 50.0); sprite->setDouble("cy", 60.0);
    sprite->setDouble("u0", 0.25); sprite->setDouble("u1", 0.75);   // an ATLAS sub-rect, not 0..1
    sprite->setDouble("v0", 0.10); sprite->setDouble("v1", 0.90);
    sprite->setBool("flipX", true);
    sprite->setInt("textureId", 7);
    ioPublisher->publish("render:sprite", std::move(sprite));

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == 1);
    const auto& s = packet.sprites[0];
    // U swapped WITHIN the sub-rect (a full-texture assumption would have written 1..0 and broken
    // every atlas sprite), V untouched, and the centre must not move.
    REQUIRE_THAT(s.u0, WithinAbs(0.75f, 0.001f));
    REQUIRE_THAT(s.u1, WithinAbs(0.25f, 0.001f));
    REQUIRE_THAT(s.v0, WithinAbs(0.10f, 0.001f));
    REQUIRE_THAT(s.v1, WithinAbs(0.90f, 0.001f));
    REQUIRE_THAT(s.x, WithinAbs(50.0f, 0.01f));
    REQUIRE_THAT(s.y, WithinAbs(60.0f, 0.01f));
}

TEST_CASE("SceneCollector: flipY mirrors V, and flips compose with rotation", "[collector][sprite][flip]") {
    auto ioPublisher = IntraIOManager::getInstance().createInstance("flip2_pub");
    auto ioCollector = IntraIOManager::getInstance().createInstance("flip2_col");
    FrameAllocator allocator(1024 * 1024);
    SceneCollector collector;
    collector.setup(ioCollector.get());

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", 10.0); sprite->setDouble("cy", 20.0);
    sprite->setDouble("rotation", 1.5707963);   // the flip must not disturb the angle
    sprite->setBool("flipX", true);
    sprite->setBool("flipY", true);
    sprite->setInt("textureId", 3);
    ioPublisher->publish("render:sprite", std::move(sprite));

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == 1);
    const auto& s = packet.sprites[0];
    REQUIRE_THAT(s.u0, WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(s.u1, WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(s.v0, WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(s.v1, WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(s.rotation, WithinAbs(1.5707963f, 0.0001f));
}

TEST_CASE("SceneCollector: no flip field leaves UVs strictly unchanged", "[collector][sprite][flip]") {
    // THE non-regression: every sprite Drifterra already publishes carries no flip, and must come out
    // byte-identical. A default that mirrored (or normalised u0<u1) would silently break them all.
    auto ioPublisher = IntraIOManager::getInstance().createInstance("flip3_pub");
    auto ioCollector = IntraIOManager::getInstance().createInstance("flip3_col");
    FrameAllocator allocator(1024 * 1024);
    SceneCollector collector;
    collector.setup(ioCollector.get());

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", 1.0); sprite->setDouble("cy", 2.0);
    sprite->setDouble("u0", 0.2); sprite->setDouble("u1", 0.8);
    sprite->setDouble("v0", 0.3); sprite->setDouble("v1", 0.7);
    sprite->setInt("textureId", 1);
    ioPublisher->publish("render:sprite", std::move(sprite));

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == 1);
    const auto& s = packet.sprites[0];
    REQUIRE_THAT(s.u0, WithinAbs(0.2f, 0.001f));
    REQUIRE_THAT(s.u1, WithinAbs(0.8f, 0.001f));
    REQUIRE_THAT(s.v0, WithinAbs(0.3f, 0.001f));
    REQUIRE_THAT(s.v1, WithinAbs(0.7f, 0.001f));
}

TEST_CASE("SceneCollector - parse multiple sprites", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Publish multiple sprites
    auto sprite1 = std::make_unique<JsonDataNode>("sprite");
    sprite1->setDouble("cx", 10.0);
    sprite1->setDouble("cy", 20.0);
    sprite1->setInt("color", 0xFFFFFFFF);
    ioPublisher->publish("render:sprite", std::move(sprite1));

    auto sprite2 = std::make_unique<JsonDataNode>("sprite");
    sprite2->setDouble("cx", 30.0);
    sprite2->setDouble("cy", 40.0);
    sprite2->setInt("color", 0xFF0000FF);
    ioPublisher->publish("render:sprite", std::move(sprite2));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == 2);
    REQUIRE_THAT(packet.sprites[0].x, WithinAbs(10.0f, 0.01f));
    REQUIRE_THAT(packet.sprites[1].x, WithinAbs(30.0f, 0.01f));
}

// ============================================================================
// Camera Parsing
// ============================================================================

TEST_CASE("SceneCollector - parse camera with matrices", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto camera = std::make_unique<JsonDataNode>("camera");
    camera->setDouble("x", 100.0);
    camera->setDouble("y", 200.0);
    camera->setDouble("zoom", 2.0);
    camera->setInt("viewportX", 0);
    camera->setInt("viewportY", 0);
    camera->setInt("viewportW", 1280);
    camera->setInt("viewportH", 720);

    ioPublisher->publish("render:camera", std::move(camera));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE_THAT(packet.mainView.positionX, WithinAbs(100.0f, 0.01f));
    REQUIRE_THAT(packet.mainView.positionY, WithinAbs(200.0f, 0.01f));
    REQUIRE_THAT(packet.mainView.zoom, WithinAbs(2.0f, 0.01f));
    REQUIRE(packet.mainView.viewportW == 1280);
    REQUIRE(packet.mainView.viewportH == 720);

    // Check view matrix (translation by -camera position)
    REQUIRE_THAT(packet.mainView.viewMatrix[12], WithinAbs(-100.0f, 0.01f));
    REQUIRE_THAT(packet.mainView.viewMatrix[13], WithinAbs(-200.0f, 0.01f));

    // Check projection matrix is not zero (ortho projection)
    REQUIRE(packet.mainView.projMatrix[0] != 0.0f);
    REQUIRE(packet.mainView.projMatrix[5] != 0.0f);
}

// ----------------------------------------------------------------------------
// Multiply a column-major 4x4 matrix (element [col*4+row], translation in 12/13/14
// — the bgfx storage SceneCollector::parseCamera writes) by a column vector.
// ----------------------------------------------------------------------------
static inline void mat4MulVec4(const float* m, float x, float y, float z, float w, float out[4]) {
    for (int r = 0; r < 4; ++r)
        out[r] = m[0 * 4 + r] * x + m[1 * 4 + r] * y + m[2 * 4 + r] * z + m[3 * 4 + r] * w;
}

// This is the lock that PROVES the renderer actually scales by zoom (the previous test
// only checked the zoom value was stored + projMatrix non-zero — it never exercised the
// projection). It does two things:
//   1. Runs world points through the REAL view*proj matrices SceneCollector produced and
//      confirms the resulting screen position equals grove::camera::worldToScreen — i.e.
//      the helper tells the truth about what the engine renders (the contract Drifterra
//      builds its seamless zoom on).
//   2. Confirms doubling zoom doubles on-screen separation (zoom genuinely magnifies).
TEST_CASE("SceneCollector - zoom scales the projection and matches grove::camera", "[scene_collector][integration][camera]") {
    auto& ioManager = IntraIOManager::getInstance();

    // Build the FramePacket view a real camera message produces, return a value copy of
    // the view (ViewInfo is plain floats — safe once the collector/allocator are gone).
    auto viewFor = [&](float camX, float camY, float zoom) -> ViewInfo {
        auto ioCollector = ioManager.createInstance(uniqueId("cam_recv"));
        auto ioPublisher = ioManager.createInstance(uniqueId("cam_pub"));
        SceneCollector collector;
        FrameAllocator allocator;
        collector.setup(ioCollector.get());

        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x", camX);
        cam->setDouble("y", camY);
        cam->setDouble("zoom", zoom);
        cam->setInt("viewportW", 1280);
        cam->setInt("viewportH", 720);
        ioPublisher->publish("render:camera", std::move(cam));

        collector.collect(ioCollector.get(), 0.016f);
        return collector.finalize(allocator).mainView;
    };

    // Project a world point through the engine's matrices into screen pixels (top-left
    // origin, y-down) — the same space grove::camera::worldToScreen targets.
    auto engineProject = [](const ViewInfo& v, float wx, float wy, float& sx, float& sy) {
        float eye[4], clip[4];
        mat4MulVec4(v.viewMatrix, wx, wy, 0.0f, 1.0f, eye);
        mat4MulVec4(v.projMatrix, eye[0], eye[1], eye[2], eye[3], clip);
        const float ndcX = clip[0] / clip[3];
        const float ndcY = clip[1] / clip[3];
        sx = (ndcX * 0.5f + 0.5f) * v.viewportW;
        sy = (0.5f - ndcY * 0.5f) * v.viewportH;  // NDC y-up -> screen y-down
    };

    // (1) Engine matrices == helper, for a non-trivial camera (offset + zoom 2).
    ViewInfo v = viewFor(100.0f, 200.0f, 2.0f);
    camera::CameraView c{100.0f, 200.0f, 2.0f, 1280.0f, 720.0f};
    const float pts[][2] = {{100.0f, 200.0f}, {420.0f, 380.0f}, {740.0f, 560.0f}, {-30.0f, 50.0f}};
    for (auto& p : pts) {
        float es_x, es_y, hs_x, hs_y;
        engineProject(v, p[0], p[1], es_x, es_y);
        camera::worldToScreen(c, p[0], p[1], hs_x, hs_y);
        REQUIRE_THAT(es_x, WithinAbs(hs_x, 0.05f));
        REQUIRE_THAT(es_y, WithinAbs(hs_y, 0.05f));
    }

    // (2) Doubling zoom doubles on-screen distance from the camera origin.
    ViewInfo v1 = viewFor(0.0f, 0.0f, 1.0f);
    ViewInfo v2 = viewFor(0.0f, 0.0f, 2.0f);
    float s1x, s1y, s2x, s2y;
    engineProject(v1, 100.0f, 50.0f, s1x, s1y);
    engineProject(v2, 100.0f, 50.0f, s2x, s2y);
    REQUIRE_THAT(s1x, WithinAbs(100.0f, 0.05f));
    REQUIRE_THAT(s1y, WithinAbs(50.0f, 0.05f));
    REQUIRE_THAT(s2x, WithinAbs(2.0f * s1x, 0.05f));
    REQUIRE_THAT(s2y, WithinAbs(2.0f * s1y, 0.05f));

    // Zoom OUT (zoom < 1) must scale too — half zoom => half on-screen distance. Locks that
    // "dezoom" is correct at the engine level (a showcase key quirk is not an engine bug).
    ViewInfo vHalf = viewFor(0.0f, 0.0f, 0.5f);
    float shx = 0.0f, shy = 0.0f;
    engineProject(vHalf, 100.0f, 50.0f, shx, shy);
    REQUIRE_THAT(shx, WithinAbs(50.0f, 0.05f));   // 0.5 * 100
    REQUIRE_THAT(shy, WithinAbs(25.0f, 0.05f));   // 0.5 * 50
}

// Locks CAMERA ROTATION (slice R): the renderer's rotated view matrix matches the rotation-aware
// grove::camera helper, the rotation pivots around the SCREEN CENTRE (that world point doesn't move),
// and a concrete 90° case lands where expected. rotation 0 is covered by the test above (unchanged).
TEST_CASE("SceneCollector - camera rotation matches grove::camera and pivots on screen centre", "[scene_collector][integration][camera]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto viewFor = [&](float camX, float camY, float zoom, float rotation) -> ViewInfo {
        auto ioC = ioManager.createInstance(uniqueId("camrot_recv"));
        auto ioP = ioManager.createInstance(uniqueId("camrot_pub"));
        SceneCollector collector; FrameAllocator allocator;
        collector.setup(ioC.get());
        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x", camX); cam->setDouble("y", camY);
        cam->setDouble("zoom", zoom); cam->setDouble("rotation", rotation);
        cam->setInt("viewportW", 1280); cam->setInt("viewportH", 720);
        ioP->publish("render:camera", std::move(cam));
        collector.collect(ioC.get(), 0.016f);
        return collector.finalize(allocator).mainView;
    };
    auto engineProject = [](const ViewInfo& v, float wx, float wy, float& sx, float& sy) {
        float eye[4], clip[4];
        mat4MulVec4(v.viewMatrix, wx, wy, 0.0f, 1.0f, eye);
        mat4MulVec4(v.projMatrix, eye[0], eye[1], eye[2], eye[3], clip);
        sx = (clip[0] / clip[3] * 0.5f + 0.5f) * v.viewportW;
        sy = (0.5f - clip[1] / clip[3] * 0.5f) * v.viewportH;
    };

    const float rot = 0.7853981634f;  // 45 degrees

    // (1) The engine's rotated matrices == grove::camera::worldToScreen (same convention).
    ViewInfo v = viewFor(100.0f, 200.0f, 1.5f, rot);
    camera::CameraView c{100.0f, 200.0f, 1.5f, 1280.0f, 720.0f, rot};
    const float pts[][2] = {{100.0f, 200.0f}, {460.0f, 380.0f}, {800.0f, 200.0f}, {300.0f, 560.0f}};
    for (auto& p : pts) {
        float ex, ey, hx, hy;
        engineProject(v, p[0], p[1], ex, ey);
        camera::worldToScreen(c, p[0], p[1], hx, hy);
        REQUIRE_THAT(ex, WithinAbs(hx, 0.1f));
        REQUIRE_THAT(ey, WithinAbs(hy, 0.1f));
    }

    // (2) Rotation pivots on the SCREEN CENTRE: the world point shown at centre is unchanged by rotation.
    camera::CameraView cv0{100.0f, 200.0f, 1.5f, 1280.0f, 720.0f, 0.0f};
    camera::CameraView cv1{100.0f, 200.0f, 1.5f, 1280.0f, 720.0f, rot};
    float c0x, c0y, c1x, c1y;
    camera::screenToWorld(cv0, 640.0f, 360.0f, c0x, c0y);
    camera::screenToWorld(cv1, 640.0f, 360.0f, c1x, c1y);
    REQUIRE_THAT(c1x, WithinAbs(c0x, 0.05f));
    REQUIRE_THAT(c1y, WithinAbs(c0y, 0.05f));

    // (3) Concrete 90° roll (square view): pivot = world centre (500,500); a point 100 to its RIGHT
    //     appears 100 BELOW centre on screen (x stays at centre).
    camera::CameraView cv90{0.0f, 0.0f, 1.0f, 1000.0f, 1000.0f, 1.5707963f};
    float pcx, pcy; camera::screenToWorld(cv90, 500.0f, 500.0f, pcx, pcy);
    REQUIRE_THAT(pcx, WithinAbs(500.0f, 0.05f));
    REQUIRE_THAT(pcy, WithinAbs(500.0f, 0.05f));
    float s90x, s90y; camera::worldToScreen(cv90, 600.0f, 500.0f, s90x, s90y);
    REQUIRE_THAT(s90x, WithinAbs(500.0f, 0.05f));
    REQUIRE_THAT(s90y, WithinAbs(600.0f, 0.05f));
}

// This locks the HUD overlay contract (engine help: screen-space view so the HUD does NOT
// zoom/pan with the world). Two guarantees:
//   1. render:rect / render:text carrying space:"screen" are bucketed into the HUD arrays,
//      NOT the world arrays.
//   2. the HUD view is a fixed screen-space transform (zoom 1, no translation) — INVARIANT
//      under a zoomed/panned render:camera. That invariance IS the feature: a HUD drawn on
//      this view stays put while the world zooms.
TEST_CASE("SceneCollector - screen-space (HUD) commands bucket apart and ignore the camera", "[scene_collector][integration][hud]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("hud_recv"));
    auto ioPublisher = ioManager.createInstance(uniqueId("hud_pub"));
    SceneCollector collector;
    FrameAllocator allocator;
    collector.setup(ioCollector.get());

    // A hard zoom + pan on the WORLD camera.
    {
        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x", 100.0); cam->setDouble("y", 200.0); cam->setDouble("zoom", 5.0);
        cam->setInt("viewportW", 1280); cam->setInt("viewportH", 720);
        ioPublisher->publish("render:camera", std::move(cam));
    }
    // World rect (default space) — should land in the world bucket.
    {
        auto r = std::make_unique<JsonDataNode>("rect");
        r->setDouble("x", 10.0); r->setDouble("y", 10.0); r->setDouble("w", 50.0); r->setDouble("h", 20.0);
        r->setInt("color", 0xFF0000FF);
        ioPublisher->publish("render:rect", std::move(r));
    }
    // HUD rect (space:"screen") — should land in the HUD bucket.
    {
        auto r = std::make_unique<JsonDataNode>("rect");
        r->setDouble("x", 10.0); r->setDouble("y", 10.0); r->setDouble("w", 50.0); r->setDouble("h", 20.0);
        r->setInt("color", 0x00FF00FF);
        r->setString("space", "screen");
        ioPublisher->publish("render:rect", std::move(r));
    }
    // HUD text (space:"screen").
    {
        auto t = std::make_unique<JsonDataNode>("text");
        t->setDouble("x", 5.0); t->setDouble("y", 5.0); t->setString("text", "HP");
        t->setString("space", "screen");
        ioPublisher->publish("render:text", std::move(t));
    }

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    // (1) Bucketing: world rect in world bucket; HUD rect + HUD text in HUD buckets.
    REQUIRE(packet.spriteCount == 1);
    REQUIRE(packet.hudSpriteCount == 1);
    REQUIRE(packet.hudTextCount == 1);
    REQUIRE(packet.textCount == 0);

    // (2) HUD view is screen-space and INVARIANT under the zoom=5 / pan camera.
    REQUIRE_THAT(packet.hudView.zoom, WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(packet.hudView.viewMatrix[12], WithinAbs(0.0f, 0.001f));  // no translation
    REQUIRE_THAT(packet.hudView.viewMatrix[13], WithinAbs(0.0f, 0.001f));
    REQUIRE(packet.hudView.viewportW == 1280);                            // spans the live viewport
    REQUIRE(packet.hudView.viewportH == 720);

    // The world view DID take the zoom (sanity: the two views diverge as intended).
    REQUIRE_THAT(packet.mainView.zoom, WithinAbs(5.0f, 0.001f));

    // Concretely: on the HUD view, screen == world (1:1), regardless of the world camera.
    camera::CameraView hud{packet.hudView.positionX, packet.hudView.positionY, packet.hudView.zoom,
                           static_cast<float>(packet.hudView.viewportW),
                           static_cast<float>(packet.hudView.viewportH)};
    float sx = 0.0f, sy = 0.0f;
    camera::worldToScreen(hud, 5.0f, 5.0f, sx, sy);
    REQUIRE_THAT(sx, WithinAbs(5.0f, 0.001f));
    REQUIRE_THAT(sy, WithinAbs(5.0f, 0.001f));
}

// ============================================================================
// Tilemap Parsing
// ============================================================================

TEST_CASE("SceneCollector - parse tilemap with tiles", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto tilemap = std::make_unique<JsonDataNode>("tilemap");
    tilemap->setDouble("x", 0.0);
    tilemap->setDouble("y", 0.0);
    tilemap->setInt("width", 10);
    tilemap->setInt("height", 10);
    tilemap->setInt("tileW", 16);
    tilemap->setInt("tileH", 16);
    tilemap->setInt("textureId", 5);
    tilemap->setString("tileData", "1,2,3,4,5");

    ioPublisher->publish("render:tilemap", std::move(tilemap));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.tilemapCount == 1);
    REQUIRE(packet.tilemaps != nullptr);

    const auto& tm = packet.tilemaps[0];
    REQUIRE(tm.width == 10);
    REQUIRE(tm.height == 10);
    REQUIRE(tm.tileWidth == 16);
    REQUIRE(tm.tileHeight == 16);
    REQUIRE(tm.textureId == 5);
    REQUIRE(tm.tileCount == 5);
    REQUIRE(tm.tiles != nullptr);

    // Check tile data copied correctly
    REQUIRE(tm.tiles[0] == 1);
    REQUIRE(tm.tiles[1] == 2);
    REQUIRE(tm.tiles[4] == 5);
}

// ============================================================================
// Text Parsing
// ============================================================================

TEST_CASE("SceneCollector - parse text with string", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto text = std::make_unique<JsonDataNode>("text");
    text->setDouble("x", 50.0);
    text->setDouble("y", 100.0);
    text->setString("text", "Hello World");
    text->setInt("fontId", 1);
    text->setInt("fontSize", 24);
    text->setInt("color", 0xFFFFFFFF);
    text->setInt("layer", 5);

    ioPublisher->publish("render:text", std::move(text));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.textCount == 1);
    REQUIRE(packet.texts != nullptr);

    const auto& t = packet.texts[0];
    REQUIRE_THAT(t.x, WithinAbs(50.0f, 0.01f));
    REQUIRE_THAT(t.y, WithinAbs(100.0f, 0.01f));
    REQUIRE(t.fontId == 1);
    REQUIRE(t.fontSize == 24);
    REQUIRE(t.color == 0xFFFFFFFF);
    REQUIRE(t.layer == 5);
    REQUIRE(t.text != nullptr);
    REQUIRE(std::string(t.text) == "Hello World");
}

// ============================================================================
// Particle Parsing
// ============================================================================

TEST_CASE("SceneCollector - parse particle", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto particle = std::make_unique<JsonDataNode>("particle");
    particle->setDouble("cx", 10.0);
    particle->setDouble("cy", 20.0);
    particle->setDouble("vx", 1.0);
    particle->setDouble("vy", -2.0);
    particle->setDouble("size", 4.0);
    particle->setDouble("life", 0.5);
    particle->setInt("color", 0xFF00FF00);
    particle->setInt("textureId", 3);

    ioPublisher->publish("render:particle", std::move(particle));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.particleCount == 1);
    REQUIRE(packet.particles != nullptr);

    const auto& p = packet.particles[0];
    REQUIRE_THAT(p.x, WithinAbs(10.0f, 0.01f));
    REQUIRE_THAT(p.y, WithinAbs(20.0f, 0.01f));
    REQUIRE_THAT(p.vx, WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(p.vy, WithinAbs(-2.0f, 0.01f));
    REQUIRE_THAT(p.size, WithinAbs(4.0f, 0.01f));
    REQUIRE_THAT(p.life, WithinAbs(0.5f, 0.01f));
    REQUIRE(p.color == 0xFF00FF00);
    REQUIRE(p.textureId == 3);
}

// ============================================================================
// Clear Color Parsing
// ============================================================================

TEST_CASE("SceneCollector - parse clear color", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto clear = std::make_unique<JsonDataNode>("clear");
    clear->setInt("color", 0x12345678);

    ioPublisher->publish("render:clear", std::move(clear));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.clearColor == 0x12345678);
}

// ============================================================================
// Debug Shapes Parsing
// ============================================================================

TEST_CASE("SceneCollector - parse debug line", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto line = std::make_unique<JsonDataNode>("line");
    line->setDouble("x1", 0.0);
    line->setDouble("y1", 0.0);
    line->setDouble("x2", 100.0);
    line->setDouble("y2", 100.0);
    line->setInt("color", 0xFF0000FF);

    ioPublisher->publish("render:debug:line", std::move(line));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.debugLineCount == 1);
    REQUIRE(packet.debugLines != nullptr);

    const auto& l = packet.debugLines[0];
    REQUIRE_THAT(l.x1, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(l.y1, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(l.x2, WithinAbs(100.0f, 0.01f));
    REQUIRE_THAT(l.y2, WithinAbs(100.0f, 0.01f));
    REQUIRE(l.color == 0xFF0000FF);
}

TEST_CASE("SceneCollector - parse debug rect filled", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto rect = std::make_unique<JsonDataNode>("rect");
    rect->setDouble("x", 10.0);
    rect->setDouble("y", 20.0);
    rect->setDouble("w", 50.0);
    rect->setDouble("h", 30.0);
    rect->setInt("color", 0x00FF00FF);
    rect->setBool("filled", true);

    ioPublisher->publish("render:debug:rect", std::move(rect));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.debugRectCount == 1);

    const auto& r = packet.debugRects[0];
    REQUIRE_THAT(r.x, WithinAbs(10.0f, 0.01f));
    REQUIRE_THAT(r.y, WithinAbs(20.0f, 0.01f));
    REQUIRE_THAT(r.w, WithinAbs(50.0f, 0.01f));
    REQUIRE_THAT(r.h, WithinAbs(30.0f, 0.01f));
    REQUIRE(r.filled == true);
}

TEST_CASE("SceneCollector - parse debug rect outline", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto rect = std::make_unique<JsonDataNode>("rect");
    rect->setDouble("x", 0.0);
    rect->setDouble("y", 0.0);
    rect->setDouble("w", 100.0);
    rect->setDouble("h", 100.0);
    rect->setInt("color", 0xFFFFFFFF);
    rect->setBool("filled", false);

    ioPublisher->publish("render:debug:rect", std::move(rect));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.debugRects[0].filled == false);
}

// ============================================================================
// FramePacket Construction
// ============================================================================

TEST_CASE("SceneCollector - finalize copies to allocator", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Add multiple sprites
    for (int i = 0; i < 5; ++i) {
        auto sprite = std::make_unique<JsonDataNode>("sprite");
        sprite->setDouble("cx", i * 10.0);
        sprite->setDouble("cy", i * 20.0);
        ioPublisher->publish("render:sprite", std::move(sprite));
    }

    collector.collect(ioCollector.get(), 0.016f);

    size_t allocatorUsedBefore = allocator.getUsed();

    FramePacket packet = collector.finalize(allocator);

    size_t allocatorUsedAfter = allocator.getUsed();

    // Allocator should have allocated memory for sprites
    REQUIRE(allocatorUsedAfter > allocatorUsedBefore);
    REQUIRE(packet.spriteCount == 5);
    REQUIRE(packet.sprites != nullptr);

    // Verify data integrity
    for (int i = 0; i < 5; ++i) {
        REQUIRE_THAT(packet.sprites[i].x, WithinAbs(i * 10.0f, 0.01f));
    }
}

TEST_CASE("SceneCollector - finalize string pointers valid", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto text1 = std::make_unique<JsonDataNode>("text");
    text1->setString("text", "First");
    ioPublisher->publish("render:text", std::move(text1));

    auto text2 = std::make_unique<JsonDataNode>("text");
    text2->setString("text", "Second");
    ioPublisher->publish("render:text", std::move(text2));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.textCount == 2);
    REQUIRE(std::string(packet.texts[0].text) == "First");
    REQUIRE(std::string(packet.texts[1].text) == "Second");

    // Pointers should be different (allocated separately)
    REQUIRE(packet.texts[0].text != packet.texts[1].text);
}

// ============================================================================
// Clear & Multiple Frames
// ============================================================================

TEST_CASE("SceneCollector - clear empties collections", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", 10.0);
    ioPublisher->publish("render:sprite", std::move(sprite));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet1 = collector.finalize(allocator);
    REQUIRE(packet1.spriteCount == 1);

    collector.clear();

    // After clear, no sprites should be collected
    allocator.reset();
    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet2 = collector.finalize(allocator);

    REQUIRE(packet2.spriteCount == 0);
}

TEST_CASE("SceneCollector - multiple frame cycles", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Frame 1
    {
        auto sprite = std::make_unique<JsonDataNode>("sprite");
        sprite->setDouble("cx", 100.0);
        ioPublisher->publish("render:sprite", std::move(sprite));

        collector.collect(ioCollector.get(), 0.016f);
        FramePacket packet = collector.finalize(allocator);

        REQUIRE(packet.spriteCount == 1);
        REQUIRE(packet.frameNumber == 1);

        collector.clear();
        allocator.reset();
    }

    // Frame 2
    {
        auto sprite1 = std::make_unique<JsonDataNode>("sprite");
        sprite1->setDouble("cx", 200.0);
        ioPublisher->publish("render:sprite", std::move(sprite1));

        auto sprite2 = std::make_unique<JsonDataNode>("sprite");
        sprite2->setDouble("cx", 300.0);
        ioPublisher->publish("render:sprite", std::move(sprite2));

        collector.collect(ioCollector.get(), 0.016f);
        FramePacket packet = collector.finalize(allocator);

        REQUIRE(packet.spriteCount == 2);
        REQUIRE(packet.frameNumber == 2);

        collector.clear();
        allocator.reset();
    }

    // Frame 3
    {
        collector.collect(ioCollector.get(), 0.016f);
        FramePacket packet = collector.finalize(allocator);

        REQUIRE(packet.spriteCount == 0);
        REQUIRE(packet.frameNumber == 3);
    }
}

// ============================================================================
// Layer / z-order (audit #4)
// ============================================================================
// The submit order = the order items appear in the FramePacket. finalize() merged
// retained (unordered_map hash order!) then ephemeral, with NO sort by layer — so
// z-order was non-deterministic / wrong. These lock the fix: the packet is sorted by
// layer ascending (lower layer drawn first / behind), stably (equal layers keep order).

TEST_CASE("SceneCollector - sprites emitted sorted by layer ascending (#4)", "[scene_collector][integration][layer]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // One retained sprite (layer 2). Retained are merged first in hash order.
    auto retained = std::make_unique<JsonDataNode>("s");
    retained->setInt("renderId", 99);
    retained->setDouble("cx", 20.0);
    retained->setInt("layer", 2);
    ioPublisher->publish("render:sprite:add", std::move(retained));

    // Ephemeral sprites published in DESCENDING layer order (5, 1, 3) — so the raw
    // (unsorted) packet order [retained 2, 5, 1, 3] is NOT layer-sorted.
    const int layers[] = {5, 1, 3};
    const double xs[]   = {50.0, 10.0, 30.0};
    for (int i = 0; i < 3; ++i) {
        auto s = std::make_unique<JsonDataNode>("sprite");
        s->setDouble("cx", xs[i]);
        s->setInt("layer", layers[i]);
        ioPublisher->publish("render:sprite", std::move(s));
    }

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == 4);

    // Layers must be non-decreasing (correct back-to-front z-order).
    for (size_t i = 1; i < packet.spriteCount; ++i) {
        INFO("layer[" << (i - 1) << "]=" << packet.sprites[i - 1].layer
             << " layer[" << i << "]=" << packet.sprites[i].layer);
        REQUIRE(packet.sprites[i - 1].layer <= packet.sprites[i].layer);
    }

    // Concretely: sorted layers 1,2,3,5 -> x 10,20,30,50.
    REQUIRE_THAT(packet.sprites[0].x, WithinAbs(10.0f, 0.01f));
    REQUIRE_THAT(packet.sprites[1].x, WithinAbs(20.0f, 0.01f));
    REQUIRE_THAT(packet.sprites[2].x, WithinAbs(30.0f, 0.01f));
    REQUIRE_THAT(packet.sprites[3].x, WithinAbs(50.0f, 0.01f));
}

TEST_CASE("SceneCollector - texts emitted sorted by layer ascending (#4)", "[scene_collector][integration][layer]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Ephemeral texts in DESCENDING layer order.
    const int layers[] = {7, 2, 4};
    const char* names[] = {"seven", "two", "four"};
    for (int i = 0; i < 3; ++i) {
        auto t = std::make_unique<JsonDataNode>("text");
        t->setString("text", names[i]);
        t->setInt("layer", layers[i]);
        ioPublisher->publish("render:text", std::move(t));
    }

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.textCount == 3);
    for (size_t i = 1; i < packet.textCount; ++i) {
        REQUIRE(packet.texts[i - 1].layer <= packet.texts[i].layer);
    }
    // Sorted layers 2,4,7 -> "two","four","seven".
    REQUIRE(std::string(packet.texts[0].text) == "two");
    REQUIRE(std::string(packet.texts[1].text) == "four");
    REQUIRE(std::string(packet.texts[2].text) == "seven");
}

// ============================================================================
// render:rect — layered filled quad (engine help A2, for HUD-heavy games)
// ============================================================================
// debug:rect draws in the LAST pass (over text) and has no layer. render:rect instead
// goes through the sprite path: a textureId=0 (white) tinted quad, which is sorted by
// layer and drawn BEFORE text — so a HUD panel can sit UNDER its label. Coords are
// top-left (x,y,w,h) like debug:rect; the collector centers them for the sprite.

TEST_CASE("SceneCollector - render:rect becomes a layered filled sprite quad (A2)", "[scene_collector][integration][rect]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    auto rect = std::make_unique<JsonDataNode>("rect");
    rect->setDouble("x", 100.0);   // top-left
    rect->setDouble("y", 50.0);
    rect->setDouble("w", 200.0);
    rect->setDouble("h", 30.0);
    rect->setInt("color", static_cast<int>(0x804020FFu));
    rect->setInt("layer", 5);
    ioPublisher->publish("render:rect", std::move(rect));

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == 1);
    const auto& s = packet.sprites[0];
    REQUIRE_THAT(s.x, WithinAbs(200.0f, 0.01f));       // center = x + w/2
    REQUIRE_THAT(s.y, WithinAbs(65.0f, 0.01f));        // center = y + h/2
    REQUIRE_THAT(s.scaleX, WithinAbs(200.0f, 0.01f));  // full width
    REQUIRE_THAT(s.scaleY, WithinAbs(30.0f, 0.01f));   // full height
    REQUIRE_THAT(s.textureId, WithinAbs(0.0f, 0.01f)); // white texture -> solid color
    REQUIRE_THAT(s.layer, WithinAbs(5.0f, 0.01f));     // honored z-order
    REQUIRE_THAT(s.r, WithinAbs(0x80 / 255.0f, 0.01f));
    REQUIRE_THAT(s.a, WithinAbs(1.0f, 0.01f));
}

// ============================================================================
// Mixed Message Types
// ============================================================================

TEST_CASE("SceneCollector - collect mixed message types", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Publish various message types
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", 10.0);
    ioPublisher->publish("render:sprite", std::move(sprite));

    auto text = std::make_unique<JsonDataNode>("text");
    text->setString("text", "Test");
    ioPublisher->publish("render:text", std::move(text));

    auto particle = std::make_unique<JsonDataNode>("particle");
    particle->setDouble("cx", 5.0);
    ioPublisher->publish("render:particle", std::move(particle));

    auto line = std::make_unique<JsonDataNode>("line");
    line->setDouble("x1", 0.0);
    line->setDouble("y1", 0.0);
    line->setDouble("x2", 10.0);
    line->setDouble("y2", 10.0);
    ioPublisher->publish("render:debug:line", std::move(line));

    collector.collect(ioCollector.get(), 0.016f);

    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == 1);
    REQUIRE(packet.textCount == 1);
    REQUIRE(packet.particleCount == 1);
    REQUIRE(packet.debugLineCount == 1);
}

// ============================================================================
// BULK direct-feed (SceneCollector::addSpritesBulk) — the high-throughput path that
// bypasses IIO + JSON. Locks the contract proven by benchmark_render_savage (the bulk
// path sustains ~22× the 60fps sprite ceiling of render:sprite). GPU-free: pure collector.
// ============================================================================

namespace {
// Build a GPU-ready instance with a recognizable x and layer (the only fields asserted).
inline SpriteInstance makeInstance(float x, float layer) {
    SpriteInstance s{};
    s.x = x; s.y = 0.0f; s.scaleX = 1.0f; s.scaleY = 1.0f;
    s.u1 = 1.0f; s.v1 = 1.0f; s.layer = layer; s.a = 1.0f;
    return s;
}
} // namespace

TEST_CASE("SceneCollector - addSpritesBulk feeds N instances into the frame (no IIO/JSON)", "[scene_collector][bulk]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    std::vector<SpriteInstance> batch = {
        makeInstance(10.0f, 0.0f), makeInstance(20.0f, 0.0f), makeInstance(30.0f, 0.0f)};
    fx.collector.addSpritesBulk(batch.data(), batch.size());

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.spriteCount == 3);
    // Same layer → stable order preserved, so the first instance keeps its x.
    REQUIRE_THAT(p.sprites[0].x, WithinAbs(10.0f, 0.01f));
    REQUIRE_THAT(p.sprites[2].x, WithinAbs(30.0f, 0.01f));
}

TEST_CASE("SceneCollector - bulk sprites and IIO ephemeral sprites coexist in one frame", "[scene_collector][bulk]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Two via the bulk path...
    std::vector<SpriteInstance> batch = {makeInstance(1.0f, 0.0f), makeInstance(2.0f, 0.0f)};
    fx.collector.addSpritesBulk(batch.data(), batch.size());
    // ...and one via the classic IIO/JSON path.
    auto s = std::make_unique<JsonDataNode>("s");
    s->setDouble("cx", 99.0);
    fx.ioPublisher->publish("render:sprite", std::move(s));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.spriteCount == 3);   // merged: both feeds land in the same ephemeral list
}

TEST_CASE("SceneCollector - clear() drops bulk sprites (ephemeral, per-frame)", "[scene_collector][bulk]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    std::vector<SpriteInstance> batch = {makeInstance(5.0f, 0.0f)};
    fx.collector.addSpritesBulk(batch.data(), batch.size());
    REQUIRE(fx.collector.finalize(allocator).spriteCount == 1);

    fx.collector.clear();          // end-of-frame reset
    REQUIRE(fx.collector.finalize(allocator).spriteCount == 0);  // gone next frame
}

// ---------------------------------------------------------------------------
// BULK particles (SceneCollector::addParticlesBulk) — symmetric to sprites; ParticleInstance is POD.
// ---------------------------------------------------------------------------

namespace {
inline ParticleInstance makeParticle(float x, uint32_t color) {
    ParticleInstance p{};
    p.x = x; p.y = 0.0f; p.vx = 0.0f; p.vy = 0.0f; p.size = 2.0f; p.life = 1.0f;
    p.color = color; p.textureId = 0;
    return p;
}
} // namespace

TEST_CASE("SceneCollector - addParticlesBulk feeds N particles into the frame (no IIO/JSON)", "[scene_collector][bulk]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    std::vector<ParticleInstance> batch = {
        makeParticle(10.0f, 0xFF0000FF), makeParticle(20.0f, 0x00FF00FF), makeParticle(30.0f, 0x0000FFFF)};
    fx.collector.addParticlesBulk(batch.data(), batch.size());

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.particleCount == 3);
    REQUIRE(p.particles != nullptr);
    REQUIRE_THAT(p.particles[0].x, WithinAbs(10.0f, 0.01f));
    REQUIRE(p.particles[0].color == 0xFF0000FF);
    REQUIRE_THAT(p.particles[2].x, WithinAbs(30.0f, 0.01f));
    REQUIRE(p.particles[2].color == 0x0000FFFF);
}

TEST_CASE("SceneCollector - bulk particles coexist with a render:particle, and clear() drops them", "[scene_collector][bulk]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    std::vector<ParticleInstance> batch = {makeParticle(1.0f, 0xFFFFFFFF), makeParticle(2.0f, 0xFFFFFFFF)};
    fx.collector.addParticlesBulk(batch.data(), batch.size());
    auto pn = std::make_unique<JsonDataNode>("particle");
    pn->setDouble("cx", 99.0);
    fx.ioPublisher->publish("render:particle", std::move(pn));
    fx.pump();

    REQUIRE(fx.collector.finalize(allocator).particleCount == 3);   // both feeds merge in one frame

    fx.collector.clear();
    REQUIRE(fx.collector.finalize(allocator).particleCount == 0);    // ephemeral: gone next frame
}

// ---------------------------------------------------------------------------
// BULK text (SceneCollector::addTextsBulk) — N labels in one call; strings copied into the frame.
// ---------------------------------------------------------------------------

namespace {
// Build a TextCommand whose `text` points at a caller-owned string (copied by addTextsBulk).
inline TextCommand makeText(float x, const char* str, uint16_t layer) {
    TextCommand t{};
    t.x = x; t.y = 0.0f; t.text = str; t.fontId = 0; t.fontSize = 16;
    t.color = 0xFFFFFFFF; t.layer = layer;
    return t;
}
} // namespace

TEST_CASE("SceneCollector - addTextsBulk feeds N labels with their strings (no IIO/JSON)", "[scene_collector][bulk]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Caller-owned strings; addTextsBulk must COPY them (they can die after the call).
    std::vector<TextCommand> batch = {
        makeText(10.0f, "Aurora", 0), makeText(20.0f, "Borealis", 0), makeText(30.0f, "Cygnus", 0)};
    fx.collector.addTextsBulk(batch.data(), batch.size());

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.textCount == 3);
    REQUIRE(p.texts != nullptr);
    // Same layer -> stable order; strings survived the copy into the frame arena.
    REQUIRE(p.texts[0].text != nullptr);
    REQUIRE(std::string(p.texts[0].text) == "Aurora");
    REQUIRE(std::string(p.texts[2].text) == "Cygnus");
    REQUIRE_THAT(p.texts[2].x, WithinAbs(30.0f, 0.01f));
}

TEST_CASE("SceneCollector - bulk text strings survive after the caller's buffers are gone", "[scene_collector][bulk]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Feed from a scope whose std::strings are destroyed before finalize — proves addTextsBulk copies.
    {
        std::string s0 = "Ephemeral0", s1 = "Ephemeral1";
        std::vector<TextCommand> batch = {makeText(1.0f, s0.c_str(), 0), makeText(2.0f, s1.c_str(), 0)};
        fx.collector.addTextsBulk(batch.data(), batch.size());
    }   // s0/s1/batch destroyed here

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.textCount == 2);
    REQUIRE(std::string(p.texts[0].text) == "Ephemeral0");
    REQUIRE(std::string(p.texts[1].text) == "Ephemeral1");
}

// ============================================================================
// Anchor convention (docs/design/render-anchor-convention.md): the field NAME carries the anchor.
//   cx,cy = CENTER (sprite, particle, sector) ; x,y = top-left CORNER (rect).
// Locks each primitive's final instance position so a silent anchor drift fails LOUD (the exact
// footgun this convention kills). The cx,cy cases are RED before the SceneCollector supports cx,cy;
// the rect/sector cases GUARD the already-correct corner/center primitives.
// ============================================================================
TEST_CASE("SceneCollector - anchor: cx,cy = center, x,y = corner", "[scene_collector][anchor]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // render:sprite (ephemeral) — cx,cy is the CENTER, passed straight to the instance position.
    {
        auto s = std::make_unique<JsonDataNode>("s");
        s->setDouble("cx", 100.0);
        s->setDouble("cy", 200.0);
        s->setDouble("scaleX", 32.0);
        s->setDouble("scaleY", 32.0);
        fx.ioPublisher->publish("render:sprite", std::move(s));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.spriteCount == 1);
        REQUIRE_THAT(p.sprites[0].x, WithinAbs(100.0f, 0.01f));
        REQUIRE_THAT(p.sprites[0].y, WithinAbs(200.0f, 0.01f));
    }
    fx.collector.clear();

    // render:sprite:add (retained) — cx,cy center.
    {
        auto s = std::make_unique<JsonDataNode>("s");
        s->setInt("renderId", 42);
        s->setDouble("cx", 10.0);
        s->setDouble("cy", 20.0);
        fx.ioPublisher->publish("render:sprite:add", std::move(s));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.spriteCount == 1);
        REQUIRE_THAT(p.sprites[0].x, WithinAbs(10.0f, 0.01f));
        REQUIRE_THAT(p.sprites[0].y, WithinAbs(20.0f, 0.01f));
    }
    fx.collector.clear();
    {   // drop the retained sprite so it doesn't leak into the next cases
        auto r = std::make_unique<JsonDataNode>("r");
        r->setInt("renderId", 42);
        fx.ioPublisher->publish("render:sprite:remove", std::move(r));
        fx.pump();
    }

    // render:particle — cx,cy center (a particle is a point; center is its only sensible anchor).
    {
        auto pa = std::make_unique<JsonDataNode>("p");
        pa->setDouble("cx", 55.0);
        pa->setDouble("cy", 66.0);
        pa->setDouble("size", 4.0);
        fx.ioPublisher->publish("render:particle", std::move(pa));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.particleCount == 1);
        REQUIRE_THAT(p.particles[0].x, WithinAbs(55.0f, 0.01f));
        REQUIRE_THAT(p.particles[0].y, WithinAbs(66.0f, 0.01f));
    }
    fx.collector.clear();

    // render:rect — x,y = top-left CORNER; the tinted sprite's CENTER = corner + half-extent (GUARD).
    {
        auto r = std::make_unique<JsonDataNode>("r");
        r->setDouble("x", 10.0);
        r->setDouble("y", 20.0);
        r->setDouble("w", 40.0);
        r->setDouble("h", 60.0);
        fx.ioPublisher->publish("render:rect", std::move(r));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.spriteCount == 1);
        REQUIRE_THAT(p.sprites[0].x, WithinAbs(30.0f, 0.01f));   // 10 + 40/2
        REQUIRE_THAT(p.sprites[0].y, WithinAbs(50.0f, 0.01f));   // 20 + 60/2
    }
    fx.collector.clear();

    // render:sector — cx,cy = center (THE MODEL this convention generalizes; GUARD).
    {
        auto se = std::make_unique<JsonDataNode>("s");
        se->setDouble("cx", 7.0);
        se->setDouble("cy", 8.0);
        se->setDouble("r1", 5.0);
        fx.ioPublisher->publish("render:sector", std::move(se));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.sectorCount == 1);
        REQUIRE_THAT(p.sectors[0].cx, WithinAbs(7.0f, 0.01f));
        REQUIRE_THAT(p.sectors[0].cy, WithinAbs(8.0f, 0.01f));
    }
}

// The legacy x,y anchor is RETIRED on center primitives: a sprite/particle published with x,y but
// no cx,cy is DROPPED (loud one-shot log), never silently shifted by half a footprint. Locks the
// hard reject (echec franc, doctrine) — see docs/design/render-anchor-convention.md.
TEST_CASE("SceneCollector - anchor: legacy x,y on sprite/particle is rejected", "[scene_collector][anchor][reject]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // render:sprite with legacy x,y (no cx,cy) -> dropped, not shifted.
    {
        auto s = std::make_unique<JsonDataNode>("s");
        s->setDouble("x", 100.0);
        s->setDouble("y", 200.0);
        fx.ioPublisher->publish("render:sprite", std::move(s));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.spriteCount == 0);
    }
    fx.collector.clear();

    // render:sprite:add with legacy x,y -> nothing retained.
    {
        auto s = std::make_unique<JsonDataNode>("s");
        s->setInt("renderId", 9);
        s->setDouble("x", 10.0);
        s->setDouble("y", 20.0);
        fx.ioPublisher->publish("render:sprite:add", std::move(s));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.spriteCount == 0);
    }
    fx.collector.clear();

    // render:particle with legacy x,y -> dropped.
    {
        auto pa = std::make_unique<JsonDataNode>("p");
        pa->setDouble("x", 5.0);
        pa->setDouble("y", 6.0);
        fx.ioPublisher->publish("render:particle", std::move(pa));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.particleCount == 0);
    }
    fx.collector.clear();

    // Sanity: the canonical cx,cy still renders (the reject is specific to legacy x,y).
    {
        auto s = std::make_unique<JsonDataNode>("s");
        s->setDouble("cx", 100.0);
        s->setDouble("cy", 200.0);
        fx.ioPublisher->publish("render:sprite", std::move(s));
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.spriteCount == 1);
        REQUIRE_THAT(p.sprites[0].x, WithinAbs(100.0f, 0.01f));
    }
}

// A RETAINED widget tagged space:"screen" (how the UIModule renders) must land in the HUD bucket and stay
// FIXED on screen while render:camera pans/zooms — the engine fix that makes a HUD-over-map possible.
// Without the retained HUD bucket it would go world-space and drift with the terrain. Locks the fix.
TEST_CASE("SceneCollector - retained HUD: a screen-space widget is camera-immune", "[scene_collector][retained][hud]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // A retained sprite tagged screen-space (HUD) + a retained WORLD sprite (no space) for contrast.
    {
        auto s = std::make_unique<JsonDataNode>("s");
        s->setInt("renderId", 7); s->setString("space", "screen");
        s->setDouble("cx", 100.0); s->setDouble("cy", 50.0);
        fx.ioPublisher->publish("render:sprite:add", std::move(s));
    }
    {
        auto s = std::make_unique<JsonDataNode>("s");
        s->setInt("renderId", 8);                        // no "space" -> world
        s->setDouble("cx", 200.0); s->setDouble("cy", 60.0);
        fx.ioPublisher->publish("render:sprite:add", std::move(s));
    }
    // A retained HUD text too (proves the text path + string plumbing).
    {
        auto t = std::make_unique<JsonDataNode>("t");
        t->setInt("renderId", 9); t->setString("space", "screen");
        t->setDouble("x", 12.0); t->setDouble("y", 20.0); t->setString("text", "TEMP 900C");
        fx.ioPublisher->publish("render:text:add", std::move(t));
    }
    fx.pump();

    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.hudSpriteCount == 1);                              // screen-space sprite -> HUD bucket
        REQUIRE_THAT(p.hudSprites[0].x, WithinAbs(100.0f, 0.01f));
        REQUIRE(p.spriteCount == 1);                                 // world sprite stayed world
        REQUIRE_THAT(p.sprites[0].x, WithinAbs(200.0f, 0.01f));
        REQUIRE(p.hudTextCount == 1);                                // screen-space text -> HUD bucket
        REQUIRE(std::string(p.hudTexts[0].text) == "TEMP 900C");
    }

    // Move the camera hard (pan far + zoom in). The HUD must NOT move; the world view must.
    fx.collector.clear();   // retained persists across the frame boundary
    {
        auto cam = std::make_unique<JsonDataNode>("c");
        cam->setDouble("x", 999.0); cam->setDouble("y", 888.0); cam->setDouble("zoom", 4.0);
        fx.ioPublisher->publish("render:camera", std::move(cam));
    }
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.hudSpriteCount == 1);
    REQUIRE_THAT(p.hudSprites[0].x, WithinAbs(100.0f, 0.01f));   // UNCHANGED — screen-space, camera-immune
    REQUIRE(p.hudTextCount == 1);                                // retained HUD text survives the frame too
    // The world view followed the camera; the HUD view is pinned to screen space (origin, zoom 1).
    REQUIRE_THAT(p.mainView.positionX, WithinAbs(999.0f, 0.01f));
    REQUIRE_THAT(p.mainView.zoom, WithinAbs(4.0f, 0.01f));
    REQUIRE_THAT(p.hudView.positionX, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(p.hudView.zoom, WithinAbs(1.0f, 0.01f));
}

// ============================================================================
// Lighting L1 — the global ambient term (render:ambient).
//
// The ambient colour is GLOBAL FRAME STATE, like clearColor and the camera: published once, it
// governs every subsequent frame until it changes. It is NOT an ephemeral primitive, so it must
// survive collector.clear().
//
// `0` is the "unset" sentinel and it is load-bearing, not a default value: it is what tells the
// renderer that lighting is INACTIVE, so it must skip the offscreen targets entirely and draw
// straight to the backbuffer exactly as it does today. Every existing consumer (Drifterra, DAOS,
// Fractax) publishes no ambient — the whole zero-cost bypass hangs off this being 0.
// ============================================================================

TEST_CASE("SceneCollector - ambient: absent means UNSET (lighting inactive)", "[scene_collector][light]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    fx.pump();
    FramePacket p = fx.collector.finalize(allocator);

    // THE non-regression assertion. A non-zero default here would silently switch every existing
    // game onto the lit path — two full-screen targets nobody asked for.
    REQUIRE(p.ambientColor == 0u);
}

TEST_CASE("SceneCollector - ambient: render:ambient sets it and it PERSISTS", "[scene_collector][light]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto amb = std::make_unique<JsonDataNode>("a");
    amb->setInt("color", 0x404060FF);          // a dim blue night
    fx.ioPublisher->publish("render:ambient", std::move(amb));
    fx.pump();

    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.ambientColor == 0x404060FFu);
    }

    // Frame boundary: ambient is global state, not an ephemeral primitive — it must survive, or a
    // game would have to re-publish it every single frame to stay lit.
    fx.collector.clear();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.ambientColor == 0x404060FFu);
    }
}

// ============================================================================
// Lighting L2 — radial lights (render:light).
//
// Lights are EPHEMERAL, like render:sprite and render:particle: a game light almost always follows
// something that moves, so re-publishing each frame is the normal case, not a cost. A retained mode
// for static torches is an optimisation to MEASURE before writing, not to assume.
//
// Deliberately NO bulk path either: the IIO+JSON wall sits around 5k primitives/frame and a lit
// scene has tens of lights. A submitLightBatch would optimise a problem that does not exist.
// ============================================================================

TEST_CASE("SceneCollector - light: render:light lands in the packet with cx,cy as CENTRE",
          "[scene_collector][light]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto l = std::make_unique<JsonDataNode>("l");
    l->setDouble("cx", 120.0);
    l->setDouble("cy", 80.0);
    l->setDouble("radius", 64.0);
    l->setInt("color", 0xFF8000FF);      // orange
    l->setDouble("intensity", 2.0);      // >1 on purpose: the RGBA16F target keeps the overbright
    fx.ioPublisher->publish("render:light", std::move(l));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.lightCount == 1);
    REQUIRE(p.lights != nullptr);
    REQUIRE_THAT(p.lights[0].cx, WithinAbs(120.0f, 0.01f));
    REQUIRE_THAT(p.lights[0].cy, WithinAbs(80.0f, 0.01f));
    REQUIRE_THAT(p.lights[0].radius, WithinAbs(64.0f, 0.01f));
    REQUIRE_THAT(p.lights[0].r, WithinAbs(1.0f, 0.01f));        // 0xFF
    REQUIRE_THAT(p.lights[0].g, WithinAbs(0.502f, 0.01f));      // 0x80
    REQUIRE_THAT(p.lights[0].b, WithinAbs(0.0f, 0.01f));        // 0x00
    REQUIRE_THAT(p.lights[0].intensity, WithinAbs(2.0f, 0.01f));   // NOT clamped to 1
}

TEST_CASE("SceneCollector - light: ephemeral, so it does NOT survive the frame boundary",
          "[scene_collector][light]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto l = std::make_unique<JsonDataNode>("l");
    l->setDouble("cx", 10.0); l->setDouble("cy", 10.0); l->setDouble("radius", 5.0);
    fx.ioPublisher->publish("render:light", std::move(l));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).lightCount == 1);

    // A light left behind for a frame it was not published in would smear a moving lamp across the
    // scene — the exact failure the ephemeral contract exists to prevent.
    fx.collector.clear();
    REQUIRE(fx.collector.finalize(allocator).lightCount == 0);
}

TEST_CASE("SceneCollector - light: no light published means NO light array at all",
          "[scene_collector][light]") {
    RetainedFixture fx;
    FrameAllocator allocator;
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.lightCount == 0);
    REQUIRE(p.lights == nullptr);   // no arena slice claimed for a feature nobody used
}

TEST_CASE("SceneCollector - light: cone params ride through, omni by default",
          "[scene_collector][light][cone]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // A cone, in the grove::fx::Emitter convention (degrees, 90 = screen-down).
    auto l = std::make_unique<JsonDataNode>("l");
    l->setDouble("cx", 50.0); l->setDouble("cy", 50.0); l->setDouble("radius", 30.0);
    l->setDouble("dirDeg", 90.0);
    l->setDouble("spreadDeg", 45.0);
    fx.ioPublisher->publish("render:light", std::move(l));

    // ...and a plain light right after it, with no cone fields at all.
    auto plain = std::make_unique<JsonDataNode>("l");
    plain->setDouble("cx", 10.0); plain->setDouble("cy", 10.0); plain->setDouble("radius", 20.0);
    fx.ioPublisher->publish("render:light", std::move(plain));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.lightCount == 2);
    REQUIRE_THAT(p.lights[0].dirDeg, WithinAbs(90.0f, 0.01f));
    REQUIRE_THAT(p.lights[0].spreadDeg, WithinAbs(45.0f, 0.01f));

    // THE non-regression: a light that says nothing about a cone is a full disc. A default of 0
    // here would silently switch every existing light off.
    REQUIRE_THAT(p.lights[1].spreadDeg, WithinAbs(360.0f, 0.01f));
}

// ============================================================================
// Lighting W1 — opaque occluders (render:occluder).
//
// A wall is not a special case anywhere in the code: it writes transmittance 0 into the occlusion
// map, and a zero annihilates the running product the light march accumulates. Everything beyond it
// on that ray goes dark as an arithmetic consequence, not as a branch.
//
// EPHEMERAL for this slice, like sprites and lights. Retained mode (W3) is where static level
// geometry belongs — and there it will NOT be premature, for the opposite reason lights are
// ephemeral: a wall does not move, so re-publishing it every frame would charge a cost proportional
// to the size of the level for a constant.
// ============================================================================

TEST_CASE("SceneCollector - occluder: x,y is the CORNER, not the centre",
          "[scene_collector][light][occluder]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto o = std::make_unique<JsonDataNode>("o");
    o->setDouble("x", 40.0);
    o->setDouble("y", 90.0);
    o->setDouble("w", 20.0);
    o->setDouble("h", 60.0);
    fx.ioPublisher->publish("render:occluder", std::move(o));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.occluderCount == 1);
    REQUIRE(p.occluders != nullptr);

    // A rect's anchor is its CORNER — the field name carries it, and a light's cx,cy carries the
    // other. Getting this wrong would shift every wall by half its size, which reads as "the shadows
    // are offset" rather than as an anchor mistake.
    REQUIRE_THAT(p.occluders[0].x, WithinAbs(40.0f, 0.01f));
    REQUIRE_THAT(p.occluders[0].y, WithinAbs(90.0f, 0.01f));
    REQUIRE_THAT(p.occluders[0].w, WithinAbs(20.0f, 0.01f));
    REQUIRE_THAT(p.occluders[0].h, WithinAbs(60.0f, 0.01f));
}

TEST_CASE("SceneCollector - occluder: a degenerate rect is dropped, not drawn",
          "[scene_collector][light][occluder]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // Zero or negative extent occludes nothing; keeping it would cost a quad per frame to draw
    // nothing, and would let a caller's uninitialised struct silently reach the GPU.
    const double sizes[3][2] = { { 0.0, 10.0 }, { 10.0, 0.0 }, { -5.0, 10.0 } };
    for (const auto& wh : sizes) {
        auto o = std::make_unique<JsonDataNode>("o");
        o->setDouble("x", 0.0); o->setDouble("y", 0.0);
        o->setDouble("w", wh[0]); o->setDouble("h", wh[1]);
        fx.ioPublisher->publish("render:occluder", std::move(o));
    }
    fx.pump();

    REQUIRE(fx.collector.finalize(allocator).occluderCount == 0);
}

TEST_CASE("SceneCollector - occluder: ephemeral, and absent means NO array",
          "[scene_collector][light][occluder]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    fx.pump();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.occluderCount == 0);
        REQUIRE(p.occluders == nullptr);   // no arena slice for a feature nobody used
    }

    auto o = std::make_unique<JsonDataNode>("o");
    o->setDouble("x", 1.0); o->setDouble("y", 2.0); o->setDouble("w", 3.0); o->setDouble("h", 4.0);
    fx.ioPublisher->publish("render:occluder", std::move(o));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).occluderCount == 1);

    fx.collector.clear();
    REQUIRE(fx.collector.finalize(allocator).occluderCount == 0);
}

// ============================================================================
// Lighting W3 — RETAINED occluders (render:occluder:add / :update / :remove).
//
// The opposite reasoning to lights, and deliberately so. A light almost always follows something
// that moves, so re-publishing it every frame is its normal case. A WALL does not move: publishing
// the level's geometry every frame would charge a cost proportional to the size of the level for a
// constant. Retained mode here is not premature — it is the shape the data actually has.
// ============================================================================

TEST_CASE("SceneCollector - occluder retained: add PERSISTS across frames",
          "[scene_collector][light][occluder][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto a = std::make_unique<JsonDataNode>("o");
    a->setInt("renderId", 7);
    a->setDouble("x", 10.0); a->setDouble("y", 20.0);
    a->setDouble("w", 30.0); a->setDouble("h", 40.0);
    fx.ioPublisher->publish("render:occluder:add", std::move(a));
    fx.pump();

    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.occluderCount == 1);
        REQUIRE_THAT(p.occluders[0].x, WithinAbs(10.0f, 0.01f));
    }

    // The whole point: WITHOUT re-publishing, the wall is still there next frame.
    fx.collector.clear();
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.occluderCount == 1);
        REQUIRE_THAT(p.occluders[0].w, WithinAbs(30.0f, 0.01f));
    }
}

TEST_CASE("SceneCollector - occluder retained: update PRESERVES unspecified fields",
          "[scene_collector][light][occluder][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto a = std::make_unique<JsonDataNode>("o");
    a->setInt("renderId", 3);
    a->setDouble("x", 100.0); a->setDouble("y", 200.0);
    a->setDouble("w", 50.0);  a->setDouble("h", 60.0);
    fx.ioPublisher->publish("render:occluder:add", std::move(a));
    fx.pump();

    // Move it without restating its size. A door that slides must not have to repeat its extent —
    // and an update that silently reset the omitted fields to zero would DELETE the wall while
    // looking like a move.
    auto u = std::make_unique<JsonDataNode>("o");
    u->setInt("renderId", 3);
    u->setDouble("x", 150.0);
    fx.ioPublisher->publish("render:occluder:update", std::move(u));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.occluderCount == 1);
    REQUIRE_THAT(p.occluders[0].x, WithinAbs(150.0f, 0.01f));   // moved
    REQUIRE_THAT(p.occluders[0].y, WithinAbs(200.0f, 0.01f));   // kept
    REQUIRE_THAT(p.occluders[0].w, WithinAbs(50.0f, 0.01f));    // kept
    REQUIRE_THAT(p.occluders[0].h, WithinAbs(60.0f, 0.01f));    // kept
}

TEST_CASE("SceneCollector - occluder retained: remove deletes it",
          "[scene_collector][light][occluder][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto a = std::make_unique<JsonDataNode>("o");
    a->setInt("renderId", 9);
    a->setDouble("x", 0.0); a->setDouble("y", 0.0);
    a->setDouble("w", 10.0); a->setDouble("h", 10.0);
    fx.ioPublisher->publish("render:occluder:add", std::move(a));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).occluderCount == 1);

    auto r = std::make_unique<JsonDataNode>("o");
    r->setInt("renderId", 9);
    fx.ioPublisher->publish("render:occluder:remove", std::move(r));
    fx.pump();

    // A destroyed wall must stop casting its shadow. Leaving it would be the exact mirror of the
    // orphaned-sprite hazard the FX layer had to solve at hot-reload.
    REQUIRE(fx.collector.finalize(allocator).occluderCount == 0);
}

TEST_CASE("SceneCollector - occluder: retained and ephemeral BOTH reach the packet",
          "[scene_collector][light][occluder][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto a = std::make_unique<JsonDataNode>("o");
    a->setInt("renderId", 1);
    a->setDouble("x", 0.0); a->setDouble("y", 0.0); a->setDouble("w", 5.0); a->setDouble("h", 5.0);
    fx.ioPublisher->publish("render:occluder:add", std::move(a));

    auto e = std::make_unique<JsonDataNode>("o");
    e->setDouble("x", 90.0); e->setDouble("y", 90.0); e->setDouble("w", 5.0); e->setDouble("h", 5.0);
    fx.ioPublisher->publish("render:occluder", std::move(e));
    fx.pump();

    // The two modes coexist: static level geometry retained, a moving shutter ephemeral. Neither is
    // an error, and a scene mixing them must occlude with both.
    REQUIRE(fx.collector.finalize(allocator).occluderCount == 2);
}

TEST_CASE("SceneCollector - occluder retained: renderId 0 is rejected",
          "[scene_collector][light][occluder][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // 0 is the "no id" value, so accepting it would give every unidentified wall the SAME slot —
    // each add silently replacing the last. Same guard as every other retained primitive.
    auto a = std::make_unique<JsonDataNode>("o");
    a->setInt("renderId", 0);
    a->setDouble("x", 1.0); a->setDouble("y", 1.0); a->setDouble("w", 9.0); a->setDouble("h", 9.0);
    fx.ioPublisher->publish("render:occluder:add", std::move(a));
    fx.pump();

    REQUIRE(fx.collector.finalize(allocator).occluderCount == 0);
}

TEST_CASE("SceneCollector - sprite blend: additive rides through, absent = alpha",
          "[scene_collector][blend]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto glow = std::make_unique<JsonDataNode>("s");
    glow->setDouble("cx", 10.0); glow->setDouble("cy", 10.0);
    glow->setString("blend", "additive");
    fx.ioPublisher->publish("render:sprite", std::move(glow));

    auto plain = std::make_unique<JsonDataNode>("s");
    plain->setDouble("cx", 50.0); plain->setDouble("cy", 50.0);
    fx.ioPublisher->publish("render:sprite", std::move(plain));

    // A retained sprite glows too: a plume held by renderId is a normal case, not an exception.
    auto held = std::make_unique<JsonDataNode>("s");
    held->setInt("renderId", 42);
    held->setDouble("cx", 90.0); held->setDouble("cy", 90.0);
    held->setString("blend", "additive");
    fx.ioPublisher->publish("render:sprite:add", std::move(held));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.spriteCount == 3);

    int additive = 0, alpha = 0;
    for (size_t i = 0; i < p.spriteCount; ++i) {
        if (p.sprites[i].padding0 > 0.5f) ++additive; else ++alpha;
    }
    // THE non-regression: a sprite that says nothing about blend stays alpha. A default of additive
    // would have quietly turned every existing scene into a glow.
    REQUIRE(additive == 2);
    REQUIRE(alpha == 1);

    // An unknown mode falls back to alpha rather than to something surprising.
    fx.collector.clear();
    auto weird = std::make_unique<JsonDataNode>("s");
    weird->setDouble("cx", 1.0); weird->setDouble("cy", 1.0);
    weird->setString("blend", "screen");
    fx.ioPublisher->publish("render:sprite", std::move(weird));
    fx.pump();
    FramePacket q = fx.collector.finalize(allocator);
    bool sawEphemeralAlpha = false;
    for (size_t i = 0; i < q.spriteCount; ++i) {
        if (q.sprites[i].x > 0.5f && q.sprites[i].x < 1.5f) sawEphemeralAlpha = (q.sprites[i].padding0 < 0.5f);
    }
    REQUIRE(sawEphemeralAlpha);
}

// ============================================================================
// Lighting F1 — coloured filters (render:filter).
//
// A wall writes (0,0,0) into the occlusion map. A FILTER writes a COLOUR — and that is the entire
// difference. The socle does not change by a line: the same running product that annihilates
// everything behind a wall tints everything behind a stained-glass window.
//
// The one decision this slice had to make is what `color` MEANS. The map stores transmittance PER
// UNIT of length (fog demands it — a thicker cloud must absorb more), but an author writing a window
// states the tint they want to SEE behind it. So the collector converts, keyed on the pane's THIN
// axis: crossing min(w,h) perpendicularly yields exactly `color`, and crossing at an angle tints
// more because the ray travelled further inside the glass.
// ============================================================================

#include <grove/light/Transmittance.h>

TEST_CASE("SceneCollector - filter: `color` is the tint after ONE perpendicular crossing",
          "[scene_collector][light][filter]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // A wide, thin pane: 200x10, so the thin axis — the one a ray crosses head-on — is 10.
    auto f = std::make_unique<JsonDataNode>("f");
    f->setDouble("x", 40.0);
    f->setDouble("y", 90.0);
    f->setDouble("w", 200.0);
    f->setDouble("h", 10.0);
    f->setInt("color", static_cast<int>(0xFF4D4DFFu));   // RRGGBBAA — red glass: 1.0, 0.302, 0.302
    fx.ioPublisher->publish("render:filter", std::move(f));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.filterCount == 1);
    REQUIRE(p.filters != nullptr);

    REQUIRE_THAT(p.filters[0].x, WithinAbs(40.0f, 0.01f));   // x,y = CORNER, like every rect
    REQUIRE_THAT(p.filters[0].y, WithinAbs(90.0f, 0.01f));

    // The packet holds PER-UNIT transmittance, so the assertion has to travel the pane to mean
    // anything. Asserting the stored value directly would pin the conversion to itself.
    using grove::light::transmitThrough;
    REQUIRE_THAT(transmitThrough(p.filters[0].r, 10.0f), Catch::Matchers::WithinRel(1.0f, 1e-3f));
    REQUIRE_THAT(transmitThrough(p.filters[0].g, 10.0f), Catch::Matchers::WithinRel(0.302f, 1e-2f));
    REQUIRE_THAT(transmitThrough(p.filters[0].b, 10.0f), Catch::Matchers::WithinRel(0.302f, 1e-2f));

    // THE discriminant of this whole chantier: a filter TINTS, it does not merely darken. A grey
    // pane would satisfy every luminance-based check while proving nothing, so the assertion has to
    // be that the channels DIVERGE — red survives untouched where green and blue collapse.
    REQUIRE(p.filters[0].r > p.filters[0].g + 0.05f);
    REQUIRE_THAT(p.filters[0].r, WithinAbs(1.0f, 1e-6f));   // vacuum stays EXACTLY vacuum
}

TEST_CASE("SceneCollector - filter: `opacity` runs from no effect to full tint",
          "[scene_collector][light][filter]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // opacity 0 must be a true no-op, not "nearly nothing": the author's guard rail against the
    // brutality of a multiplicative model has to have a neutral end, or dialling a filter down
    // would still leave the scene slightly darker than having no filter at all.
    auto off = std::make_unique<JsonDataNode>("f");
    off->setDouble("x", 0.0); off->setDouble("y", 0.0);
    off->setDouble("w", 50.0); off->setDouble("h", 50.0);
    off->setInt("color", static_cast<int>(0xFF0000FFu));   // pure red: green and blue at ZERO
    off->setDouble("opacity", 0.0);
    fx.ioPublisher->publish("render:filter", std::move(off));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.filterCount == 1);
    REQUIRE_THAT(p.filters[0].r, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(p.filters[0].g, WithinAbs(1.0f, 1e-6f));   // untinted, exactly
    REQUIRE_THAT(p.filters[0].b, WithinAbs(1.0f, 1e-6f));
    fx.collector.clear();

    // ...and at full opacity the same pane blocks green outright. A zero channel IS a wall for that
    // channel — the degenerate case of the same mechanism, which is the point of the socle.
    auto full = std::make_unique<JsonDataNode>("f");
    full->setDouble("x", 0.0); full->setDouble("y", 0.0);
    full->setDouble("w", 50.0); full->setDouble("h", 50.0);
    full->setInt("color", static_cast<int>(0xFF0000FFu));
    full->setDouble("opacity", 1.0);
    fx.ioPublisher->publish("render:filter", std::move(full));
    fx.pump();

    FramePacket p2 = fx.collector.finalize(allocator);
    REQUIRE(p2.filterCount == 1);
    REQUIRE_THAT(p2.filters[0].r, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(p2.filters[0].g, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("SceneCollector - filter: ephemeral, degenerate dropped, absent means NO array",
          "[scene_collector][light][filter]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    {   // A feature nobody used claims no arena slice — the zero-cost bypass every consumer relies on.
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.filterCount == 0);
        REQUIRE(p.filters == nullptr);
    }

    // A rect with no extent filters nothing, and its thin axis is 0 — which is precisely the
    // thickness the conversion cannot invert. Dropping it here is what keeps that division safe.
    auto bad = std::make_unique<JsonDataNode>("f");
    bad->setDouble("x", 10.0); bad->setDouble("y", 10.0);
    bad->setDouble("w", 0.0);  bad->setDouble("h", 40.0);
    bad->setInt("color", static_cast<int>(0xFF0000FFu));
    fx.ioPublisher->publish("render:filter", std::move(bad));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).filterCount == 0);

    auto ok = std::make_unique<JsonDataNode>("f");
    ok->setDouble("x", 10.0); ok->setDouble("y", 10.0);
    ok->setDouble("w", 40.0); ok->setDouble("h", 40.0);
    ok->setInt("color", static_cast<int>(0x80FF80FFu));
    fx.ioPublisher->publish("render:filter", std::move(ok));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).filterCount == 1);

    // Ephemeral like lights: silence for one frame means the pane is gone.
    fx.collector.clear();
    REQUIRE(fx.collector.finalize(allocator).filterCount == 0);
}

// ============================================================================
// Lighting F3 — RETAINED filters (render:filter:add / :update / :remove).
//
// Same reasoning as the retained occluders: a stained-glass window does not move, so re-publishing
// it every frame would charge a cost proportional to the size of the level for a constant.
//
// One trap is specific to filters, and it does not exist for walls: the packet carries the CONVERTED
// per-unit transmittance, and that conversion depends on the pane's thickness. An update that
// changes w or h therefore has to RE-derive it — otherwise a window resized at runtime keeps a
// per-unit value computed for its old thickness, and its tint drifts with no message to blame.
// ============================================================================

TEST_CASE("SceneCollector - filter retained: add PERSISTS across frames",
          "[scene_collector][light][filter][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto a = std::make_unique<JsonDataNode>("f");
    a->setInt("renderId", 7);
    a->setDouble("x", 10.0); a->setDouble("y", 20.0);
    a->setDouble("w", 60.0); a->setDouble("h", 10.0);
    a->setInt("color", static_cast<int>(0x33FF33FFu));   // green glass
    fx.ioPublisher->publish("render:filter:add", std::move(a));
    fx.pump();

    for (int frame = 0; frame < 3; ++frame) {
        fx.collector.clear();
        fx.pump();
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.filterCount == 1);
        REQUIRE_THAT(p.filters[0].x, WithinAbs(10.0f, 0.01f));
        // Still green, three frames after the single message that said so.
        REQUIRE(p.filters[0].g > p.filters[0].r + 0.05f);
    }
}

TEST_CASE("SceneCollector - filter retained: resizing RE-DERIVES the tint",
          "[scene_collector][light][filter][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    // A thin pane, tinted to 0.2 in green across its 4-unit thin axis.
    auto a = std::make_unique<JsonDataNode>("f");
    a->setInt("renderId", 9);
    a->setDouble("x", 0.0); a->setDouble("y", 0.0);
    a->setDouble("w", 4.0); a->setDouble("h", 100.0);
    a->setInt("color", static_cast<int>(0xFF33FFFFu));   // magenta: green is the eaten channel
    fx.ioPublisher->publish("render:filter:add", std::move(a));
    fx.pump();

    using grove::light::transmitThrough;
    {
        FramePacket p = fx.collector.finalize(allocator);
        REQUIRE(p.filterCount == 1);
        REQUIRE_THAT(transmitThrough(p.filters[0].g, 4.0f), Catch::Matchers::WithinRel(0.2f, 2e-2f));
    }
    fx.collector.clear();

    // Now make it four times thicker, saying nothing about its colour.
    auto u = std::make_unique<JsonDataNode>("f");
    u->setInt("renderId", 9);
    u->setDouble("w", 16.0);
    fx.ioPublisher->publish("render:filter:update", std::move(u));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.filterCount == 1);
    REQUIRE_THAT(p.filters[0].w, WithinAbs(16.0f, 0.01f));

    // The stated tint has NOT changed, so crossing the (new) thin axis must still yield it. A stored
    // per-unit value carried over unchanged would instead darken by a power of four — the window
    // would go nearly black on being widened, and nothing would say why.
    REQUIRE_THAT(transmitThrough(p.filters[0].g, 16.0f), Catch::Matchers::WithinRel(0.2f, 2e-2f));
}

TEST_CASE("SceneCollector - filter retained: update PRESERVES unspecified fields, remove deletes",
          "[scene_collector][light][filter][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto a = std::make_unique<JsonDataNode>("f");
    a->setInt("renderId", 3);
    a->setDouble("x", 100.0); a->setDouble("y", 200.0);
    a->setDouble("w", 50.0);  a->setDouble("h", 60.0);
    a->setInt("color", static_cast<int>(0xFF3333FFu));
    fx.ioPublisher->publish("render:filter:add", std::move(a));
    fx.pump();
    fx.collector.clear();

    // A sliding pane must be able to move without restating its extent or its colour — an update
    // that reset the omitted fields would DELETE the window while looking like a move.
    auto u = std::make_unique<JsonDataNode>("f");
    u->setInt("renderId", 3);
    u->setDouble("x", 150.0);
    fx.ioPublisher->publish("render:filter:update", std::move(u));
    fx.pump();

    FramePacket p = fx.collector.finalize(allocator);
    REQUIRE(p.filterCount == 1);
    REQUIRE_THAT(p.filters[0].x, WithinAbs(150.0f, 0.01f));   // moved
    REQUIRE_THAT(p.filters[0].y, WithinAbs(200.0f, 0.01f));   // kept
    REQUIRE_THAT(p.filters[0].w, WithinAbs(50.0f, 0.01f));    // kept
    // Still red glass. Asserted on the TRANSMITTED tint, not on the raw per-unit values: those
    // crowd towards 1 as a pane thickens (0.2 over 50 units is 0.9683 per unit), so a fixed margin
    // between channels would be a statement about the pane's size rather than about its colour.
    REQUIRE_THAT(grove::light::transmitThrough(p.filters[0].r, 50.0f), Catch::Matchers::WithinRel(1.0f, 1e-3f));
    REQUIRE_THAT(grove::light::transmitThrough(p.filters[0].g, 50.0f), Catch::Matchers::WithinRel(0.2f, 2e-2f));
    fx.collector.clear();

    // Updating something absent is a no-op, not an add.
    auto ghost = std::make_unique<JsonDataNode>("f");
    ghost->setInt("renderId", 999);
    ghost->setDouble("x", 5.0);
    fx.ioPublisher->publish("render:filter:update", std::move(ghost));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).filterCount == 1);
    fx.collector.clear();

    // A destroyed window must stop tinting — the mirror of the orphaned-sprite hazard.
    auto rm = std::make_unique<JsonDataNode>("f");
    rm->setInt("renderId", 3);
    fx.ioPublisher->publish("render:filter:remove", std::move(rm));
    fx.pump();
    REQUIRE(fx.collector.finalize(allocator).filterCount == 0);
}

TEST_CASE("SceneCollector - filter retained and ephemeral COEXIST",
          "[scene_collector][light][filter][retained]") {
    RetainedFixture fx;
    FrameAllocator allocator;

    auto a = std::make_unique<JsonDataNode>("f");
    a->setInt("renderId", 1);
    a->setDouble("w", 20.0); a->setDouble("h", 20.0);
    a->setInt("color", static_cast<int>(0xFF3333FFu));
    fx.ioPublisher->publish("render:filter:add", std::move(a));
    fx.pump();
    fx.collector.clear();

    auto e = std::make_unique<JsonDataNode>("f");
    e->setDouble("x", 500.0); e->setDouble("w", 20.0); e->setDouble("h", 20.0);
    e->setInt("color", static_cast<int>(0x3333FFFFu));
    fx.ioPublisher->publish("render:filter", std::move(e));
    fx.pump();

    // Neither mode is an error, and a scene mixing them tints with both. Order is irrelevant:
    // occlusion is a product, and a product does not care which factor came first.
    REQUIRE(fx.collector.finalize(allocator).filterCount == 2);
}
