#include "ShaderManager.h"
#include "../RHI/RHIDevice.h"
#include <unordered_set>

// Embedded shader bytecode
#include "vs_color.bin.h"
#include "fs_color.bin.h"
#include "vs_sprite.bin.h"
#include "fs_sprite.bin.h"
#include "vs_tilemap.bin.h"
#include "fs_tilemap.bin.h"
#include "vs_composite.bin.h"
#include "fs_composite.bin.h"
#include "vs_light.bin.h"
#include "fs_light.bin.h"

namespace grove {

ShaderManager::~ShaderManager() {
    // Note: shutdown() must be called explicitly with device before destruction
    // We can't call it here because we don't have the device reference
}

void ShaderManager::init(rhi::IRHIDevice& device, const std::string& rendererName) {
    if (m_initialized) {
        return;
    }

    loadBuiltinShaders(device, rendererName);
    m_initialized = true;
}

void ShaderManager::shutdown(rhi::IRHIDevice& device) {
    // Several program names intentionally ALIAS the same shader handle ("color" and
    // "debug" share one program — see loadBuiltinShaders). Destroying the same handle
    // more than once is a double-free of the underlying bgfx program (UB / debug
    // assert), which fired on every renderer shutdown AND every hot-reload teardown.
    // Destroy each UNIQUE handle exactly once.
    std::unordered_set<uint16_t> destroyedIds;
    for (auto& [name, handle] : m_programs) {
        if (handle.isValid() && destroyedIds.insert(handle.id).second) {
            device.destroy(handle);
        }
    }
    m_programs.clear();
    m_initialized = false;
}

rhi::ShaderHandle ShaderManager::getProgram(const std::string& name) const {
    auto it = m_programs.find(name);
    if (it != m_programs.end()) {
        return it->second;
    }
    return rhi::ShaderHandle{}; // Invalid handle
}

bool ShaderManager::hasProgram(const std::string& name) const {
    return m_programs.find(name) != m_programs.end();
}

void ShaderManager::loadBuiltinShaders(rhi::IRHIDevice& device, const std::string& rendererName) {
    // Select shader bytecode based on renderer
    const uint8_t* vsData = nullptr;
    uint32_t vsSize = 0;
    const uint8_t* fsData = nullptr;
    uint32_t fsSize = 0;

    if (rendererName == "OpenGL") {
        vsData = vs_drawstress_glsl;
        vsSize = sizeof(vs_drawstress_glsl);
        fsData = fs_drawstress_glsl;
        fsSize = sizeof(fs_drawstress_glsl);
    } else if (rendererName == "OpenGL ES") {
        vsData = vs_drawstress_essl;
        vsSize = sizeof(vs_drawstress_essl);
        fsData = fs_drawstress_essl;
        fsSize = sizeof(fs_drawstress_essl);
    } else if (rendererName == "Vulkan") {
        vsData = vs_drawstress_spv;
        vsSize = sizeof(vs_drawstress_spv);
        fsData = fs_drawstress_spv;
        fsSize = sizeof(fs_drawstress_spv);
    } else if (rendererName == "Direct3D 11" || rendererName == "Direct3D 12") {
        vsData = vs_drawstress_dx11;
        vsSize = sizeof(vs_drawstress_dx11);
        fsData = fs_drawstress_dx11;
        fsSize = sizeof(fs_drawstress_dx11);
    } else if (rendererName == "Metal") {
        vsData = vs_drawstress_mtl;
        vsSize = sizeof(vs_drawstress_mtl);
        fsData = fs_drawstress_mtl;
        fsSize = sizeof(fs_drawstress_mtl);
    } else {
        // Fallback to Vulkan (most common in WSL2)
        vsData = vs_drawstress_spv;
        vsSize = sizeof(vs_drawstress_spv);
        fsData = fs_drawstress_spv;
        fsSize = sizeof(fs_drawstress_spv);
    }

    // Create color shader via RHI
    rhi::ShaderDesc shaderDesc;
    shaderDesc.vsData = vsData;
    shaderDesc.vsSize = vsSize;
    shaderDesc.fsData = fsData;
    shaderDesc.fsSize = fsSize;

    rhi::ShaderHandle colorProgram = device.createShader(shaderDesc);

    if (colorProgram.isValid()) {
        m_programs["color"] = colorProgram;
        m_programs["debug"] = colorProgram;
    }

    // Load sprite instancing shader
    loadSpriteShader(device, rendererName);

    // Load GPU tilemap shader (index-texture path)
    loadTilemapShader(device, rendererName);

    // Full-screen lighting composite (lighting L1).
    loadCompositeShader(device, rendererName);

    // Radial lights (lighting L2).
    loadLightShader(device, rendererName);
}

void ShaderManager::loadSpriteShader(rhi::IRHIDevice& device, const std::string& rendererName) {
    const uint8_t* vsData = nullptr;
    uint32_t vsSize = 0;
    const uint8_t* fsData = nullptr;
    uint32_t fsSize = 0;

    if (rendererName == "OpenGL") {
        vsData = vs_sprite_glsl;
        vsSize = sizeof(vs_sprite_glsl);
        fsData = fs_sprite_glsl;
        fsSize = sizeof(fs_sprite_glsl);
    } else if (rendererName == "Vulkan") {
        vsData = vs_sprite_spv;
        vsSize = sizeof(vs_sprite_spv);
        fsData = fs_sprite_spv;
        fsSize = sizeof(fs_sprite_spv);
    } else if (rendererName == "Metal") {
        vsData = vs_sprite_mtl;
        vsSize = sizeof(vs_sprite_mtl);
        fsData = fs_sprite_mtl;
        fsSize = sizeof(fs_sprite_mtl);
    } else if (rendererName == "Direct3D 11" || rendererName == "Direct3D 12") {
        vsData = vs_sprite_dx11;
        vsSize = sizeof(vs_sprite_dx11);
        fsData = fs_sprite_dx11;
        fsSize = sizeof(fs_sprite_dx11);
    } else {
        // Fallback to Vulkan (most common in WSL2)
        vsData = vs_sprite_spv;
        vsSize = sizeof(vs_sprite_spv);
        fsData = fs_sprite_spv;
        fsSize = sizeof(fs_sprite_spv);
    }

    rhi::ShaderDesc shaderDesc;
    shaderDesc.vsData = vsData;
    shaderDesc.vsSize = vsSize;
    shaderDesc.fsData = fsData;
    shaderDesc.fsSize = fsSize;

    rhi::ShaderHandle spriteProgram = device.createShader(shaderDesc);

    if (spriteProgram.isValid()) {
        m_programs["sprite"] = spriteProgram;
    }
}

void ShaderManager::loadTilemapShader(rhi::IRHIDevice& device, const std::string& rendererName) {
    // Select the bytecode variant for the active renderer. Same mapping as the sprite shader.
    // NOTE: the Metal variant is a placeholder on this toolchain (no Metal backend in shaderc here);
    // the local runtime forces Direct3D11, so dx11 is the path that actually executes.
    const uint8_t* vsData = nullptr; uint32_t vsSize = 0;
    const uint8_t* fsData = nullptr; uint32_t fsSize = 0;

    if (rendererName == "OpenGL") {
        vsData = vs_tilemap_glsl; vsSize = sizeof(vs_tilemap_glsl);
        fsData = fs_tilemap_glsl; fsSize = sizeof(fs_tilemap_glsl);
    } else if (rendererName == "Direct3D 11" || rendererName == "Direct3D 12") {
        vsData = vs_tilemap_dx11; vsSize = sizeof(vs_tilemap_dx11);
        fsData = fs_tilemap_dx11; fsSize = sizeof(fs_tilemap_dx11);
    } else if (rendererName == "Metal") {
        vsData = vs_tilemap_mtl; vsSize = sizeof(vs_tilemap_mtl);
        fsData = fs_tilemap_mtl; fsSize = sizeof(fs_tilemap_mtl);
    } else {
        // Vulkan + fallback (SPIR-V).
        vsData = vs_tilemap_spv; vsSize = sizeof(vs_tilemap_spv);
        fsData = fs_tilemap_spv; fsSize = sizeof(fs_tilemap_spv);
    }

    rhi::ShaderDesc shaderDesc;
    shaderDesc.vsData = vsData; shaderDesc.vsSize = vsSize;
    shaderDesc.fsData = fsData; shaderDesc.fsSize = fsSize;

    rhi::ShaderHandle tilemapProgram = device.createShader(shaderDesc);
    if (tilemapProgram.isValid()) {
        m_programs["tilemap"] = tilemapProgram;
    }
}

void ShaderManager::loadCompositeShader(rhi::IRHIDevice& device, const std::string& rendererName) {
    // Same per-renderer bytecode mapping as the sprite/tilemap shaders. Loaded UNCONDITIONALLY at
    // startup even though most games never light anything: creating the program is a one-off cost of
    // a few KB, whereas deferring it would mean building a GPU program in the middle of the first
    // lit frame — a hitch exactly when the effect is meant to appear. The zero-cost guarantee is
    // about per-FRAME work (no targets, no draw), not about a program that sits unused.
    const uint8_t* vsData = nullptr; uint32_t vsSize = 0;
    const uint8_t* fsData = nullptr; uint32_t fsSize = 0;

    if (rendererName == "OpenGL") {
        vsData = vs_composite_glsl; vsSize = sizeof(vs_composite_glsl);
        fsData = fs_composite_glsl; fsSize = sizeof(fs_composite_glsl);
    } else if (rendererName == "Direct3D 11" || rendererName == "Direct3D 12") {
        vsData = vs_composite_dx11; vsSize = sizeof(vs_composite_dx11);
        fsData = fs_composite_dx11; fsSize = sizeof(fs_composite_dx11);
    } else if (rendererName == "Metal") {
        vsData = vs_composite_mtl; vsSize = sizeof(vs_composite_mtl);
        fsData = fs_composite_mtl; fsSize = sizeof(fs_composite_mtl);
    } else {
        vsData = vs_composite_spv; vsSize = sizeof(vs_composite_spv);
        fsData = fs_composite_spv; fsSize = sizeof(fs_composite_spv);
    }

    rhi::ShaderDesc shaderDesc;
    shaderDesc.vsData = vsData; shaderDesc.vsSize = vsSize;
    shaderDesc.fsData = fsData; shaderDesc.fsSize = fsSize;

    rhi::ShaderHandle compositeProgram = device.createShader(shaderDesc);
    if (compositeProgram.isValid()) {
        m_programs["composite"] = compositeProgram;
    }
}

void ShaderManager::loadLightShader(rhi::IRHIDevice& device, const std::string& rendererName) {
    // Same per-renderer mapping as every other program. Loaded unconditionally for the same reason
    // as the composite: a few KB once, versus building a GPU program mid-frame the first time a game
    // lights something.
    const uint8_t* vsData = nullptr; uint32_t vsSize = 0;
    const uint8_t* fsData = nullptr; uint32_t fsSize = 0;

    if (rendererName == "OpenGL") {
        vsData = vs_light_glsl; vsSize = sizeof(vs_light_glsl);
        fsData = fs_light_glsl; fsSize = sizeof(fs_light_glsl);
    } else if (rendererName == "Direct3D 11" || rendererName == "Direct3D 12") {
        vsData = vs_light_dx11; vsSize = sizeof(vs_light_dx11);
        fsData = fs_light_dx11; fsSize = sizeof(fs_light_dx11);
    } else if (rendererName == "Metal") {
        vsData = vs_light_mtl; vsSize = sizeof(vs_light_mtl);
        fsData = fs_light_mtl; fsSize = sizeof(fs_light_mtl);
    } else {
        vsData = vs_light_spv; vsSize = sizeof(vs_light_spv);
        fsData = fs_light_spv; fsSize = sizeof(fs_light_spv);
    }

    rhi::ShaderDesc shaderDesc;
    shaderDesc.vsData = vsData; shaderDesc.vsSize = vsSize;
    shaderDesc.fsData = fsData; shaderDesc.fsSize = fsSize;

    rhi::ShaderHandle lightProgram = device.createShader(shaderDesc);
    if (lightProgram.isValid()) {
        m_programs["light"] = lightProgram;
    }
}

} // namespace grove
