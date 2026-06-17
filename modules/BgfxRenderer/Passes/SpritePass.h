#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"
#include <vector>

namespace grove {

class ResourceCache;
namespace camera { struct WorldBounds; }  // fwd-decl; the .cpp includes Scene/Camera.h

// ============================================================================
// Sprite Pass - Renders 2D sprites with batching by texture
// ============================================================================

class SpritePass : public RenderPass {
public:
    /**
     * @brief Construct SpritePass with required shader
     * @param shader The shader program to use for sprite rendering
     */
    explicit SpritePass(rhi::ShaderHandle shader);

    const char* getName() const override { return "Sprites"; }
    uint32_t getSortOrder() const override { return 100; }
    std::vector<const char*> getDependencies() const override { return {"Clear"}; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

    /**
     * @brief Set resource cache for texture lookup by ID
     */
    void setResourceCache(ResourceCache* cache) { m_resourceCache = cache; }

    /**
     * @brief Set fallback texture when textureId=0 or texture not found
     */
    void setDefaultTexture(rhi::TextureHandle texture) { m_activeTexture = texture; }

    /**
     * @brief Legacy: Set a single texture for all sprites (backward compat)
     * @deprecated Use setResourceCache for multi-texture support
     */
    void setTexture(rhi::TextureHandle texture) { m_activeTexture = texture; }

private:
    void flushBatch(rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd,
                    rhi::TextureHandle texture, uint32_t count);

    // Render one sprite set (sort by layer/texture, batch, submit) to a specific bgfx view.
    // Called once for world sprites (view 0) and once for HUD sprites (view 1) so the HUD
    // can ride a fixed screen-space transform while the world view zooms.
    // `cull` (optional) = visible world bounds; sprites whose AABB is outside are skipped. Pass
    // nullptr to disable culling (HUD is screen-space → never culled).
    void renderSpriteSet(rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd,
                         const SpriteInstance* sprites, size_t count, rhi::ViewId viewId,
                         const camera::WorldBounds* cull);

    rhi::ShaderHandle m_shader;
    rhi::BufferHandle m_quadVB;
    rhi::BufferHandle m_quadIB;
    rhi::BufferHandle m_instanceBuffer;
    rhi::UniformHandle m_textureSampler;
    rhi::TextureHandle m_defaultTexture;  // White 1x1 texture fallback
    rhi::TextureHandle m_activeTexture;   // Default texture for textureId=0

    ResourceCache* m_resourceCache = nullptr;

    // Sorted sprite indices for batching
    std::vector<uint32_t> m_sortedIndices;

    static constexpr uint32_t MAX_SPRITES_PER_BATCH = 10000;
};

} // namespace grove
