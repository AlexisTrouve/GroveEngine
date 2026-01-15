#include "RenderGraph.h"
#include "../RHI/RHIDevice.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <queue>
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

    const size_t n = m_passes.size();
    if (n == 0) {
        m_compiled = true;
        return;
    }

    // Build name to index map
    std::unordered_map<std::string, size_t> nameToIndex;
    for (size_t i = 0; i < n; ++i) {
        nameToIndex[m_passes[i]->getName()] = i;
    }

    // Build adjacency list and in-degree count for topological sort
    std::vector<std::vector<size_t>> adjacency(n);
    std::vector<size_t> inDegree(n, 0);

    for (size_t i = 0; i < n; ++i) {
        auto deps = m_passes[i]->getDependencies();
        for (const char* depName : deps) {
            auto it = nameToIndex.find(depName);
            if (it != nameToIndex.end()) {
                size_t depIdx = it->second;
                adjacency[depIdx].push_back(i);  // depIdx -> i (dep must run before i)
                inDegree[i]++;
            }
        }
    }

    // Kahn's algorithm for topological sort with sort order as secondary key
    // Use a priority queue to respect sortOrder among nodes with same in-degree
    auto compare = [this](size_t a, size_t b) {
        return m_passes[a]->getSortOrder() > m_passes[b]->getSortOrder(); // min-heap
    };
    std::priority_queue<size_t, std::vector<size_t>, decltype(compare)> readyQueue(compare);

    // Initialize with nodes that have no dependencies
    for (size_t i = 0; i < n; ++i) {
        if (inDegree[i] == 0) {
            readyQueue.push(i);
        }
    }

    m_sortedIndices.clear();
    m_sortedIndices.reserve(n);

    while (!readyQueue.empty()) {
        size_t current = readyQueue.top();
        readyQueue.pop();
        m_sortedIndices.push_back(current);

        // Decrease in-degree of dependents
        for (size_t dependent : adjacency[current]) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                readyQueue.push(dependent);
            }
        }
    }

    // Check for cycles
    if (m_sortedIndices.size() != n) {
        throw std::runtime_error("RenderGraph: Cycle detected in pass dependencies!");
    }

    m_compiled = true;
}

void RenderGraph::execute(const FramePacket& frame, rhi::IRHIDevice& device) {
    if (!m_compiled) {
        compile();
    }

    // Single command buffer for single-threaded execution
    rhi::RHICommandBuffer cmdBuffer;

    // Execute passes in topologically sorted order
    for (size_t idx : m_sortedIndices) {
        m_passes[idx]->execute(frame, device, cmdBuffer);
    }

    // Execute the recorded command buffer on the device
    device.executeCommandBuffer(cmdBuffer);
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
