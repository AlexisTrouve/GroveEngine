#include "ShaderManager.h"
#include "../RHI/RHIDevice.h"

// Embedded shader bytecode
#include "vs_color.bin.h"
#include "fs_color.bin.h"
#include "vs_sprite.bin.h"
#include "fs_sprite.bin.h"

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
    for (auto& [name, handle] : m_programs) {
        if (handle.isValid()) {
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

} // namespace grove
