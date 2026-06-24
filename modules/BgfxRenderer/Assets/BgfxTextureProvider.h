#pragma once

#include "AssetManager.h"
#include "../Resources/TextureLoader.h"
#include "../Resources/ResourceCache.h"
#include "../RHI/RHIDevice.h"
#include <unordered_map>
#include <cstdint>
#include <string>

namespace grove::assets {

/**
 * @brief Real ITextureProvider over bgfx — the GPU side of the AssetManager.
 *
 * QUOI : charge un PNG via TextureLoader, ENREGISTRE le handle dans le ResourceCache (donc le SpritePass le
 *   rend par id, sans rien changer au path sprite), et le détruit via ResourceCache::unloadById. Rapporte le
 *   coût VRAM RGBA8 pour le budget de l'AssetManager.
 * POURQUOI : on garde l'AssetManager pur (décide quoi/quand) ; ce provider fait juste le travail GPU. On
 *   n'utilise PAS le cache-par-path du ResourceCache (loadTextureWithId) — c'est l'AssetManager qui cache
 *   par assetId ; on passe par registerTexture (id pur) + unloadById (éviction réelle).
 * COMMENT : `load` -> texId (l'id ResourceCache, utilisable par render:sprite) + on mémorise w*h*4. `unload`
 *   -> unloadById + on oublie la taille. Pattern backend-derrière-interface (cf. SoundManager).
 */
class BgfxTextureProvider : public ITextureProvider {
public:
    BgfxTextureProvider(rhi::IRHIDevice* device, ResourceCache* cache) : m_device(device), m_cache(cache) {}

    uint32_t load(const std::string& path) override {
        if (!m_device || !m_cache) return 0;
        TextureLoader::LoadResult r = TextureLoader::loadFromFile(*m_device, path);
        if (!r.success) return 0;
        const uint16_t id = m_cache->registerTexture(r.handle);   // -> renderable by id in the sprite pass
        if (id == 0) { m_device->destroy(r.handle); return 0; }
        m_bytes[id] = static_cast<uint64_t>(r.width) * static_cast<uint64_t>(r.height) * 4ull;   // RGBA8
        return id;
    }

    // Render-thread upload of already-decoded RGBA8 pixels (phase 3 async load). Same registerTexture path as
    // load(), but the slow decode already happened off-thread — here we only do the cheap GPU createTexture.
    // mipLevels 1 (no mips): keeps this render-thread call as light as possible; like AtlasPacker, async-loaded
    // textures forgo trilinear minification (fine for icons/UI drawn near native scale). `rgba` may be freed
    // right after — bgfx::copy (in createTexture) duplicates it immediately.
    uint32_t upload(const uint8_t* rgba, int w, int h) override {
        if (!m_device || !m_cache || !rgba || w <= 0 || h <= 0) return 0;
        rhi::TextureDesc desc;
        desc.width = static_cast<uint16_t>(w);
        desc.height = static_cast<uint16_t>(h);
        desc.format = rhi::TextureDesc::RGBA8;
        desc.mipLevels = 1;
        desc.data = rgba;
        desc.dataSize = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4u;
        rhi::TextureHandle handle = m_device->createTexture(desc);
        if (!handle.isValid()) return 0;
        const uint16_t id = m_cache->registerTexture(handle);
        if (id == 0) { m_device->destroy(handle); return 0; }
        m_bytes[id] = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * 4ull;   // RGBA8
        return id;
    }

    void unload(uint32_t texId) override {
        if (m_cache && m_device) m_cache->unloadById(static_cast<uint16_t>(texId), *m_device);
        m_bytes.erase(texId);
    }

    uint64_t bytes(uint32_t texId) const override {
        auto it = m_bytes.find(texId);
        return it != m_bytes.end() ? it->second : 0;
    }

private:
    rhi::IRHIDevice* m_device = nullptr;
    ResourceCache* m_cache = nullptr;
    std::unordered_map<uint32_t, uint64_t> m_bytes;
};

} // namespace grove::assets
