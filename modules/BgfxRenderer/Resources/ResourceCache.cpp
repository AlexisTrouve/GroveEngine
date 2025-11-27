#include "ResourceCache.h"
#include "TextureLoader.h"
#include "../RHI/RHIDevice.h"
#include <mutex>
#include <shared_mutex>

namespace grove {

rhi::TextureHandle ResourceCache::getTexture(const std::string& path) const {
    std::shared_lock lock(m_mutex);
    auto it = m_textures.find(path);
    if (it != m_textures.end()) {
        return it->second;
    }
    return rhi::TextureHandle{}; // Invalid handle
}

rhi::ShaderHandle ResourceCache::getShader(const std::string& name) const {
    std::shared_lock lock(m_mutex);
    auto it = m_shaders.find(name);
    if (it != m_shaders.end()) {
        return it->second;
    }
    return rhi::ShaderHandle{}; // Invalid handle
}

rhi::TextureHandle ResourceCache::loadTexture(rhi::IRHIDevice& device, const std::string& path) {
    // Check if already loaded
    {
        std::shared_lock lock(m_mutex);
        auto it = m_textures.find(path);
        if (it != m_textures.end()) {
            return it->second;
        }
    }

    // Load texture from file using TextureLoader (stb_image)
    auto result = TextureLoader::loadFromFile(device, path);

    if (!result.success) {
        // Return invalid handle on failure
        return rhi::TextureHandle{};
    }

    // Store in cache
    {
        std::unique_lock lock(m_mutex);
        m_textures[path] = result.handle;
    }

    return result.handle;
}

rhi::ShaderHandle ResourceCache::loadShader(rhi::IRHIDevice& device, const std::string& name,
                                             const void* vsData, uint32_t vsSize,
                                             const void* fsData, uint32_t fsSize) {
    // Check if already loaded
    {
        std::shared_lock lock(m_mutex);
        auto it = m_shaders.find(name);
        if (it != m_shaders.end()) {
            return it->second;
        }
    }

    rhi::ShaderDesc desc;
    desc.vsData = vsData;
    desc.vsSize = vsSize;
    desc.fsData = fsData;
    desc.fsSize = fsSize;

    rhi::ShaderHandle handle = device.createShader(desc);

    // Store in cache
    {
        std::unique_lock lock(m_mutex);
        m_shaders[name] = handle;
    }

    return handle;
}

bool ResourceCache::hasTexture(const std::string& path) const {
    std::shared_lock lock(m_mutex);
    return m_textures.find(path) != m_textures.end();
}

bool ResourceCache::hasShader(const std::string& name) const {
    std::shared_lock lock(m_mutex);
    return m_shaders.find(name) != m_shaders.end();
}

void ResourceCache::clear(rhi::IRHIDevice& device) {
    std::unique_lock lock(m_mutex);

    for (auto& [path, handle] : m_textures) {
        device.destroy(handle);
    }
    m_textures.clear();

    for (auto& [name, handle] : m_shaders) {
        device.destroy(handle);
    }
    m_shaders.clear();
}

size_t ResourceCache::getTextureCount() const {
    std::shared_lock lock(m_mutex);
    return m_textures.size();
}

size_t ResourceCache::getShaderCount() const {
    std::shared_lock lock(m_mutex);
    return m_shaders.size();
}

} // namespace grove
