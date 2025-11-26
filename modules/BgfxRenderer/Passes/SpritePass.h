#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"

namespace grove {

// ============================================================================
// Sprite Pass - Renders 2D sprites with batching
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

private:
    rhi::ShaderHandle m_shader;
    rhi::BufferHandle m_quadVB;
    rhi::BufferHandle m_quadIB;
    rhi::BufferHandle m_instanceBuffer;
    rhi::UniformHandle m_textureSampler;

    static constexpr uint32_t MAX_SPRITES_PER_BATCH = 10000;
};

} // namespace grove
