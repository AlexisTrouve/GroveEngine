# ============================================================================
# BgfxRenderer Module - CMake Configuration
# ============================================================================

cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Fetch bgfx
# ============================================================================

include(FetchContent)

FetchContent_Declare(
    bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        v1.127.8710-464
    GIT_SHALLOW    TRUE
)

# bgfx options
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(bgfx)

# ============================================================================
# BgfxRenderer Shared Library
# ============================================================================

add_library(BgfxRenderer SHARED
    # Main module
    BgfxRendererModule.cpp

    # RHI
    RHI/RHICommandBuffer.cpp
    RHI/BgfxDevice.cpp

    # Frame
    Frame/FrameAllocator.cpp

    # RenderGraph
    RenderGraph/RenderGraph.cpp

    # Passes
    Passes/ClearPass.cpp
    Passes/SpritePass.cpp
    Passes/DebugPass.cpp

    # Scene
    Scene/SceneCollector.cpp

    # Resources
    Resources/ResourceCache.cpp
)

target_include_directories(BgfxRenderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(BgfxRenderer PRIVATE
    GroveEngine::impl
    bgfx
    bx
    spdlog::spdlog
)

target_compile_features(BgfxRenderer PRIVATE cxx_std_17)

set_target_properties(BgfxRenderer PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
)

# ============================================================================
# Platform-specific settings
# ============================================================================

if(WIN32)
    target_compile_definitions(BgfxRenderer PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(BgfxRenderer PRIVATE
        pthread
        dl
        X11
        GL
    )
endif()

if(APPLE)
    target_link_libraries(BgfxRenderer PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
    )
endif()
