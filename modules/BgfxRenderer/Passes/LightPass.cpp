#include "LightPass.h"
#include "CompositePass.h"        // kLightView — the view lights accumulate into
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"
#include <grove/light/Light.h>   // kConeSoftFraction — the shader mirrors this curve
#include <cmath>

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
    m_lightConeUniform  = device.createUniform("u_lightCone", 1);
    m_occlusionSampler  = device.createUniform("s_occlusion", 1);
}

void LightPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_lightUniform);
    device.destroy(m_lightColorUniform);
    device.destroy(m_lightConeUniform);
    device.destroy(m_occlusionSampler);
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

        // Cone, resolved to what the shader needs: an axis and two cosines. Doing the trigonometry
        // HERE rather than per pixel is the point of the cosine formulation — the fragment stage
        // only ever does a dot product.
        //
        // Omni ships bounds BELOW the cosine range, so the shader's smoothstep saturates to 1 for
        // every direction with no branch: an omni light comes out exactly as it did before cones.
        //
        // ⚠️ The two bounds must DIFFER. smoothstep(e, e, x) with equal edges is undefined in GLSL —
        // it would divide by zero, and "undefined" here means it might look right on this driver and
        // wrong on the next. -2 and -1 bracket the whole valid cosine range [-1, 1] instead.
        constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
        float cone[4] = { 1.0f, 0.0f, -2.0f, -1.0f };
        if (l.spreadDeg < 360.0f && l.spreadDeg > 0.0f) {
            const float half = 0.5f * l.spreadDeg * kDeg2Rad;
            cone[0] = std::cos(l.dirDeg * kDeg2Rad);
            cone[1] = std::sin(l.dirDeg * kDeg2Rad);
            cone[2] = std::cos(half);                                                    // rim
            cone[3] = std::cos(half * (1.0f - grove::light::kConeSoftFraction));         // full
        }

        // State is consumed per submit, so it is set per light — the lesson already paid for by the
        // tilemap pass ("bgfx::setState is consumed per submit -> emit it per chunk, not once").
        cmd.setState(state);
        cmd.setVertexBuffer(m_quadVB);
        cmd.setIndexBuffer(m_quadIB);
        cmd.setUniform(m_lightUniform, light, 1);
        cmd.setUniform(m_lightColorUniform, lightColor, 1);
        cmd.setUniform(m_lightConeUniform, cone, 1);
        cmd.setTexture(0, m_occlusionTex, m_occlusionSampler);
        cmd.drawIndexed(6, 0);
        cmd.submit(CompositePass::kLightView, m_shader, 0);
    }
}

} // namespace grove
