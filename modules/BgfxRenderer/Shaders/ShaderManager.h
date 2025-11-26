#pragma once

#include "../RHI/RHITypes.h"
#include <bgfx/bgfx.h>
#include <string>
#include <unordered_map>

namespace grove {

/**
 * @brief Manages shader loading and caching for BgfxRenderer
 *
 * Loads embedded pre-compiled shaders based on the current renderer type.
 * Supports: OpenGL, OpenGL ES, Vulkan, DirectX 11/12, Metal
 */
class ShaderManager {
public:
    ShaderManager() = default;
    ~ShaderManager();

    // Non-copyable
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    /**
     * @brief Initialize with current renderer type
     */
    void init(bgfx::RendererType::Enum rendererType);

    /**
     * @brief Shutdown and destroy all shaders
     */
    void shutdown();

    /**
     * @brief Get a shader program by name
     * @param name Program name (e.g., "color", "sprite", "debug")
     * @return Valid program handle or invalid handle if not found
     */
    bgfx::ProgramHandle getProgram(const std::string& name);

    /**
     * @brief Check if a program exists
     */
    bool hasProgram(const std::string& name) const;

    /**
     * @brief Get current renderer type
     */
    bgfx::RendererType::Enum getRendererType() const { return m_rendererType; }

private:
    bgfx::ShaderHandle loadShader(const char* name);
    void loadBuiltinShaders();

    bgfx::RendererType::Enum m_rendererType = bgfx::RendererType::Count;
    std::unordered_map<std::string, bgfx::ProgramHandle> m_programs;
    bool m_initialized = false;
};

} // namespace grove
