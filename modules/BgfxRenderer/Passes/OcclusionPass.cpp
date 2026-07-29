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
    const size_t wallCount   = (frame.occluders != nullptr) ? frame.occluderCount : 0;
    const size_t filterCount = (frame.filters   != nullptr) ? frame.filterCount   : 0;
    const size_t fogCount    = (frame.fogs      != nullptr) ? frame.fogCount      : 0;

    // No matter of any kind: record nothing. The view's WHITE clear alone then leaves the map at
    // vacuum, which the light march multiplies by 1 — a game with neither walls, stained glass nor
    // fog pays a clear and no more.
    if (wallCount == 0 && filterCount == 0 && fogCount == 0) {
        return;
    }

    // OVERFLOW GUARD, the same one DebugPass already needed: the vertex buffer is a FIXED
    // allocation, so a scene with more matter than it holds must be truncated loudly, not written
    // past. Uploading beyond the buffer corrupts GPU memory instead of losing a wall.
    //
    // Walls and filters share the ONE budget because they share the one buffer — counting them
    // separately would let their sum overrun it while each looked within bounds.
    const size_t maxRects = MAX_OCCLUDERS;
    const size_t walls    = (wallCount > maxRects) ? maxRects : wallCount;
    const size_t filters  = (walls + filterCount > maxRects) ? (maxRects - walls) : filterCount;
    const size_t used     = walls + filters;
    const size_t fogs     = (used + fogCount > maxRects) ? (maxRects - used) : fogCount;
    if (wallCount + filterCount + fogCount > maxRects) {
        static bool warned = false;
        if (!warned) {
            spdlog::warn("[OcclusionPass] {} occluders + {} filters + {} fog volumes exceed the {} "
                         "cap — truncated. Raise MAX_OCCLUDERS if this is intentional.",
                         wallCount, filterCount, fogCount, maxRects);
            warned = true;
        }
    }

    std::vector<OccVertex> verts;
    verts.reserve((walls + filters + fogs) * 6);

    // QUOI : one quad per rect, coloured by what that matter TRANSMITS per unit of length.
    // POURQUOI walls and filters share this loop shape: they are the same mechanism at two points of
    //         one scale. A wall transmits nothing, a filter transmits a colour — see
    //         lighting-transmittance-core.md. Two loops rather than one only because the two
    //         primitives carry different fields.
    auto pushRect = [&verts](float x, float y, float w, float h, float r, float g, float b) {
        // x,y is the top-left CORNER (anchor convention), so the far corner is +w,+h.
        const float x0 = x,     y0 = y;
        const float x1 = x + w, y1 = y + h;
        const float a  = 1.0f;

        verts.push_back({x0, y0, 0.0f, r, g, b, a});
        verts.push_back({x1, y0, 0.0f, r, g, b, a});
        verts.push_back({x1, y1, 0.0f, r, g, b, a});

        verts.push_back({x0, y0, 0.0f, r, g, b, a});
        verts.push_back({x1, y1, 0.0f, r, g, b, a});
        verts.push_back({x0, y1, 0.0f, r, g, b, a});
    };

    for (size_t i = 0; i < walls; ++i) {
        const OccluderCommand& o = frame.occluders[i];
        // BLACK = transmittance 0. The wall is not a branch anywhere downstream: a zero annihilates
        // the running product the light march accumulates, and everything beyond it on that ray is
        // dark as an arithmetic consequence.
        pushRect(o.x, o.y, o.w, o.h, 0.0f, 0.0f, 0.0f);
    }

    for (size_t i = 0; i < filters; ++i) {
        const FilterCommand& f = frame.filters[i];
        // The collector already converted the author's tint into a PER-UNIT transmittance, so this
        // pass writes it verbatim. A wall is the degenerate case of this very quad.
        pushRect(f.x, f.y, f.w, f.h, f.r, f.g, f.b);
    }

    for (size_t i = 0; i < fogs; ++i) {
        const FogCommand& f = frame.fogs[i];
        // Third and last way of feeding one map: exp(-density). Identical quad, identical blend —
        // only the number differs, which is exactly what the socle claimed and is now three for three.
        pushRect(f.x, f.y, f.w, f.h, f.r, f.g, f.b);
    }

    if (verts.empty()) return;

    device.updateBuffer(m_vb, verts.data(), static_cast<uint32_t>(verts.size() * sizeof(OccVertex)));

    // MULTIPLICATIVE, so that overlapping matter COMPOSES instead of overwriting.
    //
    // ⚠️ This was `None` while only walls existed, and that was invisible: black over black is
    // black whichever wins. The moment a filter exists, an overwrite would make the last pane drawn
    // govern the overlap — the depth-ordering problem the socle claims not to have. A product has
    // no order, which is exactly why the socle claims it.
    //
    // Walls are byte-identical under this change: their source is (0,0,0), and zero times anything
    // is still zero.
    rhi::RenderState state;
    state.blend      = rhi::BlendMode::Multiply;
    state.cull       = rhi::CullMode::None;
    state.depthTest  = false;
    state.depthWrite = false;

    cmd.setState(state);
    cmd.setVertexBuffer(m_vb);
    cmd.draw(static_cast<uint32_t>(verts.size()), 0);
    cmd.submit(kOcclusionView, m_shader, 0);
}

} // namespace grove
