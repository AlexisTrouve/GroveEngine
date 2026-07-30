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
    void loadCompositeShader(rhi::IRHIDevice& device, const std::string& rendererName);
    void loadLightShader(rhi::IRHIDevice& device, const std::string& rendererName);
    // Soft radial media (lighting A4). Has its own vertex stage for READABILITY only (`u_nebula`
    // names what it places, where `u_light` needed an apology comment) — reusing vs_light works just
    // as well, and the "bytecode sharing corrupts bgfx's refcount" claim that first justified the
    // split was FALSE (see vs_nebula.sc's header).
    void loadNebulaShader(rhi::IRHIDevice& device, const std::string& rendererName);
    // Post-processing (plan B): bright-pass extraction, separable blur, and the present pass.
    // ⚠️ The three programs ALL reuse `vs_composite` — the same full-screen clip-space quad — and that
    // is deliberate and verified: sharing a vertex stage between programs is fine (the earlier claim
    // to the contrary was a misdiagnosed stale build artifact, known-annoyances §3bis).
    void loadBloomShaders(rhi::IRHIDevice& device, const std::string& rendererName);

    std::unordered_map<std::string, rhi::ShaderHandle> m_programs;
    bool m_initialized = false;
};

} // namespace grove
