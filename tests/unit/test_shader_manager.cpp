/**
 * Unit Tests: ShaderManager
 *
 * Tests shader management including:
 * - Initialization with built-in shaders
 * - Program retrieval
 * - Shutdown cleanup
 *
 * Uses MockRHIDevice to avoid GPU dependency
 */

#include <catch2/catch_test_macros.hpp>

#include "../../modules/BgfxRenderer/Shaders/ShaderManager.h"
#include "../mocks/MockRHIDevice.h"

using namespace grove;
using namespace grove::test;

// ============================================================================
// Initialization & Cleanup
// ============================================================================

TEST_CASE("ShaderManager - init creates default shaders", "[shader_manager][unit]") {
    MockRHIDevice device;
    ShaderManager manager;

    manager.init(device, "OpenGL");

    // Should create at least 1 shader (color, sprite, etc.)
    // Exact count depends on built-in shaders
    REQUIRE(manager.getProgramCount() > 0);
    REQUIRE(device.shaderCreateCount > 0);
}

TEST_CASE("ShaderManager - init with different renderers", "[shader_manager][unit]") {
    MockRHIDevice device;

    SECTION("OpenGL") {
        ShaderManager manager;
        manager.init(device, "OpenGL");
        REQUIRE(manager.getProgramCount() > 0);
    }

    SECTION("Vulkan") {
        device.reset();
        ShaderManager manager;
        manager.init(device, "Vulkan");
        REQUIRE(manager.getProgramCount() > 0);
    }

    SECTION("Direct3D 11") {
        device.reset();
        ShaderManager manager;
        manager.init(device, "Direct3D 11");
        REQUIRE(manager.getProgramCount() > 0);
    }

    SECTION("Metal") {
        device.reset();
        ShaderManager manager;
        manager.init(device, "Metal");
        REQUIRE(manager.getProgramCount() > 0);
    }
}

TEST_CASE("ShaderManager - shutdown destroys all programs", "[shader_manager][unit]") {
    MockRHIDevice device;
    ShaderManager manager;

    manager.init(device, "OpenGL");

    int shadersCreated = device.shaderCreateCount.load();
    REQUIRE(shadersCreated > 0);

    size_t programCount = manager.getProgramCount();
    REQUIRE(programCount > 0);

    manager.shutdown(device);

    // Each UNIQUE shader handle must be destroyed EXACTLY ONCE. Several program
    // names alias the same handle ("color" and "debug" share one program), so the
    // number of destroy() calls must equal the number of shaders CREATED (one per
    // unique handle) — NOT the number of program-name entries. Destroying the same
    // bgfx handle twice is a double-free (UB on the GPU program resource).
    REQUIRE(device.shaderDestroyCount == shadersCreated);
}

// ============================================================================
// Program Retrieval
// ============================================================================

TEST_CASE("ShaderManager - getProgram returns valid handle for existing program", "[shader_manager][unit]") {
    MockRHIDevice device;
    ShaderManager manager;

    manager.init(device, "OpenGL");

    SECTION("sprite program exists") {
        auto handle = manager.getProgram("sprite");
        REQUIRE(handle.isValid());
    }

    SECTION("color program exists") {
        auto handle = manager.getProgram("color");
        REQUIRE(handle.isValid());
    }
}

TEST_CASE("ShaderManager - getProgram returns invalid handle for non-existent program", "[shader_manager][unit]") {
    MockRHIDevice device;
    ShaderManager manager;

    manager.init(device, "OpenGL");

    auto handle = manager.getProgram("nonexistent_shader");
    REQUIRE(!handle.isValid());
}

TEST_CASE("ShaderManager - hasProgram returns correct values", "[shader_manager][unit]") {
    MockRHIDevice device;
    ShaderManager manager;

    manager.init(device, "OpenGL");

    SECTION("Has sprite program") {
        REQUIRE(manager.hasProgram("sprite") == true);
    }

    SECTION("Has color program") {
        REQUIRE(manager.hasProgram("color") == true);
    }

    SECTION("Does not have unknown program") {
        REQUIRE(manager.hasProgram("unknown") == false);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("ShaderManager - calling getProgram before init", "[shader_manager][unit]") {
    ShaderManager manager;

    // Should return invalid handle gracefully (no crash)
    auto handle = manager.getProgram("sprite");
    REQUIRE(!handle.isValid());
}

TEST_CASE("ShaderManager - calling shutdown before init", "[shader_manager][unit]") {
    MockRHIDevice device;
    ShaderManager manager;

    // Should not crash
    manager.shutdown(device);

    REQUIRE(device.shaderDestroyCount == 0);
}

TEST_CASE("ShaderManager - calling init twice", "[shader_manager][unit]") {
    MockRHIDevice device;
    ShaderManager manager;

    manager.init(device, "OpenGL");
    int firstCount = manager.getProgramCount();

    // Second init should probably be a no-op or replace programs
    // Current implementation behavior: test what actually happens
    manager.init(device, "Vulkan");

    // Verify no crash and manager still functional
    REQUIRE(manager.getProgramCount() > 0);
}

TEST_CASE("ShaderManager - getProgramCount reflects init state", "[shader_manager][unit]") {
    MockRHIDevice device;
    ShaderManager manager;

    REQUIRE(manager.getProgramCount() == 0);

    manager.init(device, "OpenGL");

    REQUIRE(manager.getProgramCount() > 0);
}

// ============================================================================
// Multiple Instances
// ============================================================================

TEST_CASE("ShaderManager - multiple instances share no state", "[shader_manager][unit]") {
    MockRHIDevice device;

    ShaderManager manager1;
    ShaderManager manager2;

    manager1.init(device, "OpenGL");
    manager2.init(device, "Vulkan");

    // Both should have programs
    REQUIRE(manager1.getProgramCount() > 0);
    REQUIRE(manager2.getProgramCount() > 0);

    // Programs from manager1 should be independent of manager2
    auto handle1 = manager1.getProgram("sprite");
    auto handle2 = manager2.getProgram("sprite");

    REQUIRE(handle1.isValid());
    REQUIRE(handle2.isValid());

    // Handles may be different (different shader binaries loaded)
    // Just verify both are valid
}
