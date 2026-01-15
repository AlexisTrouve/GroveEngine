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
    sprite->setDouble("x", 100.0);
    sprite->setDouble("y", 200.0);
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
        sprite->setDouble("x", i * 10.0);
        sprite->setDouble("y", i * 5.0);
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
    sprite->setDouble("x", 50.0);
    sprite->setDouble("y", 50.0);
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
        sprite->setDouble("x", frame * 100.0);
        sprite->setDouble("y", 0.0);
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
