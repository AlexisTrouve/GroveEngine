#include "PresentPass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"

#include <grove/light/Tonemap.h>
#include <grove/light/Grade.h>

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
    m_grade0Uniform  = device.createUniform("u_grade0", 1);
    m_grade1Uniform  = device.createUniform("u_grade1", 1);
}

void PresentPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_sceneSampler);
    device.destroy(m_bloomSampler);
    device.destroy(m_presentUniform);
    device.destroy(m_grade0Uniform);
    device.destroy(m_grade1Uniform);
}

void PresentPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    (void)device;

    // Le contournement — mais sur DEUX réglages désormais (plan T). Le bloom et le tonemapping
    // activent la même plomberie sans dépendre l'un de l'autre : un jeu peut vouloir une courbe
    // d'exposition sans aucune lueur, et l'inverse. Ne tester que le bloom rendrait le tonemapping
    // inatteignable, ce qui est exactement le genre de chaînon jamais câblé qu'on prend ensuite pour
    // un bug de shader.
    // TROIS reglages activent desormais cette passe, et aucun ne depend des autres : une lueur, une
    // courbe d'exposition, un etalonnage. Oublier l'un d'eux le rendrait INATTEIGNABLE -- le « chainon
    // jamais cable », deja rencontre au plan T et attrape par sabotage.
    const bool tonemapActive = (frame.tonemap.mode != light::TonemapMode::None);
    const bool gradeActive   = !light::gradeIsNeutral(frame.grade);
    if (!(frame.bloom.intensity > 0.0f) && !tonemapActive && !gradeActive) {
        return;
    }

    // (intensité de lueur, exposition, mode) — le mode voyage en float parce qu'un uniform vec4 est ce
    // que le RHI expose ; l'énumération reste l'unique source de vérité, et la conversion est ici, à
    // un seul endroit.
    const float present[4] = {
        frame.bloom.intensity,
        frame.tonemap.exposure,
        static_cast<float>(static_cast<int>(frame.tonemap.mode)),
        0.0f,
    };

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
    // L'etalonnage (plan G). Les neutres traversent le shader sans rien changer : teinte blanche =
    // multiplication par 1, contraste 1 = pivot inchange, saturation 1 = interpolation nulle. Il n'y a
    // donc pas de branche a ecrire pour le cas neutre -- la formule EST l'identite.
    const float grade0[4] = { frame.grade.tintR, frame.grade.tintG, frame.grade.tintB,
                              frame.grade.contrast };
    const float grade1[4] = { frame.grade.saturation, 0.0f, 0.0f, 0.0f };

    cmd.setUniform(m_presentUniform, present, 1);
    cmd.setUniform(m_grade0Uniform, grade0, 1);
    cmd.setUniform(m_grade1Uniform, grade1, 1);
    cmd.drawIndexed(6, 0);
    cmd.submit(kPresentView, m_shader, 0);
}

} // namespace grove
