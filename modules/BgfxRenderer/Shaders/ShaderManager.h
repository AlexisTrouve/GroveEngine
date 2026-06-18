#pragma once

#include "../RHI/RHITypes.h"
#include <string>
#include <unordered_map>

namespace grove {

namespace rhi { class IRHIDevice; }

/**
 * @brief Manages shader loading and caching for BgfxRenderer
 *
 * Loads embedded pre-compiled shaders based on the current renderer type.
 * Supports: OpenGL, OpenGL ES, Vulkan, DirectX 11/12, Metal
 *
 * Uses the RHI abstraction - no bgfx types exposed.
 */
class ShaderManager {
public:
    ShaderManager() = default;
    ~ShaderManager();

    // Non-copyable
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    /**
     * @brief Initialize with RHI device and renderer name
     * @param device The RHI device for shader creation
     * @param rendererName Renderer name from device caps (e.g., "Vulkan", "OpenGL")
     */
    void init(rhi::IRHIDevice& device, const std::string& rendererName);

    /**
     * @brief Shutdown and destroy all shaders
     * @param device The RHI device for shader destruction
     */
    void shutdown(rhi::IRHIDevice& device);

    /**
     * @brief Get a shader program by name
     * @param name Program name (e.g., "color", "sprite", "debug")
     * @return Valid shader handle or invalid handle if not found
     */
    rhi::ShaderHandle getProgram(const std::string& name) const;

    /**
     * @brief Check if a program exists
     */
    bool hasProgram(const std::string& name) const;

    /**
     * @brief Get number of loaded programs
     */
    size_t getProgramCount() const { return m_programs.size(); }

private:
    void loadBuiltinShaders(rhi::IRHIDevice& device, const std::string& rendererName);
    void loadSpriteShader(rhi::IRHIDevice& device, const std::string& rendererName);
    void loadTilemapShader(rhi::IRHIDevice& device, const std::string& rendererName);

    std::unordered_map<std::string, rhi::ShaderHandle> m_programs;
    bool m_initialized = false;
};

} // namespace grove
