#include "DebugPass.h"
#include "../RHI/RHIDevice.h"

namespace grove {

DebugPass::DebugPass(rhi::ShaderHandle shader)
    : m_lineShader(shader)
{
}

void DebugPass::setup(rhi::IRHIDevice& device) {
    // Create dynamic vertex buffer for debug lines
    rhi::BufferDesc vbDesc;
    vbDesc.type = rhi::BufferDesc::Vertex;
    vbDesc.size = MAX_DEBUG_LINES * 2 * sizeof(float) * 6; // 2 verts per line, pos + color
    vbDesc.data = nullptr;
    vbDesc.dynamic = true;
    m_lineVB = device.createBuffer(vbDesc);
}

void DebugPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_lineVB);
    // Note: m_lineShader is owned by ShaderManager, not destroyed here
}

void DebugPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    // Skip if no debug primitives
    if (frame.debugLineCount == 0 && frame.debugRectCount == 0) {
        return;
    }

    // Set render state for debug (no blending, no depth)
    rhi::RenderState state;
    state.blend = rhi::BlendMode::None;
    state.cull = rhi::CullMode::None;
    state.depthTest = false;
    state.depthWrite = false;
    cmd.setState(state);

    // Build vertex data for lines
    // Each line needs 2 vertices with position (x, y, z) and color (r, g, b, a)

    if (frame.debugLineCount > 0) {
        // TODO: Build line vertex data from frame.debugLines and update buffer
        // device.updateBuffer(m_lineVB, lineVertices, lineVertexDataSize);
        cmd.setVertexBuffer(m_lineVB);
        cmd.draw(static_cast<uint32_t>(frame.debugLineCount * 2));
        cmd.submit(0, m_lineShader, 0);
    }

    // Rectangles are rendered as line loops or filled quads
    // For now, just lines (wireframe)
    if (frame.debugRectCount > 0) {
        // Each rect = 4 lines = 8 vertices
        // TODO: Build rect line data and draw
        (void)device; // Will be used when implementing rect rendering
    }
}

} // namespace grove
