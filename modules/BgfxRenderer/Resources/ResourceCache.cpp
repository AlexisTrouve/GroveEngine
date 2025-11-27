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

rhi::TextureHandle ResourceCache::getTextureById(uint16_t id) const {
    std::shared_lock lock(m_mutex);
    if (id < m_textureById.size()) {
        return m_textureById[id];
    }
    return rhi::TextureHandle{}; // Invalid handle
}

uint16_t ResourceCache::getTextureId(const std::string& path) const {
    std::shared_lock lock(m_mutex);
    auto it = m_pathToTextureId.find(path);
    if (it != m_pathToTextureId.end()) {
        return it->second;
    }
    return 0; // Invalid ID
}

uint16_t ResourceCache::loadTextureWithId(rhi::IRHIDevice& device, const std::string& path) {
    // Check if already loaded
    {
        std::shared_lock lock(m_mutex);
        auto it = m_pathToTextureId.find(path);
        if (it != m_pathToTextureId.end()) {
            return it->second;
        }
    }

    // Load texture from file using TextureLoader (stb_image)
    auto result = TextureLoader::loadFromFile(device, path);

    if (!result.success) {
        return 0; // Invalid ID
    }

    // Store in cache with new ID
    {
        std::unique_lock lock(m_mutex);

        // Double-check after acquiring exclusive lock
        auto it = m_pathToTextureId.find(path);
        if (it != m_pathToTextureId.end()) {
            // Another thread loaded it, destroy our copy
            device.destroy(result.handle);
            return it->second;
        }

        // Assign new ID (1-based, 0 = invalid)
        uint16_t newId = static_cast<uint16_t>(m_textureById.size());
        if (newId == 0) {
            // Reserve index 0 as invalid/default
            m_textureById.push_back(rhi::TextureHandle{});
            newId = 1;
        }

        m_textureById.push_back(result.handle);
        m_pathToTextureId[path] = newId;
        m_textures[path] = result.handle;

        return newId;
    }
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

    // Store in cache (also register with ID system)
    {
        std::unique_lock lock(m_mutex);

        // Double check
        auto it = m_textures.find(path);
        if (it != m_textures.end()) {
            device.destroy(result.handle);
            return it->second;
        }

        m_textures[path] = result.handle;

        // Also add to ID system
        uint16_t newId = static_cast<uint16_t>(m_textureById.size());
        if (newId == 0) {
            m_textureById.push_back(rhi::TextureHandle{});
            newId = 1;
        }
        m_textureById.push_back(result.handle);
        m_pathToTextureId[path] = newId;
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
    m_textureById.clear();
    m_pathToTextureId.clear();

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
