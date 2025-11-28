#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"
#include "../Frame/FramePacket.h"
#include <vector>

namespace grove {

class ResourceCache;

// ============================================================================
// Particle Pass - Renders 2D particles with additive blending
// ============================================================================

class ParticlePass : public RenderPass {
public:
    /**
     * @brief Construct ParticlePass with required shader
     * @param shader The shader program to use for particle rendering
     */
    explicit ParticlePass(rhi::ShaderHandle shader);

    const char* getName() const override { return "Particles"; }
    uint32_t getSortOrder() const override { return 150; }  // After sprites (100)
    std::vector<const char*> getDependencies() const override { return {"Sprites"}; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

    /**
     * @brief Set resource cache for texture lookup by ID
     */
    void setResourceCache(ResourceCache* cache) { m_resourceCache = cache; }

    /**
     * @brief Set blend mode for particles
     * @param additive true for additive blending (fire, sparks), false for alpha (smoke)
     */
    void setAdditiveBlending(bool additive) { m_additiveBlending = additive; }

private:
    void flushBatch(rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd,
                    rhi::TextureHandle texture, uint32_t count);

    rhi::ShaderHandle m_shader;
    rhi::BufferHandle m_quadVB;
    rhi::BufferHandle m_quadIB;
    rhi::BufferHandle m_instanceBuffer;  // Fallback for when transient allocation fails
    rhi::UniformHandle m_textureSampler;
    rhi::TextureHandle m_defaultTexture;  // White 1x1 texture for untextured particles

    ResourceCache* m_resourceCache = nullptr;
    bool m_additiveBlending = true;  // Default to additive for fire/spark effects

    // GPU-aligned particle instances for batching
    std::vector<SpriteInstance> m_particleInstances;

    static constexpr uint32_t MAX_PARTICLES_PER_BATCH = 10000;
};

} // namespace grove
