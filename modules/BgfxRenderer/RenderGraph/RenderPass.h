#pragma once

#include "../RHI/RHICommandBuffer.h"
#include "../Frame/FramePacket.h"
#include <vector>

namespace grove {

namespace rhi { class IRHIDevice; }

// ============================================================================
// Render Pass Interface
// ============================================================================

class RenderPass {
public:
    virtual ~RenderPass() = default;

    // Unique identifier
    virtual const char* getName() const = 0;

    // Render order (lower = earlier)
    virtual uint32_t getSortOrder() const = 0;

    // Dependencies (names of passes that must execute before)
    virtual std::vector<const char*> getDependencies() const { return {}; }

    // Execution - MUST be thread-safe
    // frame: read-only
    // device: for dynamic buffer updates
    // cmd: write-only, thread-local
    virtual void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) = 0;

    // Initial setup (load shaders, create buffers)
    virtual void setup(rhi::IRHIDevice& device) = 0;

    // Cleanup
    virtual void shutdown(rhi::IRHIDevice& device) = 0;
};

} // namespace grove
