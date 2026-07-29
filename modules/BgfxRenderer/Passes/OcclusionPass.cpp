#include "OcclusionPass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"

#include <spdlog/spdlog.h>
#include <vector>

namespace grove {

namespace {
// Same PosColor layout the "color" program consumes (DebugPass / SectorPass use it too).
struct OccVertex {
    float x, y, z;
    float r, g, b, a;
};
static_assert(sizeof(OccVertex) == 28, "OccVertex must match the PosColor layout");
} // namespace

OcclusionPass::OcclusionPass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
}

void OcclusionPass::setup(rhi::IRHIDevice& device) {
    rhi::BufferDesc vbDesc;
    vbDesc.type    = rhi::BufferDesc::Vertex;
    vbDesc.size    = static_cast<size_t>(MAX_OCCLUDERS) * 6 * sizeof(OccVertex);
    vbDesc.data    = nullptr;
    vbDesc.dynamic = true;
    vbDesc.layout  = rhi::BufferDesc::PosColor;
    m_vb = device.createBuffer(vbDesc);
}

void OcclusionPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_vb);
    // m_shader belongs to ShaderManager, like every other pass.
}

void OcclusionPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    // Nothing to occlude: record nothing. The view's WHITE clear alone then leaves the map at
    // vacuum, which the light march multiplies by 1 — a game with no walls pays a clear and no more.
    if (frame.occluders == nullptr || frame.occluderCount == 0) {
        return;
    }

    // OVERFLOW GUARD, the same one DebugPass already needed: the vertex buffer is a FIXED
    // allocation, so a scene with more walls than it holds must be truncated loudly, not written
    // past. Uploading beyond the buffer corrupts GPU memory instead of losing a wall.
    const size_t maxRects = MAX_OCCLUDERS;
    const size_t count    = (frame.occluderCount > maxRects) ? maxRects : frame.occluderCount;
    if (frame.occluderCount > maxRects) {
        static bool warned = false;
        if (!warned) {
            spdlog::warn("[OcclusionPass] {} occluders exceed the {} cap — truncated. Raise "
                         "MAX_OCCLUDERS if this is intentional.", frame.occluderCount, maxRects);
            warned = true;
        }
    }

    std::vector<OccVertex> verts;
    verts.reserve(count * 6);

    for (size_t i = 0; i < count; ++i) {
        const OccluderCommand& o = frame.occluders[i];
        // x,y is the top-left CORNER (anchor convention), so the far corner is +w,+h.
        const float x0 = o.x,        y0 = o.y;
        const float x1 = o.x + o.w,  y1 = o.y + o.h;

        // BLACK = transmittance 0. The wall is not a branch anywhere downstream: a zero annihilates
        // the running product the light march accumulates, and everything beyond it on that ray is
        // dark as an arithmetic consequence.
        const float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

        verts.push_back({x0, y0, 0.0f, r, g, b, a});
        verts.push_back({x1, y0, 0.0f, r, g, b, a});
        verts.push_back({x1, y1, 0.0f, r, g, b, a});

        verts.push_back({x0, y0, 0.0f, r, g, b, a});
        verts.push_back({x1, y1, 0.0f, r, g, b, a});
        verts.push_back({x0, y1, 0.0f, r, g, b, a});
    }

    if (verts.empty()) return;

    device.updateBuffer(m_vb, verts.data(), static_cast<uint32_t>(verts.size() * sizeof(OccVertex)));

    // OPAQUE. The occlusion map records what matter IS, not what it looks like: blending a wall
    // would let a second wall behind it lighten the first, which is the opposite of occlusion.
    rhi::RenderState state;
    state.blend      = rhi::BlendMode::None;
    state.cull       = rhi::CullMode::None;
    state.depthTest  = false;
    state.depthWrite = false;

    cmd.setState(state);
    cmd.setVertexBuffer(m_vb);
    cmd.draw(static_cast<uint32_t>(verts.size()), 0);
    cmd.submit(kOcclusionView, m_shader, 0);
}

} // namespace grove
