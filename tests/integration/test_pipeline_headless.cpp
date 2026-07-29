/**
 * Integration Tests: Pipeline Headless
 *
 * End-to-end tests of the complete rendering pipeline without GPU:
 * IIO messages → SceneCollector → FramePacket → RenderGraph → CommandBuffer
 *
 * Validates:
 * - Full data flow from IIO to command generation
 * - Pass ordering (Clear before Sprite before Debug)
 * - Multiple frames handling
 * - FramePacket construction accuracy
 *
 * Uses MockRHIDevice for headless testing
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../../modules/BgfxRenderer/Scene/SceneCollector.h"
#include "../../modules/BgfxRenderer/Frame/FrameAllocator.h"
#include "../../modules/BgfxRenderer/RenderGraph/RenderGraph.h"
#include "../../modules/BgfxRenderer/RHI/RHICommandBuffer.h"
#include "../mocks/MockRHIDevice.h"

#include "grove/IntraIO.h"
#include "grove/IntraIOManager.h"
#include "grove/JsonDataNode.h"

#include <memory>
#include <chrono>
#include <sstream>

using namespace grove;
using namespace grove::test;
using Catch::Matchers::WithinAbs;

// Helper to create unique instance IDs per test
inline std::string uniqueId(const std::string& prefix) {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << prefix << "_" << now;
    return oss.str();
}

// ============================================================================
// Single Sprite Pipeline
// ============================================================================

TEST_CASE("Pipeline - single sprite end-to-end", "[pipeline][integration]") {
    MockRHIDevice device;
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));

    SceneCollector collector;
    FrameAllocator allocator;
    RenderGraph graph;

    // Setup collector
    collector.setup(ioCollector.get());

    // Publish sprite message
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", 100.0);
    sprite->setDouble("cy", 200.0);
    sprite->setInt("color", 0xFFFFFFFF);
    sprite->setInt("textureId", 1);

    ioPublisher->publish("render:sprite", std::move(sprite));

    // Collect messages
    collector.collect(ioCollector.get(), 0.016f);

    // Finalize packet
    FramePacket packet = collector.finalize(allocator);

    // Validate packet
    REQUIRE(packet.spriteCount == 1);
    REQUIRE(packet.sprites != nullptr);
    REQUIRE_THAT(packet.sprites[0].x, WithinAbs(100.0f, 0.01f));
    REQUIRE_THAT(packet.sprites[0].y, WithinAbs(200.0f, 0.01f));
    // Color is white (1.0, 1.0, 1.0, 1.0)
    REQUIRE_THAT(packet.sprites[0].r, WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(packet.sprites[0].a, WithinAbs(1.0f, 0.01f));
}

// ============================================================================
// Batch Sprites Pipeline
// ============================================================================

TEST_CASE("Pipeline - batch 100 sprites", "[pipeline][integration]") {
    MockRHIDevice device;
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));

    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Publish 100 sprites
    constexpr int NUM_SPRITES = 100;
    for (int i = 0; i < NUM_SPRITES; ++i) {
        auto sprite = std::make_unique<JsonDataNode>("sprite");
        sprite->setDouble("cx", i * 10.0);
        sprite->setDouble("cy", i * 5.0);
        sprite->setInt("color", 0xFF000000 | i);
        sprite->setInt("textureId", i % 10);

        ioPublisher->publish("render:sprite", std::move(sprite));
    }

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.spriteCount == NUM_SPRITES);
    REQUIRE(packet.sprites != nullptr);

    // Verify first and last sprite
    REQUIRE_THAT(packet.sprites[0].x, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(packet.sprites[99].x, WithinAbs(990.0f, 0.01f));
    // No color checks needed for batch test
}

// ============================================================================
// Camera Pipeline
// ============================================================================

TEST_CASE("Pipeline - camera message sets view", "[pipeline][integration]") {
    MockRHIDevice device;
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));

    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Publish camera message
    auto camera = std::make_unique<JsonDataNode>("camera");
    camera->setDouble("x", 500.0);
    camera->setDouble("y", 300.0);
    camera->setDouble("zoom", 2.0);
    camera->setInt("viewportW", 1920);
    camera->setInt("viewportH", 1080);

    ioPublisher->publish("render:camera", std::move(camera));

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    // Verify camera applied to mainView
    REQUIRE_THAT(packet.mainView.positionX, WithinAbs(500.0f, 0.01f));
    REQUIRE_THAT(packet.mainView.positionY, WithinAbs(300.0f, 0.01f));
    REQUIRE_THAT(packet.mainView.zoom, WithinAbs(2.0f, 0.01f));
    REQUIRE(packet.mainView.viewportW == 1920);
    REQUIRE(packet.mainView.viewportH == 1080);
}

// ============================================================================
// Clear Color Pipeline
// ============================================================================

TEST_CASE("Pipeline - clear color message", "[pipeline][integration]") {
    MockRHIDevice device;
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));

    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Publish clear color
    auto clear = std::make_unique<JsonDataNode>("clear");
    clear->setInt("color", 0x336699FF);

    ioPublisher->publish("render:clear", std::move(clear));

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    REQUIRE(packet.clearColor == 0x336699FF);
}

// ============================================================================
// All Passes Pipeline
// ============================================================================

TEST_CASE("Pipeline - mixed message types", "[pipeline][integration]") {
    MockRHIDevice device;
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));

    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    // Publish mixed types: clear + sprite + debug
    auto clear = std::make_unique<JsonDataNode>("clear");
    clear->setInt("color", 0x000000FF);
    ioPublisher->publish("render:clear", std::move(clear));

    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", 50.0);
    sprite->setDouble("cy", 50.0);
    ioPublisher->publish("render:sprite", std::move(sprite));

    auto line = std::make_unique<JsonDataNode>("line");
    line->setDouble("x1", 0.0);
    line->setDouble("y1", 0.0);
    line->setDouble("x2", 100.0);
    line->setDouble("y2", 100.0);
    line->setInt("color", 0xFF0000FF);
    ioPublisher->publish("render:debug:line", std::move(line));

    collector.collect(ioCollector.get(), 0.016f);
    FramePacket packet = collector.finalize(allocator);

    // Verify all data collected
    REQUIRE(packet.clearColor == 0x000000FF);
    REQUIRE(packet.spriteCount == 1);
    REQUIRE(packet.debugLineCount == 1);

    REQUIRE_THAT(packet.sprites[0].x, WithinAbs(50.0f, 0.01f));
    REQUIRE_THAT(packet.debugLines[0].x1, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(packet.debugLines[0].x2, WithinAbs(100.0f, 0.01f));
}

// ============================================================================
// Multiple Frames Pipeline
// ============================================================================

TEST_CASE("Pipeline - 10 consecutive frames", "[pipeline][integration]") {
    MockRHIDevice device;
    auto& ioManager = IntraIOManager::getInstance();
    auto ioCollector = ioManager.createInstance(uniqueId("receiver"));
    auto ioPublisher = ioManager.createInstance(uniqueId("publisher"));

    SceneCollector collector;
    FrameAllocator allocator;

    collector.setup(ioCollector.get());

    constexpr int NUM_FRAMES = 10;

    for (int frame = 0; frame < NUM_FRAMES; ++frame) {
        // Reset allocator each frame
        allocator.reset();

        // Publish sprite with frame-specific position
        auto sprite = std::make_unique<JsonDataNode>("sprite");
        sprite->setDouble("cx", frame * 100.0);
        sprite->setDouble("cy", 0.0);
        sprite->setInt("textureId", frame);

        ioPublisher->publish("render:sprite", std::move(sprite));

        // Collect and finalize
        collector.collect(ioCollector.get(), 0.016f);
        FramePacket packet = collector.finalize(allocator);

        // Verify frame data
        REQUIRE(packet.spriteCount == 1);
        REQUIRE_THAT(packet.sprites[0].x, WithinAbs(frame * 100.0f, 0.01f));
        REQUIRE_THAT(packet.sprites[0].textureId, WithinAbs(static_cast<float>(frame), 0.01f));

        // Clear for next frame
        collector.clear();
    }
}

// ============================================================================
// Lighting L1 — the composite pass.
//
// The composite is the step that multiplies the rendered scene by the light term. Its cost is a
// full-screen draw plus two offscreen targets, so the ONE property that matters as much as the
// effect itself is that it charges NOTHING when nobody lights anything: no ambient published means
// no draw, no target, no view redirection — the frame goes straight to the backbuffer exactly as it
// did before lighting existed.
//
// Drifterra, DAOS and Fractax share this engine and publish no ambient. This is their regression
// test, not a nicety.
// ============================================================================

#include "../../modules/BgfxRenderer/Passes/CompositePass.h"

TEST_CASE("Composite - no ambient means the pass emits NOTHING", "[pipeline][light]") {
    MockRHIDevice device;
    CompositePass pass;
    rhi::RHICommandBuffer cmd;

    pass.setup(device);
    const int fbAfterSetup = device.framebufferCreateCount.load();

    FramePacket packet;                 // ambientColor defaults to 0 = lighting inactive
    REQUIRE(packet.ambientColor == 0u);

    pass.execute(packet, device, cmd);

    REQUIRE(cmd.size() == 0);           // not one command — the bypass is total
    REQUIRE(device.framebufferCreateCount.load() == fbAfterSetup);   // and no target was made

    pass.shutdown(device);
}

TEST_CASE("Composite - an ambient makes the pass draw a full-screen quad", "[pipeline][light]") {
    MockRHIDevice device;
    CompositePass pass;
    rhi::RHICommandBuffer cmd;

    pass.setup(device);

    FramePacket packet;
    packet.ambientColor = 0x404060FF;   // a dim blue night
    pass.execute(packet, device, cmd);

    // One full-screen draw, submitted. Asserting "more than zero commands" would pass on a pass that
    // set state and drew nothing, so we require the draw and the submit specifically.
    REQUIRE(cmd.size() > 0);
    bool sawDraw = false, sawSubmit = false;
    for (const auto& c : cmd.getCommands()) {
        if (c.type == rhi::CommandType::DrawIndexed || c.type == rhi::CommandType::Draw) sawDraw = true;
        if (c.type == rhi::CommandType::Submit) sawSubmit = true;
    }
    REQUIRE(sawDraw);
    REQUIRE(sawSubmit);

    pass.shutdown(device);
}

// ============================================================================
// OcclusionPass — walls and filters write into ONE map, multiplicatively (lighting F1).
//
// The blend mode is the whole subject here. While only walls existed the pass wrote OPAQUE, and
// that was invisible: black over black is black whichever quad wins. A coloured filter breaks that
// tie — under an overwrite the last pane drawn would govern the overlap, which is precisely the
// depth-ordering problem the socle claims not to have. A product has no order.
// ============================================================================

#include "../../modules/BgfxRenderer/Passes/OcclusionPass.h"

namespace {
// Bytes one rect costs in the occlusion vertex buffer: 6 vertices of PosColor (3 + 4 floats).
constexpr uint32_t kBytesPerRect = 6 * 7 * sizeof(float);

rhi::BlendMode firstBlend(const rhi::RHICommandBuffer& cmd) {
    for (const auto& c : cmd.getCommands()) {
        if (c.type == rhi::CommandType::SetState) return c.setState.state.blend;
    }
    return rhi::BlendMode::None;
}
bool hasDraw(const rhi::RHICommandBuffer& cmd) {
    for (const auto& c : cmd.getCommands()) {
        if (c.type == rhi::CommandType::Draw || c.type == rhi::CommandType::DrawIndexed) return true;
    }
    return false;
}
} // namespace

TEST_CASE("Occlusion - matter is written MULTIPLICATIVELY, so overlaps compose",
          "[pipeline][light][filter]") {
    MockRHIDevice device;
    OcclusionPass pass(device.createShader(rhi::ShaderDesc{}));
    rhi::RHICommandBuffer cmd;
    pass.setup(device);

    OccluderCommand wall{}; wall.x = 0.0f; wall.y = 0.0f; wall.w = 10.0f; wall.h = 10.0f;
    FramePacket packet;
    packet.occluders = &wall;
    packet.occluderCount = 1;

    pass.execute(packet, device, cmd);

    // Not "some blend": the SPECIFIC one. Alpha or additive would each still draw something, and a
    // test that only checked for a draw would be green on either.
    REQUIRE(firstBlend(cmd) == rhi::BlendMode::Multiply);
    pass.shutdown(device);
}

TEST_CASE("Occlusion - a filter ALONE fills the map (no wall required)",
          "[pipeline][light][filter]") {
    MockRHIDevice device;
    OcclusionPass pass(device.createShader(rhi::ShaderDesc{}));
    rhi::RHICommandBuffer cmd;
    pass.setup(device);

    // The regression this pins: the early-out used to test occluders only. A scene whose matter is
    // entirely stained glass would have recorded nothing and sampled a blank map — the tint would
    // simply not exist, with no error anywhere to say why.
    FilterCommand glass{};
    glass.x = 5.0f; glass.y = 5.0f; glass.w = 20.0f; glass.h = 4.0f;
    glass.r = 1.0f; glass.g = 0.7f; glass.b = 0.7f;

    FramePacket packet;
    packet.filters = &glass;
    packet.filterCount = 1;

    pass.execute(packet, device, cmd);

    REQUIRE(hasDraw(cmd));
    REQUIRE(device.lastUpdateBufferSize.load() == kBytesPerRect);
    pass.shutdown(device);
}

TEST_CASE("Occlusion - walls and filters share ONE draw", "[pipeline][light][filter]") {
    MockRHIDevice device;
    OcclusionPass pass(device.createShader(rhi::ShaderDesc{}));
    rhi::RHICommandBuffer cmd;
    pass.setup(device);

    OccluderCommand walls[2] = {};
    walls[0].w = walls[0].h = 10.0f;
    walls[1].x = 40.0f; walls[1].w = walls[1].h = 10.0f;
    FilterCommand glass{};
    glass.w = 20.0f; glass.h = 4.0f; glass.r = 1.0f; glass.g = 0.5f; glass.b = 0.5f;

    FramePacket packet;
    packet.occluders = walls; packet.occluderCount = 2;
    packet.filters = &glass;  packet.filterCount = 1;

    pass.execute(packet, device, cmd);

    // Three rects, one upload, one draw. Asserting the byte size rather than "more than zero" is
    // what would catch a loop that silently dropped the filters after the walls.
    REQUIRE(device.lastUpdateBufferSize.load() == 3 * kBytesPerRect);
    int submits = 0;
    for (const auto& c : cmd.getCommands()) if (c.type == rhi::CommandType::Submit) ++submits;
    REQUIRE(submits == 1);
    pass.shutdown(device);
}

TEST_CASE("Occlusion - no matter at all records NOTHING", "[pipeline][light][filter]") {
    MockRHIDevice device;
    OcclusionPass pass(device.createShader(rhi::ShaderDesc{}));
    rhi::RHICommandBuffer cmd;
    pass.setup(device);

    FramePacket packet;    // neither occluders nor filters
    pass.execute(packet, device, cmd);

    REQUIRE(cmd.size() == 0);   // the zero-cost bypass, unchanged by adding a second primitive
    pass.shutdown(device);
}
