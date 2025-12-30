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

# CRITICAL: Disable bgfx multithreading BEFORE fetching to avoid TLS issues when running from a DLL on Windows
# Without this, bgfx::frame() crashes on Frame 1 due to render thread/DLL memory conflicts
add_compile_definitions(BGFX_CONFIG_MULTITHREADED=0)

# Fix for MinGW GCC 15+ - glslang headers don't include <stdint.h>
# Use stdint.h (C header, works in C++ too) to ensure uint32_t is defined
if(MINGW)
    add_compile_options(-include stdint.h)
endif()

# bgfx options
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)  # Need shaderc for shader compilation
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
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

    # Shaders
    Shaders/ShaderManager.cpp

    # Passes
    Passes/ClearPass.cpp
    Passes/TilemapPass.cpp
    Passes/SpritePass.cpp
    Passes/TextPass.cpp
    Passes/ParticlePass.cpp
    Passes/DebugPass.cpp

    # Text
    Text/BitmapFont.cpp

    # Scene
    Scene/SceneCollector.cpp

    # Resources
    Resources/ResourceCache.cpp
    Resources/TextureLoader.cpp

    # Debug
    Debug/DebugOverlay.cpp
)

target_include_directories(BgfxRenderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${bgfx_SOURCE_DIR}/bimg/3rdparty  # stb_image
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

# ============================================================================
# Shader Compilation
# ============================================================================

set(BGFX_SHADER_INCLUDE_DIR ${bgfx_SOURCE_DIR}/bgfx/src)
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Shaders)

# Function to compile a shader
function(compile_bgfx_shader SHADER_NAME SHADER_TYPE OUTPUT_VAR)
    set(INPUT_FILE ${SHADER_DIR}/${SHADER_NAME}.sc)
    set(OUTPUT_DIR ${SHADER_DIR})
    set(VARYING_DEF ${SHADER_DIR}/varying.def.sc)

    # Output files for each platform
    set(OUTPUT_SPIRV ${OUTPUT_DIR}/${SHADER_NAME}_spirv.bin)
    set(OUTPUT_GLSL ${OUTPUT_DIR}/${SHADER_NAME}_glsl.bin)
    set(OUTPUT_ESSL ${OUTPUT_DIR}/${SHADER_NAME}_essl.bin)
    set(OUTPUT_DX11 ${OUTPUT_DIR}/${SHADER_NAME}_dx11.bin)
    set(OUTPUT_MTL ${OUTPUT_DIR}/${SHADER_NAME}_mtl.bin)

    if(SHADER_TYPE STREQUAL "vertex")
        set(SHADER_MODEL vs_5_0)
        set(SHADER_PROFILE 430)
    else()
        set(SHADER_MODEL ps_5_0)
        set(SHADER_PROFILE 430)
    endif()

    # Compile for Vulkan (SPIR-V)
    add_custom_command(
        OUTPUT ${OUTPUT_SPIRV}
        COMMAND shaderc
            -f ${INPUT_FILE}
            -o ${OUTPUT_SPIRV}
            --type ${SHADER_TYPE}
            --platform linux
            --profile spirv
            -i ${BGFX_SHADER_INCLUDE_DIR}
            --varyingdef ${VARYING_DEF}
        DEPENDS ${INPUT_FILE} ${VARYING_DEF} shaderc
        COMMENT "Compiling ${SHADER_NAME} for SPIR-V"
    )

    # Compile for OpenGL
    add_custom_command(
        OUTPUT ${OUTPUT_GLSL}
        COMMAND shaderc
            -f ${INPUT_FILE}
            -o ${OUTPUT_GLSL}
            --type ${SHADER_TYPE}
            --platform linux
            --profile ${SHADER_PROFILE}
            -i ${BGFX_SHADER_INCLUDE_DIR}
            --varyingdef ${VARYING_DEF}
        DEPENDS ${INPUT_FILE} ${VARYING_DEF} shaderc
        COMMENT "Compiling ${SHADER_NAME} for GLSL"
    )

    # Compile for OpenGL ES
    add_custom_command(
        OUTPUT ${OUTPUT_ESSL}
        COMMAND shaderc
            -f ${INPUT_FILE}
            -o ${OUTPUT_ESSL}
            --type ${SHADER_TYPE}
            --platform android
            -i ${BGFX_SHADER_INCLUDE_DIR}
            --varyingdef ${VARYING_DEF}
        DEPENDS ${INPUT_FILE} ${VARYING_DEF} shaderc
        COMMENT "Compiling ${SHADER_NAME} for ESSL"
    )

    # Compile for DirectX 11
    add_custom_command(
        OUTPUT ${OUTPUT_DX11}
        COMMAND shaderc
            -f ${INPUT_FILE}
            -o ${OUTPUT_DX11}
            --type ${SHADER_TYPE}
            --platform windows
            --profile ${SHADER_MODEL}
            -i ${BGFX_SHADER_INCLUDE_DIR}
            --varyingdef ${VARYING_DEF}
        DEPENDS ${INPUT_FILE} ${VARYING_DEF} shaderc
        COMMENT "Compiling ${SHADER_NAME} for DX11"
    )

    # Compile for Metal
    add_custom_command(
        OUTPUT ${OUTPUT_MTL}
        COMMAND shaderc
            -f ${INPUT_FILE}
            -o ${OUTPUT_MTL}
            --type ${SHADER_TYPE}
            --platform osx
            --profile metal
            -i ${BGFX_SHADER_INCLUDE_DIR}
            --varyingdef ${VARYING_DEF}
        DEPENDS ${INPUT_FILE} ${VARYING_DEF} shaderc
        COMMENT "Compiling ${SHADER_NAME} for Metal"
    )

    set(${OUTPUT_VAR} ${OUTPUT_SPIRV} ${OUTPUT_GLSL} ${OUTPUT_ESSL} ${OUTPUT_DX11} ${OUTPUT_MTL} PARENT_SCOPE)
endfunction()

# Compile sprite shaders
compile_bgfx_shader(vs_sprite vertex VS_SPRITE_OUTPUTS)
compile_bgfx_shader(fs_sprite fragment FS_SPRITE_OUTPUTS)

# Custom target to compile all shaders
add_custom_target(compile_shaders
    DEPENDS ${VS_SPRITE_OUTPUTS} ${FS_SPRITE_OUTPUTS}
    COMMENT "Compiling all BgfxRenderer shaders"
)

# Make BgfxRenderer depend on shaders (optional, enable if you want auto-compilation)
# add_dependencies(BgfxRenderer compile_shaders)
