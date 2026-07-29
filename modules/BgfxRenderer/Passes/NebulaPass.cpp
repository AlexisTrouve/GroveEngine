#include "NebulaPass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"

namespace grove {

NebulaPass::NebulaPass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
}

void NebulaPass::setup(rhi::IRHIDevice& device) {
    // Unit quad spanning -1..1, exactly like LightPass: the vertex shader (vs_light, shared) scales
    // it by the volume's radius and moves it to its world centre, so ONE mesh serves every nebula.
    const float quadVertices[] = {
        // pos.x, pos.y, pos.z,    r,    g,    b,    a   (vertex colour unused — parameters are uniforms)
        -1.0f, -1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,
         1.0f, -1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f,    1.0f, 1.0f, 1.0f, 1.0f,
    };

    rhi::BufferDesc vbDesc;
    vbDesc.type    = rhi::BufferDesc::Vertex;
    vbDesc.size    = sizeof(quadVertices);
    vbDesc.data    = quadVertices;
    vbDesc.dynamic = false;
    vbDesc.layout  = rhi::BufferDesc::PosColor;
    m_quadVB = device.createBuffer(vbDesc);

    const uint16_t quadIndices[] = { 0, 1, 2, 0, 2, 3 };
    rhi::BufferDesc ibDesc;
    ibDesc.type    = rhi::BufferDesc::Index;
    ibDesc.size    = sizeof(quadIndices);
    ibDesc.data    = quadIndices;
    ibDesc.dynamic = false;
    m_quadIB = device.createBuffer(ibDesc);

    // ⚠️ `u_light` is set here too, and the name is the shared vertex shader's, not a copy-paste
    // slip: vs_light reads (cx, cy, radius) from it to place the quad. A medium is not a lamp, but
    // the geometry problem is identical and duplicating the shader to rename a uniform would be
    // worse than this comment.
    m_placementUniform = device.createUniform("u_light", 1);
    m_fogUniform      = device.createUniform("u_fog", 1);
    m_fogColorUniform = device.createUniform("u_fogColor", 1);
}

void NebulaPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_placementUniform);
    device.destroy(m_fogUniform);
    device.destroy(m_fogColorUniform);
    // m_shader belongs to ShaderManager, like every other pass.
}

void NebulaPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    (void)device;

    // No nebula this frame: record nothing. The occlusion map then keeps whatever the rect matter
    // wrote, or its white clear — vacuum — and a game with no media pays nothing here.
    if (frame.nebulae == nullptr || frame.nebulaCount == 0) {
        return;
    }

    // MULTIPLICATIVE, like every other kind of matter: overlapping media compose by product, in any
    // order. Alpha blending would make the last volume drawn govern the overlap.
    rhi::RenderState state;
    state.blend      = rhi::BlendMode::Multiply;
    state.cull       = rhi::CullMode::None;
    state.depthTest  = false;
    state.depthWrite = false;

    // ONE DRAW PER VOLUME, deliberately not instanced — the same reasoning as the lights: a scene
    // holds a handful of media, so the per-draw cost is noise against one more vertex layout to keep
    // in sync with a shader.
    for (size_t i = 0; i < frame.nebulaCount; ++i) {
        const NebulaCommand& n = frame.nebulae[i];
        if (n.radius <= 0.0f) continue;   // belt to the collector's braces: no divide by zero

        const float placement[4] = { n.cx, n.cy, n.radius, 0.0f };   // consumed by vs_light
        const float fog[4]       = { n.density, n.scatter, 0.0f, 0.0f };
        const float fogColor[4]  = { n.r, n.g, n.b, 1.0f };

        // State is consumed per submit, so it is set per volume — the same lesson the tilemap and
        // light passes already paid for.
        cmd.setState(state);
        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        cmd.setUniform(m_placementUniform, placement, 1);
        cmd.setUniform(m_fogUniform, fog, 1);
        cmd.setUniform(m_fogColorUniform, fogColor, 1);
        cmd.drawIndexed(6, 0);
        cmd.submit(OcclusionPass::kOcclusionView, m_shader, 0);
    }
}

} // namespace grove
