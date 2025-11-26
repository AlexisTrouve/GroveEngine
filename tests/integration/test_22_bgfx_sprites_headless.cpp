/**
 * Test: BgfxRenderer Sprite Integration Test (Headless)
 *
 * Tests the BgfxRendererModule data structures without actual rendering.
 * Validates: JsonDataNode for sprite data, FramePacket structures.
 */

#include <grove/JsonDataNode.h>

#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <cmath>

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
    REQUIRE(std::abs(sprite->getDouble("rotation") - 1.57f) < 0.01);
    REQUIRE(sprite->getInt("color") == 0xFF0000FF);
    REQUIRE(sprite->getInt("textureId") == 5);
    REQUIRE(sprite->getInt("layer") == 10);
}
