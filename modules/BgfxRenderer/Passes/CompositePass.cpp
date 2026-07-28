#include "CompositePass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"

namespace grove {

CompositePass::CompositePass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
}

void CompositePass::setup(rhi::IRHIDevice& device) {
    // Full-screen quad in CLIP SPACE. Unlike the sprite quad (a unit quad transformed per instance),
    // this one is already in normalised device coordinates, so the composite needs no view/projection
    // and is immune to the world camera — it covers the screen whatever the zoom or pan.
    const float quadVertices[] = {
        // pos.x, pos.y, pos.z,    r,    g,    b,    a   (colour unused, layout must match PosColor)
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

    m_sceneSampler   = device.createUniform("s_scene", 1);
    m_lightSampler   = device.createUniform("s_light", 1);
    m_ambientUniform = device.createUniform("u_ambient", 1);
}

void CompositePass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_sceneSampler);
    device.destroy(m_lightSampler);
    device.destroy(m_ambientUniform);
    // m_shader is owned by ShaderManager, like every other pass.
}

void CompositePass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    (void)device;

    // THE bypass. `ambientColor == 0` means no game asked for lighting, so this pass must cost
    // nothing at all — not a state change, not a draw. See the header and lighting-2d.md §3.
    if (frame.ambientColor == 0) {
        return;
    }

    // Ambient as linear 0..1. The alpha byte is deliberately IGNORED: the ambient term multiplies
    // the scene, it does not blend with it, so an alpha here would have no meaning to give.
    const float ambient[4] = {
        static_cast<float>((frame.ambientColor >> 24) & 0xFF) / 255.0f,
        static_cast<float>((frame.ambientColor >> 16) & 0xFF) / 255.0f,
        static_cast<float>((frame.ambientColor >>  8) & 0xFF) / 255.0f,
        1.0f,
    };

    // Opaque, depth-less, full-screen: the composite REPLACES the backbuffer content rather than
    // blending into it — it is the frame's final colour, not a layer on top of one.
    rhi::RenderState state;
    state.blend      = rhi::BlendMode::None;
    state.cull       = rhi::CullMode::None;
    state.depthTest  = false;
    state.depthWrite = false;

    cmd.setState(state);
    cmd.setVertexBuffer(m_quadVB);
    cmd.setIndexBuffer(m_quadIB);
    cmd.setTexture(0, m_sceneTex, m_sceneSampler);
    cmd.setTexture(1, m_lightTex, m_lightSampler);
    cmd.setUniform(m_ambientUniform, ambient, 1);
    cmd.drawIndexed(6, 0);
    cmd.submit(kCompositeView, m_shader, 0);
}

} // namespace grove
