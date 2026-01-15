/**
 * Integration Tests: TextureLoader
 *
 * Tests texture loading from files/memory including:
 * - Error handling (nonexistent files, invalid data)
 * - LoadResult structure
 * - Dimensions validation
 *
 * Note: Full format tests (PNG, JPG, etc.) would require actual image assets.
 * This test suite focuses on API contract and error handling.
 *
 * Uses MockRHIDevice for headless testing.
 */

#include <catch2/catch_test_macros.hpp>

#include "../../modules/BgfxRenderer/Resources/TextureLoader.h"
#include "../mocks/MockRHIDevice.h"

#include <vector>
#include <cstdint>

using namespace grove;
using namespace grove::test;

// ============================================================================
// Error Handling
// ============================================================================

TEST_CASE("TextureLoader - load nonexistent file fails", "[texture_loader][integration]") {
    MockRHIDevice device;

    auto result = TextureLoader::loadFromFile(device, "/nonexistent/path/to/file.png");

    REQUIRE(result.success == false);
    REQUIRE(!result.handle.isValid());
    REQUIRE(result.width == 0);
    REQUIRE(result.height == 0);
    REQUIRE(!result.error.empty()); // Should have error message
}

TEST_CASE("TextureLoader - load empty path fails", "[texture_loader][integration]") {
    MockRHIDevice device;

    auto result = TextureLoader::loadFromFile(device, "");

    REQUIRE(result.success == false);
    REQUIRE(!result.handle.isValid());
}

TEST_CASE("TextureLoader - load from invalid memory fails", "[texture_loader][integration]") {
    MockRHIDevice device;

    // Invalid PNG data (just random bytes)
    uint8_t invalidData[] = {0xFF, 0xFE, 0xFD, 0xFC};

    auto result = TextureLoader::loadFromMemory(device, invalidData, sizeof(invalidData));

    REQUIRE(result.success == false);
    REQUIRE(!result.handle.isValid());
}

TEST_CASE("TextureLoader - load from null memory fails", "[texture_loader][integration]") {
    MockRHIDevice device;

    auto result = TextureLoader::loadFromMemory(device, nullptr, 0);

    REQUIRE(result.success == false);
    REQUIRE(!result.handle.isValid());
}

// ============================================================================
// LoadResult Structure
// ============================================================================

TEST_CASE("TextureLoader - LoadResult has expected fields", "[texture_loader][integration]") {
    // Just verify the structure compiles and has expected members
    TextureLoader::LoadResult result;

    result.success = false;
    result.handle = rhi::TextureHandle{};
    result.width = 0;
    result.height = 0;
    result.error = "test error";

    REQUIRE(result.success == false);
    REQUIRE(result.width == 0);
    REQUIRE(result.height == 0);
    REQUIRE(result.error == "test error");
}

// ============================================================================
// Memory Loading
// ============================================================================

TEST_CASE("TextureLoader - loadFromMemory with zero size fails", "[texture_loader][integration]") {
    MockRHIDevice device;

    uint8_t data[] = {0x00};

    auto result = TextureLoader::loadFromMemory(device, data, 0);

    REQUIRE(result.success == false);
}

// ============================================================================
// Integration with Mock Device
// ============================================================================

TEST_CASE("TextureLoader - failed load does not create texture", "[texture_loader][integration]") {
    MockRHIDevice device;

    int beforeCount = device.textureCreateCount.load();

    TextureLoader::loadFromFile(device, "/invalid/path.png");

    int afterCount = device.textureCreateCount.load();

    // No texture should be created on failure
    REQUIRE(afterCount == beforeCount);
}

// ============================================================================
// Notes for Future Asset-Based Tests
// ============================================================================

/*
 * To add comprehensive format tests, create test assets:
 *
 * tests/assets/textures/white_16x16.png
 * tests/assets/textures/checker_32x32.png
 * tests/assets/textures/gradient_64x64.jpg
 *
 * Then add tests like:
 *
 * TEST_CASE("TextureLoader - load valid PNG succeeds") {
 *     MockRHIDevice device;
 *     auto result = TextureLoader::loadFromFile(device, "tests/assets/textures/white_16x16.png");
 *
 *     REQUIRE(result.success == true);
 *     REQUIRE(result.handle.isValid());
 *     REQUIRE(result.width == 16);
 *     REQUIRE(result.height == 16);
 *     REQUIRE(result.error.empty());
 *     REQUIRE(device.textureCreateCount == 1);
 * }
 *
 * TEST_CASE("TextureLoader - load valid JPG succeeds") {
 *     // Similar test for JPEG
 * }
 *
 * TEST_CASE("TextureLoader - dimensions validation") {
 *     // Verify reported dimensions match actual image
 * }
 */
