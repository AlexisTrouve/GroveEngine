#pragma once

#include "../RHI/RHITypes.h"
#include <unordered_map>
#include <string>
#include <shared_mutex>

namespace grove {

namespace rhi { class IRHIDevice; }

// ============================================================================
// Resource Cache - Thread-safe texture and shader cache
// ============================================================================

class ResourceCache {
public:
    ResourceCache() = default;

    // Thread-safe resource access (returns invalid handle if not found)
    rhi::TextureHandle getTexture(const std::string& path) const;
    rhi::ShaderHandle getShader(const std::string& name) const;

    // Loading (called from main thread)
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
    std::unordered_map<std::string, rhi::TextureHandle> m_textures;
    std::unordered_map<std::string, rhi::ShaderHandle> m_shaders;
    mutable std::shared_mutex m_mutex;
};

} // namespace grove
