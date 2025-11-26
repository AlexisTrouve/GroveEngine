#include "ShaderManager.h"

// Embedded shader bytecode
#include "vs_color.bin.h"
#include "fs_color.bin.h"

namespace grove {

ShaderManager::~ShaderManager() {
    shutdown();
}

void ShaderManager::init(bgfx::RendererType::Enum rendererType) {
    if (m_initialized) {
        return;
    }

    m_rendererType = rendererType;
    loadBuiltinShaders();
    m_initialized = true;
}

void ShaderManager::shutdown() {
    for (auto& [name, program] : m_programs) {
        if (bgfx::isValid(program)) {
            bgfx::destroy(program);
        }
    }
    m_programs.clear();
    m_initialized = false;
}

bgfx::ProgramHandle ShaderManager::getProgram(const std::string& name) {
    auto it = m_programs.find(name);
    if (it != m_programs.end()) {
        return it->second;
    }
    return BGFX_INVALID_HANDLE;
}

bool ShaderManager::hasProgram(const std::string& name) const {
    return m_programs.find(name) != m_programs.end();
}

bgfx::ShaderHandle ShaderManager::loadShader(const char* name) {
    const uint8_t* data = nullptr;
    uint32_t size = 0;

    bool isVertex = (name[0] == 'v');

    switch (m_rendererType) {
        case bgfx::RendererType::OpenGL:
            if (isVertex) {
                data = vs_drawstress_glsl;
                size = sizeof(vs_drawstress_glsl);
            } else {
                data = fs_drawstress_glsl;
                size = sizeof(fs_drawstress_glsl);
            }
            break;

        case bgfx::RendererType::OpenGLES:
            if (isVertex) {
                data = vs_drawstress_essl;
                size = sizeof(vs_drawstress_essl);
            } else {
                data = fs_drawstress_essl;
                size = sizeof(fs_drawstress_essl);
            }
            break;

        case bgfx::RendererType::Vulkan:
            if (isVertex) {
                data = vs_drawstress_spv;
                size = sizeof(vs_drawstress_spv);
            } else {
                data = fs_drawstress_spv;
                size = sizeof(fs_drawstress_spv);
            }
            break;

        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12:
            if (isVertex) {
                data = vs_drawstress_dx11;
                size = sizeof(vs_drawstress_dx11);
            } else {
                data = fs_drawstress_dx11;
                size = sizeof(fs_drawstress_dx11);
            }
            break;

        case bgfx::RendererType::Metal:
            if (isVertex) {
                data = vs_drawstress_mtl;
                size = sizeof(vs_drawstress_mtl);
            } else {
                data = fs_drawstress_mtl;
                size = sizeof(fs_drawstress_mtl);
            }
            break;

        default:
            return BGFX_INVALID_HANDLE;
    }

    return bgfx::createShader(bgfx::copy(data, size));
}

void ShaderManager::loadBuiltinShaders() {
    // Load color shader program (for sprites, debug shapes, etc.)
    bgfx::ShaderHandle vsh = loadShader("vs_color");
    bgfx::ShaderHandle fsh = loadShader("fs_color");

    if (bgfx::isValid(vsh) && bgfx::isValid(fsh)) {
        bgfx::ProgramHandle colorProgram = bgfx::createProgram(vsh, fsh, true);
        if (bgfx::isValid(colorProgram)) {
            m_programs["color"] = colorProgram;
            // Alias for sprites (same shader for now)
            m_programs["sprite"] = colorProgram;
            m_programs["debug"] = colorProgram;
        }
    }

    // TODO: Add more specialized shaders as needed:
    // - "sprite_textured" for textured sprites
    // - "text" for text rendering
    // - "particle" for particle systems
}

} // namespace grove
