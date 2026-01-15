#pragma once

#include "RenderPass.h"
#include <memory>
#include <vector>

namespace grove {

namespace rhi { class IRHIDevice; }

// ============================================================================
// Render Graph - Manages pass ordering and execution
// ============================================================================

class RenderGraph {
public:
    RenderGraph() = default;
    ~RenderGraph() = default;

    // Non-copyable
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // Pass registration
    void addPass(std::unique_ptr<RenderPass> pass);

    // Setup all passes
    void setup(rhi::IRHIDevice& device);

    // Compile the graph (order, dependencies)
    void compile();

    // Execute all passes for a frame
    void execute(const FramePacket& frame, rhi::IRHIDevice& device);

    // Shutdown all passes
    void shutdown(rhi::IRHIDevice& device);

    // Accessors
    size_t getPassCount() const { return m_passes.size(); }

private:
    std::vector<std::unique_ptr<RenderPass>> m_passes;
    std::vector<size_t> m_sortedIndices;
    bool m_compiled = false;
};

} // namespace grove
