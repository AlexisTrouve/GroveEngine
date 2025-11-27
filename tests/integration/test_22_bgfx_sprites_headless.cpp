/**
 * Test: BgfxRenderer Sprite Integration Test (Headless)
 *
 * Tests the BgfxRendererModule data structures without actual rendering.
 * Validates: JsonDataNode for sprite data, IIO message publishing.
 *
 * Note: SceneCollector/FramePacket tests are in the visual test that links with BgfxRenderer.
 */

#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <iostream>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("SpriteInstance data layout", "[bgfx][unit]") {
    // Test that SpriteInstance struct can be constructed from IIO message data

    auto sprite = std::make_unique<grove::JsonDataNode>("sprite");
    sprite->setDouble("x", 100.0);
    sprite->setDouble("y", 200.0);
    sprite->setDouble("scaleX", 32.0);
    sprite->setDouble("scaleY", 32.0);
    sprite->setDouble("rotation", 1.57f);  // ~90 degrees
    sprite->setDouble("u0", 0.0);
    sprite->setDouble("v0", 0.0);
    sprite->setDouble("u1", 1.0);
    sprite->setDouble("v1", 1.0);
    sprite->setInt("color", 0xFF0000FF);
    sprite->setInt("textureId", 5);
    sprite->setInt("layer", 10);

    // Verify data can be read back
    REQUIRE(sprite->getDouble("x") == 100.0);
    REQUIRE(sprite->getDouble("y") == 200.0);
    REQUIRE(sprite->getDouble("scaleX") == 32.0);
    REQUIRE(sprite->getDouble("scaleY") == 32.0);
    REQUIRE_THAT(sprite->getDouble("rotation"), WithinAbs(1.57, 0.01));
    REQUIRE(sprite->getInt("color") == 0xFF0000FF);
    REQUIRE(sprite->getInt("textureId") == 5);
    REQUIRE(sprite->getInt("layer") == 10);
}

TEST_CASE("IIO sprite message routing between modules", "[bgfx][integration]") {
    // Use singleton IntraIOManager (same as IntraIO::publish uses)
    auto& ioManager = grove::IntraIOManager::getInstance();

    // Create two IO instances to simulate module communication
    auto gameIO = ioManager.createInstance("test_game_module");
    auto rendererIO = ioManager.createInstance("test_renderer_module");

    // Renderer subscribes to render topics
    rendererIO->subscribe("render:*");

    // Game module publishes sprites via IIO
    for (int i = 0; i < 3; ++i) {
        auto sprite = std::make_unique<grove::JsonDataNode>("sprite");
        sprite->setDouble("x", 100.0 + i * 50);
        sprite->setDouble("y", 200.0 + i * 30);
        sprite->setDouble("scaleX", 32.0);
        sprite->setDouble("scaleY", 32.0);
        sprite->setInt("color", 0xFFFFFFFF);
        sprite->setInt("layer", i);

        std::unique_ptr<grove::IDataNode> spriteData = std::move(sprite);
        gameIO->publish("render:sprite", std::move(spriteData));
    }

    // Messages should be routed to renderer
    REQUIRE(rendererIO->hasMessages() == 3);

    // Pull and verify first message
    auto msg1 = rendererIO->pullMessage();
    REQUIRE(msg1.topic == "render:sprite");
    REQUIRE(msg1.data != nullptr);
    REQUIRE_THAT(msg1.data->getDouble("x"), WithinAbs(100.0, 0.01));

    // Cleanup
    rendererIO->clearAllMessages();
    ioManager.removeInstance("test_game_module");
    ioManager.removeInstance("test_renderer_module");
}

TEST_CASE("Camera message structure", "[bgfx][unit]") {
    auto camera = std::make_unique<grove::JsonDataNode>("camera");
    camera->setDouble("x", 100.0);
    camera->setDouble("y", 50.0);
    camera->setDouble("zoom", 2.0);
    camera->setInt("viewportX", 0);
    camera->setInt("viewportY", 0);
    camera->setInt("viewportW", 800);
    camera->setInt("viewportH", 600);

    REQUIRE(camera->getDouble("x") == 100.0);
    REQUIRE(camera->getDouble("y") == 50.0);
    REQUIRE(camera->getDouble("zoom") == 2.0);
    REQUIRE(camera->getInt("viewportW") == 800);
    REQUIRE(camera->getInt("viewportH") == 600);
}

TEST_CASE("Clear color message structure", "[bgfx][unit]") {
    auto clear = std::make_unique<grove::JsonDataNode>("clear");
    clear->setInt("color", 0x112233FF);

    REQUIRE(clear->getInt("color") == 0x112233FF);
}

TEST_CASE("Debug line message structure", "[bgfx][unit]") {
    auto line = std::make_unique<grove::JsonDataNode>("line");
    line->setDouble("x1", 10.0);
    line->setDouble("y1", 20.0);
    line->setDouble("x2", 100.0);
    line->setDouble("y2", 200.0);
    line->setInt("color", 0xFF0000FF);

    REQUIRE(line->getDouble("x1") == 10.0);
    REQUIRE(line->getDouble("y1") == 20.0);
    REQUIRE(line->getDouble("x2") == 100.0);
    REQUIRE(line->getDouble("y2") == 200.0);
    REQUIRE(line->getInt("color") == 0xFF0000FF);
}
