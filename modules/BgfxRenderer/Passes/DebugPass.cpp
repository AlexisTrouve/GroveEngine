#include "DebugPass.h"
#include "../RHI/RHIDevice.h"
#include "../Frame/FramePacket.h"
#include <vector>
#include <cstring>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace grove {

// Vertex format: x, y, z, r, g, b, a (7 floats = 28 bytes)
struct DebugVertex {
    float x, y, z;
    float r, g, b, a;
};
static_assert(sizeof(DebugVertex) == 28, "DebugVertex must be 28 bytes");

// Helper to convert packed RGBA color to floats
static void unpackColor(uint32_t color, float& r, float& g, float& b, float& a) {
    r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    a = static_cast<float>(color & 0xFF) / 255.0f;
}

DebugPass::DebugPass(rhi::ShaderHandle shader)
    : m_lineShader(shader)
{
}

void DebugPass::setup(rhi::IRHIDevice& device) {
    // Create dynamic vertex buffer for debug lines
    // Each line = 2 vertices, each rect = 4 lines = 8 vertices
    // Buffer size: MAX_DEBUG_LINES * 2 vertices * 28 bytes
    rhi::BufferDesc vbDesc;
    vbDesc.type = rhi::BufferDesc::Vertex;
    vbDesc.size = static_cast<size_t>(MAX_DEBUG_LINES) * 2 * sizeof(DebugVertex);
    vbDesc.data = nullptr;
    vbDesc.dynamic = true;
    vbDesc.layout = rhi::BufferDesc::PosColor;  // vec3 pos + vec4 color
    m_lineVB = device.createBuffer(vbDesc);
}

void DebugPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_lineVB);
    // Note: m_lineShader is owned by ShaderManager, not destroyed here
}

void DebugPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    static int logCounter = 0;

    // Skip if no debug primitives
    if (frame.debugLineCount == 0 && frame.debugRectCount == 0) {
        if (logCounter++ % 60 == 0) {
            spdlog::debug("[DebugPass] No primitives (lines={}, rects={})", frame.debugLineCount, frame.debugRectCount);
        }
        return;
    }

    // Skip if shader is invalid
    if (!m_lineShader.isValid()) {
        spdlog::warn("[DebugPass] Shader invalid!");
        return;
    }

    // Log periodically
    if (logCounter++ % 60 == 0) {
        spdlog::info("[DebugPass] Drawing {} lines, {} rects (shader={})",
                     frame.debugLineCount, frame.debugRectCount, m_lineShader.id);
    }

    // Build vertex data for all debug primitives
    std::vector<DebugVertex> vertices;

    // OVERFLOW GUARD: m_lineVB holds at most MAX_DEBUG_LINES*2 vertices (sized in
    // setup() assuming all primitives are lines). A rect costs 8 vertices, so a scene
    // with many rects (collision boxes, HUD bars, shipyard grid) can request far more
    // than the buffer holds. Without clamping, updateBuffer() below uploads past the
    // buffer capacity → OOB write / GPU corruption. We stop emitting once the capacity
    // is reached and warn (no SILENT truncation).
    const size_t maxVertices = static_cast<size_t>(MAX_DEBUG_LINES) * 2;
    const size_t requestedVertices = frame.debugLineCount * 2 + frame.debugRectCount * 8;
    vertices.reserve(std::min(requestedVertices, maxVertices));

    // Add line vertices (stop before exceeding buffer capacity)
    for (size_t i = 0; i < frame.debugLineCount && vertices.size() + 2 <= maxVertices; ++i) {
        const DebugLine& line = frame.debugLines[i];
        float r, g, b, a;
        unpackColor(line.color, r, g, b, a);

        // Vertex 1
        vertices.push_back({line.x1, line.y1, 0.0f, r, g, b, a});
        // Vertex 2
        vertices.push_back({line.x2, line.y2, 0.0f, r, g, b, a});
    }

    // Add rect vertices as 4 lines (wireframe) (stop before exceeding buffer capacity)
    for (size_t i = 0; i < frame.debugRectCount && vertices.size() + 8 <= maxVertices; ++i) {
        const DebugRect& rect = frame.debugRects[i];
        float r, g, b, a;
        unpackColor(rect.color, r, g, b, a);

        float x1 = rect.x;
        float y1 = rect.y;
        float x2 = rect.x + rect.w;
        float y2 = rect.y + rect.h;

        // Line 1: top
        vertices.push_back({x1, y1, 0.0f, r, g, b, a});
        vertices.push_back({x2, y1, 0.0f, r, g, b, a});
        // Line 2: right
        vertices.push_back({x2, y1, 0.0f, r, g, b, a});
        vertices.push_back({x2, y2, 0.0f, r, g, b, a});
        // Line 3: bottom
        vertices.push_back({x2, y2, 0.0f, r, g, b, a});
        vertices.push_back({x1, y2, 0.0f, r, g, b, a});
        // Line 4: left
        vertices.push_back({x1, y2, 0.0f, r, g, b, a});
        vertices.push_back({x1, y1, 0.0f, r, g, b, a});
    }

    if (requestedVertices > maxVertices) {
        spdlog::warn("[DebugPass] debug primitives exceed buffer capacity ({} > {} verts) "
                     "— truncated. Raise MAX_DEBUG_LINES if this is intentional.",
                     requestedVertices, maxVertices);
    }

    if (vertices.empty()) {
        return;
    }

    // Update dynamic vertex buffer
    device.updateBuffer(m_lineVB, vertices.data(),
                        static_cast<uint32_t>(vertices.size() * sizeof(DebugVertex)));

    // Set render state for debug lines
    rhi::RenderState state;
    state.blend = rhi::BlendMode::None;
    state.cull = rhi::CullMode::None;
    state.primitive = rhi::PrimitiveType::Lines;
    state.depthTest = false;
    state.depthWrite = false;
    cmd.setState(state);

    // Draw all lines
    cmd.setVertexBuffer(m_lineVB);
    cmd.draw(static_cast<uint32_t>(vertices.size()));
    cmd.submit(0, m_lineShader, 0);
}

} // namespace grove
