#include "FadePass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"

namespace grove {

FadePass::FadePass(rhi::ShaderHandle shader)
    : m_shader(shader)
{
}

void FadePass::setup(rhi::IRHIDevice& device) {
    // Le même quad en espace de clip que le composite, le bloom et la présentation : aucune vue ni
    // projection, donc rien ne peut le déplacer.
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

    m_fadeUniform = device.createUniform("u_fade", 1);
}

void FadePass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_fadeUniform);
}

void FadePass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    (void)device;

    // Le contournement, et le plus simple des quatre : rien à défaire, rien à libérer.
    if (!(frame.fade.amount > 0.0f)) {
        return;
    }

    // La couleur en 0..1, alpha = `amount`. L'octet alpha de la couleur publiée est IGNORÉ : c'est
    // `amount` qui pilote le mélange, et avoir deux alphas serait une invitation à les désaccorder.
    const float fade[4] = {
        static_cast<float>((frame.fade.color >> 24) & 0xFF) / 255.0f,
        static_cast<float>((frame.fade.color >> 16) & 0xFF) / 255.0f,
        static_cast<float>((frame.fade.color >>  8) & 0xFF) / 255.0f,
        frame.fade.amount,
    };

    // ⚠️ MÉLANGE ALPHA, et c'est tout le mécanisme : `dst = a·src + (1−a)·dst` EST le mix voulu.
    //    En ADDITIF, un fondu au NOIR ne ferait rien du tout (ajouter zéro) — et comme c'est le cas
    //    d'usage le plus courant, la faute se présenterait comme « le fondu ne marche pas ».
    rhi::RenderState state;
    state.blend      = rhi::BlendMode::Alpha;
    state.cull       = rhi::CullMode::None;
    state.depthTest  = false;
    state.depthWrite = false;

    cmd.setState(state);
    cmd.setVertexBuffer(m_quadVB);
    cmd.setIndexBuffer(m_quadIB);
    cmd.setUniform(m_fadeUniform, fade, 1);
    cmd.drawIndexed(6, 0);
    cmd.submit(kFadeView, m_shader, 0);
}

} // namespace grove
