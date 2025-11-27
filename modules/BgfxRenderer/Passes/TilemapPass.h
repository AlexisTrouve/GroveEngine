#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"
#include <vector>

namespace grove {

class ResourceCache;

// ============================================================================
// Tilemap Pass - Renders 2D tilemaps efficiently
// ============================================================================

class TilemapPass : public RenderPass {
public:
    /**
     * @brief Construct TilemapPass with required shader
     * @param shader The shader program to use (sprite shader)
     */
    explicit TilemapPass(rhi::ShaderHandle shader);

    const char* getName() const override { return "Tilemaps"; }
    uint32_t getSortOrder() const override { return 50; }  // Before sprites
    std::vector<const char*> getDependencies() const override { return {"Clear"}; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

    /**
     * @brief Set resource cache for texture lookup
     */
    void setResourceCache(ResourceCache* cache) { m_resourceCache = cache; }

    /**
     * @brief Set default tileset texture
     */
    void setDefaultTileset(rhi::TextureHandle texture) { m_defaultTileset = texture; }

    /**
     * @brief Set tileset dimensions (tiles per row/column in atlas)
     */
    void setTilesetLayout(uint16_t tilesPerRow, uint16_t tilesPerCol) {
        m_tilesPerRow = tilesPerRow;
        m_tilesPerCol = tilesPerCol;
    }

private:
    rhi::ShaderHandle m_shader;
    rhi::BufferHandle m_quadVB;
    rhi::BufferHandle m_quadIB;
    rhi::BufferHandle m_instanceBuffer;
    rhi::UniformHandle m_textureSampler;
    rhi::TextureHandle m_defaultTexture;
    rhi::TextureHandle m_defaultTileset;

    ResourceCache* m_resourceCache = nullptr;

    // Tileset layout (for UV calculation)
    uint16_t m_tilesPerRow = 16;
    uint16_t m_tilesPerCol = 16;

    // Reusable buffer for tile instances
    std::vector<SpriteInstance> m_tileInstances;

    static constexpr uint32_t MAX_TILES_PER_BATCH = 16384;
};

} // namespace grove
