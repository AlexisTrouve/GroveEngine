/**
 * Integration Test: render:nineslice:{add,update,remove} through the SceneCollector.
 *
 * WHAT : proves the renderer-side half of the 9-slice feature END-TO-END through a real IntraIO — ONE
 *        render:nineslice:add message expands into the expected retained SpriteInstances (9 for a large box,
 *        fewer when a band collapses), lands in the WORLD bucket by default and the HUD bucket under
 *        space:"screen", each quad carries the composed UV, an :update re-expands in place, and :remove drops
 *        every child. This is the "prove it bites" E2E the doctrine demands for a GPU-adjacent path (the pure
 *        geometry is already unit-locked by NineSliceUnit; here we lock the IIO wiring + bucket routing +
 *        SpriteInstance construction that no code-reading can vouch for).
 *
 * WHY  : the widget layer (UIButton/UIWindow) rides on exactly this contract; if the expansion drops a quad,
 *        routes to the wrong bucket, or mis-builds the sprite, textured borders break silently. This test is
 *        the regression lock on the primitive independent of any widget.
 *
 * HOW  : the same publisher+collector-over-IntraIO harness as test_scene_collector.cpp; assert on the
 *        finalized FramePacket's sprite arrays. Numeric textureId (atlas UV = identity [0,1]) so the assertions
 *        read the raw slice UVs directly.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../../modules/BgfxRenderer/Scene/SceneCollector.h"
#include "../../modules/BgfxRenderer/Frame/FrameAllocator.h"
#include "grove/IntraIO.h"
#include "grove/IntraIOManager.h"
#include "grove/JsonDataNode.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <vector>
#include <algorithm>

using namespace grove;
using Catch::Matchers::WithinAbs;

namespace {
std::string uid(const std::string& p) {
    auto n = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream o; o << p << "_" << n; return o.str();
}

// Publisher + collector wired through a real IntraIO (mirrors test_scene_collector.cpp).
struct Fixture {
    std::shared_ptr<IntraIO> ioCollector, ioPublisher;
    SceneCollector collector;
    Fixture() {
        auto& mgr = IntraIOManager::getInstance();
        ioCollector = mgr.createInstance(uid("ns_rcv"));
        ioPublisher = mgr.createInstance(uid("ns_pub"));
        collector.setup(ioCollector.get());
    }
    void pump() { collector.collect(ioCollector.get(), 0.016f); }

    // Publish a nineslice message on `topic`, filling a 32x32 source / uniform-8 patch over the given box.
    void publishBox(const std::string& topic, int renderId, double x, double y, double w, double h,
                    bool screen, uint32_t color = 0xFFFFFFFFu) {
        auto n = std::make_unique<JsonDataNode>("ns");
        n->setInt("renderId", renderId);
        n->setDouble("x", x); n->setDouble("y", y); n->setDouble("w", w); n->setDouble("h", h);
        n->setDouble("srcW", 32.0); n->setDouble("srcH", 32.0);
        n->setDouble("left", 8.0); n->setDouble("right", 8.0);
        n->setDouble("top", 8.0);  n->setDouble("bottom", 8.0);
        n->setInt("textureId", 5);
        n->setInt("color", static_cast<int>(color));
        n->setInt("layer", 1000);
        if (screen) n->setString("space", "screen");
        ioPublisher->publish(topic, std::move(n));
    }
};

// Collect the world sprites of a finalized packet into a vector (copies out; packet memory is transient).
std::vector<SpriteInstance> worldSprites(const FramePacket& p) {
    return std::vector<SpriteInstance>(p.sprites, p.sprites + p.spriteCount);
}
} // namespace

TEST_CASE("render:nineslice:add expands to 9 world sprites with composed geometry", "[nineslice][scene_collector]") {
    Fixture fx; FrameAllocator alloc;
    // Box 128x64 at (100,200), corners 8px -> full 3x3 grid (matches NineSliceUnit's first case).
    fx.publishBox("render:nineslice:add", 42, 100.0, 200.0, 128.0, 64.0, /*screen=*/false);
    fx.pump();

    FramePacket p = fx.collector.finalize(alloc);
    REQUIRE(p.spriteCount == 9);
    REQUIRE(p.hudSpriteCount == 0);   // no space:"screen" -> world bucket

    auto s = worldSprites(p);
    // Sprites carry a CENTRE (x,y) + scale; find the top-left corner quad = the 8x8 at centre (104,204).
    auto corner = std::find_if(s.begin(), s.end(), [](const SpriteInstance& q){
        return std::abs(q.x - 104.0f) < 0.01f && std::abs(q.y - 204.0f) < 0.01f; });
    REQUIRE(corner != s.end());
    REQUIRE_THAT(corner->scaleX, WithinAbs(8.0f, 0.01f));
    REQUIRE_THAT(corner->scaleY, WithinAbs(8.0f, 0.01f));
    REQUIRE_THAT(corner->u0, WithinAbs(0.0f, 1e-5));   // identity atlas -> raw slice UV
    REQUIRE_THAT(corner->u1, WithinAbs(0.25f, 1e-5));  // 8/32
    REQUIRE_THAT(corner->textureId, WithinAbs(5.0f, 0.01f));

    // The centre quad: 112x48 stretched, centred at (100+64, 200+32) = (164,232), UV 0.25..0.75.
    auto centre = std::find_if(s.begin(), s.end(), [](const SpriteInstance& q){
        return std::abs(q.scaleX - 112.0f) < 0.01f && std::abs(q.scaleY - 48.0f) < 0.01f; });
    REQUIRE(centre != s.end());
    REQUIRE_THAT(centre->x, WithinAbs(164.0f, 0.01f));
    REQUIRE_THAT(centre->y, WithinAbs(232.0f, 0.01f));
    REQUIRE_THAT(centre->u0, WithinAbs(0.25f, 1e-5));
    REQUIRE_THAT(centre->u1, WithinAbs(0.75f, 1e-5));
}

