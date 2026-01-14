#pragma once

#include "../RHI/RHITypes.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <shared_mutex>

namespace grove {

namespace rhi { class IRHIDevice; }

// ============================================================================
// Resource Cache - Thread-safe texture and shader cache with numeric IDs
// ============================================================================

class ResourceCache {
public:
    ResourceCache() = default;

    // Thread-safe resource access (returns invalid handle if not found)
    rhi::TextureHandle getTexture(const std::string& path) const;
    rhi::ShaderHandle getShader(const std::string& name) const;

    // Get texture by numeric ID (for sprite rendering)
    rhi::TextureHandle getTextureById(uint16_t id) const;

    // Get texture ID from path (returns 0 if not found)
    uint16_t getTextureId(const std::string& path) const;

    // Loading (called from main thread) - returns texture ID
    uint16_t loadTextureWithId(rhi::IRHIDevice& device, const std::string& path);

    // Register an already-created texture (for runtime/procedural textures)
    // Returns the assigned texture ID
    uint16_t registerTexture(rhi::TextureHandle handle, const std::string& name = "");

    // Legacy loading (returns handle directly)
    rhi::TextureHandle loadTexture(rhi::IRHIDevice& device, const std::string& path);
    rhi::ShaderHandle loadShader(rhi::IRHIDevice& device, const std::string& name,
                                  const void* vsData, uint32_t vsSize,
                                  const void* fsData, uint32_t fsSize);

    // Check if resource exists
    bool hasTexture(const std::string& path) const;
    bool hasShader(const std::string& name) const;

    // Cleanup
    void clear(rhi::IRHIDevice& device);

    // Stats
    size_t getTextureCount() const;
    size_t getShaderCount() const;

private:
    // Path-based lookup
    std::unordered_map<std::string, rhi::TextureHandle> m_textures;
    std::unordered_map<std::string, rhi::ShaderHandle> m_shaders;

    // ID-based lookup for textures (index = textureId, 0 = invalid/default)
    std::vector<rhi::TextureHandle> m_textureById;
    std::unordered_map<std::string, uint16_t> m_pathToTextureId;

    mutable std::shared_mutex m_mutex;
};

} // namespace grove
