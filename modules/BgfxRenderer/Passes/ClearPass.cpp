#include "ClearPass.h"
#include "../RHI/RHIDevice.h"

namespace grove {

void ClearPass::setup(rhi::IRHIDevice& device) {
    // No resources needed for clear pass
}

void ClearPass::shutdown(rhi::IRHIDevice& device) {
    // Nothing to clean up
}

void ClearPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    (void)device; // Unused
    // Clear is handled via view setup in bgfx
    // The clear color is set in BgfxRendererModule before frame execution

    // For command buffer approach, we'd record:
    // cmd.setClear(frame.clearColor);

    // But bgfx handles clear through setViewClear, which is called
    // at the beginning of each frame in the main module
}

} // namespace grove