TEST_CASE("render:nineslice:add with space:screen routes to the HUD bucket", "[nineslice][scene_collector]") {
    Fixture fx; FrameAllocator alloc;
    fx.publishBox("render:nineslice:add", 7, 0.0, 0.0, 200.0, 120.0, /*screen=*/true);
    fx.pump();

    FramePacket p = fx.collector.finalize(alloc);
    REQUIRE(p.spriteCount == 0);       // nothing in the world bucket
    REQUIRE(p.hudSpriteCount == 9);    // camera-immune HUD bucket (UI chrome)
}

TEST_CASE("render:nineslice:update re-expands in place; :remove drops all children", "[nineslice][scene_collector]") {
    Fixture fx; FrameAllocator alloc;
    fx.publishBox("render:nineslice:add", 3, 0.0, 0.0, 128.0, 64.0, false);
    fx.pump();
    REQUIRE(fx.collector.finalize(alloc).spriteCount == 9);

    // Update to a NARROW box (10px wide): the centre column collapses -> only 6 quads survive.
    fx.publishBox("render:nineslice:update", 3, 0.0, 0.0, 10.0, 64.0, false);
    fx.pump();
    REQUIRE(fx.collector.finalize(alloc).spriteCount == 6);   // re-expanded in place, no stale children

    // Remove: every child gone.
    auto rem = std::make_unique<JsonDataNode>("ns");
    rem->setInt("renderId", 3);
    fx.ioPublisher->publish("render:nineslice:remove", std::move(rem));
    fx.pump();
    REQUIRE(fx.collector.finalize(alloc).spriteCount == 0);
}

TEST_CASE("render:nineslice tint is applied to every child quad", "[nineslice][scene_collector]") {
    Fixture fx; FrameAllocator alloc;
    // 0x80402000 -> bytes 0x80/0x40/0x20/0x00; each channel is byte/255 (NOT /256), so 0x80 = 128/255.
    fx.publishBox("render:nineslice:add", 9, 0.0, 0.0, 128.0, 64.0, false, 0x80402000u);
    fx.pump();

    FramePacket p = fx.collector.finalize(alloc);
    REQUIRE(p.spriteCount == 9);
    const float er = 128.0f/255.0f, eg = 64.0f/255.0f, eb = 32.0f/255.0f;
    for (size_t i = 0; i < p.spriteCount; ++i) {
        REQUIRE_THAT(p.sprites[i].r, WithinAbs(er, 1e-4));
        REQUIRE_THAT(p.sprites[i].g, WithinAbs(eg, 1e-4));
        REQUIRE_THAT(p.sprites[i].b, WithinAbs(eb, 1e-4));
        REQUIRE_THAT(p.sprites[i].a, WithinAbs(0.0f, 1e-4));
    }
}
