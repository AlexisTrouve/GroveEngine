#include "RenderGraph.h"
#include "../RHI/RHIDevice.h"
#include <algorithm>
#include <unordered_map>
#include <stdexcept>

namespace grove {

void RenderGraph::addPass(std::unique_ptr<RenderPass> pass) {
    m_passes.push_back(std::move(pass));
    m_compiled = false;
}

void RenderGraph::setup(rhi::IRHIDevice& device) {
    for (auto& pass : m_passes) {
        pass->setup(device);
    }
}

void RenderGraph::compile() {
    if (m_compiled) return;

    // Build name to index map
    std::unordered_map<std::string, size_t> nameToIndex;
    for (size_t i = 0; i < m_passes.size(); ++i) {
        nameToIndex[m_passes[i]->getName()] = i;
    }

    // Create sorted indices based on sort order and dependencies
    m_sortedIndices.clear();
    m_sortedIndices.reserve(m_passes.size());

    for (size_t i = 0; i < m_passes.size(); ++i) {
        m_sortedIndices.push_back(i);
    }

    // Sort by sort order (topological sort would be more complete but this is simpler)
    std::sort(m_sortedIndices.begin(), m_sortedIndices.end(),
        [this](size_t a, size_t b) {
            return m_passes[a]->getSortOrder() < m_passes[b]->getSortOrder();
        });

    m_compiled = true;
}

void RenderGraph::execute(const FramePacket& frame, rhi::IRHIDevice& device) {
    if (!m_compiled) {
        compile();
    }

    // Single command buffer for single-threaded execution
    rhi::RHICommandBuffer cmdBuffer;

    // Execute passes in order
    for (size_t idx : m_sortedIndices) {
        m_passes[idx]->execute(frame, cmdBuffer);
    }

    // TODO: Execute command buffer on device
    // For now, passes directly call bgfx through the device
}

void RenderGraph::shutdown(rhi::IRHIDevice& device) {
    for (auto& pass : m_passes) {
        pass->shutdown(device);
    }
    m_passes.clear();
    m_sortedIndices.clear();
    m_compiled = false;
}

} // namespace grove
