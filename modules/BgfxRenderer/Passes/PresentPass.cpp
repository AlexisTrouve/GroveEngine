#include "PresentPass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"

namespace grove {

PresentPass::PresentPass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
}

void PresentPass::setup(rhi::IRHIDevice& device) {
    // Le même quad en espace de clip que le composite et le bloom.
    const float quadVertices[] = {
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
    m_bloomSampler   = device.createUniform("s_bloom", 1);
    m_presentUniform = device.createUniform("u_present", 1);
}

void PresentPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_sceneSampler);
    device.destroy(m_bloomSampler);
    device.destroy(m_presentUniform);
}

void PresentPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    (void)device;

    // Le contournement, identique aux deux autres passes de la chaîne.
    if (!(frame.bloom.intensity > 0.0f)) {
        return;
    }

    const float present[4] = { frame.bloom.intensity, 0.0f, 0.0f, 0.0f };

    // Opaque : cette passe produit la couleur finale du monde, elle ne se mélange à rien. Le HUD, lui,
    // se mélangera par-dessus — il est soumis après.
    rhi::RenderState state;
    state.blend      = rhi::BlendMode::None;
    state.cull       = rhi::CullMode::None;
    state.depthTest  = false;
    state.depthWrite = false;

    cmd.setState(state);
    cmd.setVertexBuffer(m_quadVB);
    cmd.setIndexBuffer(m_quadIB);
    cmd.setTexture(0, m_hdrTex, m_sceneSampler);
    cmd.setTexture(1, m_bloomTex, m_bloomSampler);
    cmd.setUniform(m_presentUniform, present, 1);
    cmd.drawIndexed(6, 0);
    cmd.submit(kPresentView, m_shader, 0);
}

} // namespace grove
