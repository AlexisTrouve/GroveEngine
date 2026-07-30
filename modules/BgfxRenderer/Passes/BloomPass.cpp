#include "BloomPass.h"
#include "../RHI/RHIDevice.h"
#include "../RHI/RHICommandBuffer.h"

#include <grove/light/Bloom.h>

namespace grove {

BloomPass::BloomPass(rhi::ShaderHandle extractShader, rhi::ShaderHandle blurShader)
    : m_extractShader(extractShader)
    , m_blurShader(blurShader)
{
}

void BloomPass::setup(rhi::IRHIDevice& device) {
    // Quad plein écran DÉJÀ en espace de clip, comme celui du composite : aucune vue ni projection
    // n'est nécessaire, et les trois étapes couvrent l'écran quel que soit l'état de la caméra monde.
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
    m_extractUniform = device.createUniform("u_bloomExtract", 1);
    m_blurUniform    = device.createUniform("u_bloomBlur", 1);
    // 2 vec4 : le demi-noyau tient dans (w0,w1,w2,w3) + (w4,0,0,0).
    m_weightsUniform = device.createUniform("u_bloomWeights", 2);
}

void BloomPass::shutdown(rhi::IRHIDevice& device) {
    device.destroy(m_quadVB);
    device.destroy(m_quadIB);
    device.destroy(m_sceneSampler);
    device.destroy(m_extractUniform);
    device.destroy(m_blurUniform);
    device.destroy(m_weightsUniform);
    // Les shaders appartiennent au ShaderManager, comme dans toutes les autres passes.
}

void BloomPass::execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) {
    (void)device;

    // LE contournement. `intensity == 0` veut dire qu'aucun jeu n'a demandé de post-traitement : pas
    // un changement d'état, pas un draw. Voir l'en-tête et lighting-bloom.md §3.
    if (!(frame.bloom.intensity > 0.0f)) {
        return;
    }
    // Sans dimensions, un décalage d'UV n'a pas de sens — et une division par zéro suivrait.
    if (m_quarterW == 0 || m_quarterH == 0 || m_fullW == 0 || m_fullH == 0) {
        return;
    }

    // Opaque, sans profondeur, plein écran : chaque étape REMPLACE le contenu de sa cible. Un mélange
    // ferait s'accumuler la lueur de la frame précédente — une traînée fantôme qu'on prendrait pour un
    // effet de rémanence voulu.
    rhi::RenderState state;
    state.blend      = rhi::BlendMode::None;
    state.cull       = rhi::CullMode::None;
    state.depthTest  = false;
    state.depthWrite = false;

    // ---- 1. Extraction : pleine résolution -> quart -------------------------------------------
    // Le décalage des quatre taps est un DEMI-texel de la source. Un texel entier sauterait le
    // voisinage immédiat et laisserait des trous d'un pixel sur deux ; un demi-texel place les quatre
    // taps aux centres des quatre quadrants que ce pixel de sortie recouvre.
    const float extractUniform[4] = {
        frame.bloom.threshold,
        light::bloomKnee(frame.bloom.threshold),
        0.5f / static_cast<float>(m_fullW),
        0.5f / static_cast<float>(m_fullH),
    };

    cmd.setState(state);
    cmd.setVertexBuffer(m_quadVB);
    cmd.setIndexBuffer(m_quadIB);
    cmd.setTexture(0, m_hdrTex, m_sceneSampler);
    cmd.setUniform(m_extractUniform, extractUniform, 1);
    cmd.drawIndexed(6, 0);
    cmd.submit(kExtractView, m_extractShader, 0);

    // ---- Les poids du noyau, TÉLÉVERSÉS ------------------------------------------------------
    // Ils viennent de l'oracle C++ et jamais du shader. Un noyau non normalisé changerait la
    // luminosité globale de la lueur, ce qui se lirait comme un mauvais réglage d'`intensity` et se
    // compenserait au mauvais bouton. BloomMathUnit prouve que la somme réfléchie vaut 1.
    //
    // Sigma est FIXE à 2 texels : la FORME du noyau ne change pas, c'est son ÉTENDUE qui suit le
    // rayon publié (via l'écartement des taps ci-dessous). Faire varier sigma à écartement constant
    // aurait plafonné le rayon à 4 texels quoi qu'on demande.
    float w[5] = {0, 0, 0, 0, 0};
    light::bloomHalfKernel(2.0f, w);
    const float weights[8] = { w[0], w[1], w[2], w[3],
                               w[4], 0.0f, 0.0f, 0.0f };

    // Écartement des taps, en texels du quart de résolution. `radius` est en PIXELS ÉCRAN, et le
    // rayon naturel (écartement 1) vaut 16 px — voir kNaturalRadiusPx.
    const float spacing = frame.bloom.radius / kNaturalRadiusPx;

    // ---- 2. Flou horizontal : A -> B ---------------------------------------------------------
    const float blurH[4] = { spacing / static_cast<float>(m_quarterW), 0.0f, 0.0f, 0.0f };
    cmd.setState(state);
    cmd.setVertexBuffer(m_quadVB);
    cmd.setIndexBuffer(m_quadIB);
    cmd.setTexture(0, m_bloomATex, m_sceneSampler);
    cmd.setUniform(m_blurUniform, blurH, 1);
    cmd.setUniform(m_weightsUniform, weights, 2);
    cmd.drawIndexed(6, 0);
    cmd.submit(kBlurHView, m_blurShader, 0);

    // ---- 3. Flou vertical : B -> A -----------------------------------------------------------
    // Le résultat revient dans A, ce dont la passe de présentation dépend (elle reçoit la texture A).
    const float blurV[4] = { 0.0f, spacing / static_cast<float>(m_quarterH), 0.0f, 0.0f };
    cmd.setState(state);
    cmd.setVertexBuffer(m_quadVB);
    cmd.setIndexBuffer(m_quadIB);
    cmd.setTexture(0, m_bloomBTex, m_sceneSampler);
    cmd.setUniform(m_blurUniform, blurV, 1);
    cmd.setUniform(m_weightsUniform, weights, 2);
    cmd.drawIndexed(6, 0);
    cmd.submit(kBlurVView, m_blurShader, 0);
}

} // namespace grove
