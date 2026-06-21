#include "SectorPass.h"
#include "SectorMesh.h"
#include "../RHI/RHIDevice.h"
#include "../Frame/FramePacket.h"
#include <vector>
#include <spdlog/spdlog.h>

namespace grove {

// Position + colour, 28 bytes — matches the PosColor layout the "debug"/color shader expects.
struct SectorVertex {
    float x, y, z;
    float r, g, b, a;
};
static_assert(sizeof(SectorVertex) == 28, "SectorVertex must be 28 bytes (PosColor)");

static void unpackColor(uint32_t color, float& r, float& g, float& b, float& a) {
    r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    a = static_cast<float>(color & 0xFF) / 255.0f;
}

// Tessellate a set of sectors into coloured triangle vertices, stopping before `maxVerts` (no OOB
// upload, no silent truncation — warn if it had to drop sectors).
static void buildSectorVerts(const SectorCommand* sectors, size_t count,
                             std::vector<SectorVertex>& out, size_t maxVerts) {
    bool dropped = false;
    for (size_t i = 0; i < count; ++i) {
        const SectorCommand& s = sectors[i];
        float r, g, b, a; unpackColor(s.color, r, g, b, a);
        const int steps = geom::sectorSteps(s.a1 - s.a0);
        std::vector<float> xy;                                  // pure (x,y) triangle-list
        geom::appendSector(xy, s.cx, s.cy, s.r0, s.r1, s.a0, s.a1, steps);
        if (out.size() + xy.size() / 2 > maxVerts) { dropped = true; break; }
        for (size_t v = 0; v + 1 < xy.size(); v += 2)
            out.push_back({xy[v], xy[v + 1], 0.0f, r, g, b, a});
    }
    if (dropped) spdlog::warn("[SectorPass] sector vertices exceed buffer capacity — truncated.");
}

SectorPass::SectorPass(rhi::ShaderHandle shader) : m_shader(shader) {}

void SectorPass::setup(rhi::IRHIDevice& device) {
    rhi::BufferDesc d;
    d.type = rhi::BufferDesc::Vertex;
    d.size = MAX_SECTOR_VERTICES * sizeof(SectorVertex);
    d.data = nullptr;
    d.dynamic = true;
    d.layout = rhi::BufferDesc::PosColor;   // vec3 position + vec4 colour
    m_worldVB = device.createBuffer(d);
    m_hudVB = device.createBuffer(d);
}

void SectorPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_worldVB);
    device.destroy(m_hudVB);
}

void SectorPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    if (frame.sectorCount == 0 && frame.hudSectorCount == 0) return;
    if (!m_shader.isValid()) return;

    rhi::RenderState state;
    state.blend = rhi::BlendMode::Alpha;          // wedges/backgrounds are semi-transparent
    state.cull = rhi::CullMode::None;
    state.primitive = rhi::PrimitiveType::Triangles;
    state.depthTest = false;
    state.depthWrite = false;

    // World sectors -> view 0 (zoom with the camera).
    if (frame.sectorCount > 0) {
        std::vector<SectorVertex> verts;
        buildSectorVerts(frame.sectors, frame.sectorCount, verts, MAX_SECTOR_VERTICES);
        if (!verts.empty()) {
            device.updateBuffer(m_worldVB, verts.data(),
                                static_cast<uint32_t>(verts.size() * sizeof(SectorVertex)));
            cmd.setState(state);
            cmd.setVertexBuffer(m_worldVB);
            cmd.draw(static_cast<uint32_t>(verts.size()));
            cmd.submit(0, m_shader, 0);
        }
    }

    // HUD sectors -> view 1 (fixed screen-space overlay; e.g. the action wheel). Separate VB so this
    // upload doesn't clobber the world draw recorded above.
    if (frame.hudSectorCount > 0) {
        std::vector<SectorVertex> verts;
        buildSectorVerts(frame.hudSectors, frame.hudSectorCount, verts, MAX_SECTOR_VERTICES);
        if (!verts.empty()) {
            device.updateBuffer(m_hudVB, verts.data(),
                                static_cast<uint32_t>(verts.size() * sizeof(SectorVertex)));
            cmd.setState(state);
            cmd.setVertexBuffer(m_hudVB);
            cmd.draw(static_cast<uint32_t>(verts.size()));
            cmd.submit(1, m_shader, 0);
        }
    }
}

} // namespace grove
