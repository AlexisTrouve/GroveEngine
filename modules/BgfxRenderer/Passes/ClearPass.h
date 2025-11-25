#pragma once

#include "../RenderGraph/RenderPass.h"

namespace grove {

// ============================================================================
// Clear Pass - Clears the framebuffer
// ============================================================================

class ClearPass : public RenderPass {
public:
    const char* getName() const override { return "Clear"; }
    uint32_t getSortOrder() const override { return 0; } // First pass

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::RHICommandBuffer& cmd) override;
};

} // namespace grove
