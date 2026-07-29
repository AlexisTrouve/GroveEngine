#include "LightPass.h"
#include "CompositePass.h"        // kLightView — the view lights accumulate into
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"

namespace grove {

LightPass::LightPass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
}

void LightPass::setup(rhi::IRHIDevice& device) {
    // Unit quad spanning -1..1. The vertex shader scales it by the light's radius and translates it
    // to the light's world centre, so ONE mesh serves every light whatever its size.
    const float quadVertices[] = {
        // pos.x, pos.y, pos.z,    r,    g,    b,    a   (colour unused — the light colour is a uniform)
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

    m_lightUniform      = device.createUniform("u_light", 1);
    m_lightColorUniform = device.createUniform("u_lightColor", 1);
}

void LightPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_lightUniform);
    device.destroy(m_lightColorUniform);
    // m_shader belongs to ShaderManager, like every other pass.
}

void LightPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    (void)device;

    // No lights this frame: record nothing. The light target then stays at its black clear, the
    // composite multiplies by the ambient alone, and a game that lights nothing pays nothing here.
    if (frame.lights == nullptr || frame.lightCount == 0) {
        return;
    }

    // ADDITIVE, depth-less: light is a SUM. Alpha blending would let the last lamp drawn hide the
    // ones under it instead of adding to them, which is the difference between "lighting" and
    // "painting discs".
    rhi::RenderState state;
    state.blend      = rhi::BlendMode::Additive;
    state.cull       = rhi::CullMode::None;
    state.depthTest  = false;
    state.depthWrite = false;

    for (size_t i = 0; i < frame.lightCount; ++i) {
        const LightCommand& l = frame.lights[i];

        // A degenerate light draws nothing rather than a division by zero in the shader. The
        // collector already drops radius <= 0, so this is the belt to that braces.
        if (l.radius <= 0.0f) continue;

        const float light[4]      = { l.cx, l.cy, l.radius, l.intensity };
        const float lightColor[4] = { l.r, l.g, l.b, 1.0f };

        // State is consumed per submit, so it is set per light — the lesson already paid for by the
        // tilemap pass ("bgfx::setState is consumed per submit -> emit it per chunk, not once").
        cmd.setState(state);
        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        cmd.setUniform(m_lightUniform, light, 1);
        cmd.setUniform(m_lightColorUniform, lightColor, 1);
        cmd.drawIndexed(6, 0);
        cmd.submit(CompositePass::kLightView, m_shader, 0);
    }
}

} // namespace grove
