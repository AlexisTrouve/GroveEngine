#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"

namespace grove {

// ============================================================================
// Debug Pass - Renders debug lines and shapes
// ============================================================================

class DebugPass : public RenderPass {
public:
    /**
     * @brief Construct DebugPass with required shader
     * @param shader The shader program to use for debug line rendering
     */
    explicit DebugPass(rhi::ShaderHandle shader);

    const char* getName() const override { return "Debug"; }
    uint32_t getSortOrder() const override { return 900; } // Near last
    std::vector<const char*> getDependencies() const override { return {"Sprites"}; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

private:
    rhi::ShaderHandle m_lineShader;
    rhi::BufferHandle m_lineVB;

    static constexpr uint32_t MAX_DEBUG_LINES = 10000;
};

} // namespace grove
