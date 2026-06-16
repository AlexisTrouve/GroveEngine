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

#include <memory>
#include <chrono>
#include <sstream>

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
    add->setDouble("x", 10.0);
    add->setDouble("y", 20.0);
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
    add->setDouble("x", 10.0);
    add->setInt("color", static_cast<int>(0xFF0000FF));
    fx.ioPublisher->publish("render:sprite:add", std::move(add));
    fx.pump();

    // Update ONLY x — no color field. Color must be PRESERVED (red), like x/y/scale are.
    auto upd = std::make_unique<JsonDataNode>("s");
    upd->setInt("renderId", 3);
    upd->setDouble("x", 20.0);
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
    add->setDouble("x", 10.0);
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
    sprite->setDouble("x", 100.0);
    sprite->setDouble("y", 200.0);
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

TEST_CASE("SceneCollector - parse multiple sprites", "[scene_collector][integration]") {
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));
    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Publish multiple sprites
    auto sprite1 = std::make_unique<JsonDataNode>("sprite");
    sprite1->setDouble("x", 10.0);
    sprite1->setDouble("y", 20.0);
    sprite1->setInt("color", 0xFFFFFFFF);
    ioPublisher->publish("render:sprite", std::move(sprite1));

    auto sprite2 = std::make_unique<JsonDataNode>("sprite");
    sprite2->setDouble("x", 30.0);
    sprite2->setDouble("y", 40.0);
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
    particle->setDouble("x", 10.0);
    particle->setDouble("y", 20.0);
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
        sprite->setDouble("x", i * 10.0);
        sprite->setDouble("y", i * 20.0);
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
    sprite->setDouble("x", 10.0);
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
        sprite->setDouble("x", 100.0);
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
        sprite1->setDouble("x", 200.0);
        ioPublisher->publish("render:sprite", std::move(sprite1));

        auto sprite2 = std::make_unique<JsonDataNode>("sprite");
        sprite2->setDouble("x", 300.0);
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
    retained->setDouble("x", 20.0);
    retained->setInt("layer", 2);
    ioPublisher->publish("render:sprite:add", std::move(retained));

    // Ephemeral sprites published in DESCENDING layer order (5, 1, 3) — so the raw
    // (unsorted) packet order [retained 2, 5, 1, 3] is NOT layer-sorted.
    const int layers[] = {5, 1, 3};
    const double xs[]   = {50.0, 10.0, 30.0};
    for (int i = 0; i < 3; ++i) {
        auto s = std::make_unique<JsonDataNode>("sprite");
        s->setDouble("x", xs[i]);
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
    sprite->setDouble("x", 10.0);
    ioPublisher->publish("render:sprite", std::move(sprite));

    auto text = std::make_unique<JsonDataNode>("text");
    text->setString("text", "Test");
    ioPublisher->publish("render:text", std::move(text));

    auto particle = std::make_unique<JsonDataNode>("particle");
    particle->setDouble("x", 5.0);
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
