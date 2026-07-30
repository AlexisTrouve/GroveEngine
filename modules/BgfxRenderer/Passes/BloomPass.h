#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"

namespace grove {

// ============================================================================
// BloomPass — extrait le sur-brillant de la frame composée et l'étale (plan B, tranche B3).
//
// QUOI  : trois quads plein écran sur trois vues. Extraction (pleine résolution → quart), puis flou
//         gaussien séparable horizontal, puis vertical, en ping-pong entre deux cibles au quart.
//
// POURQUOI une seule passe pour trois draws plutôt que trois classes : les trois étapes partagent
//         leur quad, leurs samplers et leurs poids de noyau, et elles n'ont aucun sens l'une sans les
//         autres — les séparer donnerait trois fichiers qui doivent être modifiés ensemble. C'est le
//         même arbitrage que SpritePass, qui soumet dans la vue monde ET la vue HUD.
//
// POURQUOI le quart de résolution : la réduction elle-même moyenne 4×4 pixels, donc une partie du
//         flou est déjà faite par l'échantillonnage bilinéaire — c'est de la qualité gratuite autant
//         qu'une économie. Une lueur est basse fréquence par nature ; la résoudre au pixel serait
//         payer pour une information qu'on s'apprête à étaler.
//
// ⚠️ CONTOURNEMENT À COÛT NUL : `frame.bloom.intensity == 0` (le défaut) ⇒ execute() sort
//    IMMÉDIATEMENT, sans rien enregistrer. Un jeu qui ne publie pas `render:bloom` paie exactement ce
//    qu'il payait avant que cette passe existe. Même contrat que CompositePass.
//
// Plan : docs/design/lighting-bloom.md
// ============================================================================

class BloomPass : public RenderPass {
public:
    // Les trois vues du post-traitement. 0 (monde), 1 (HUD), 2 (lumière), 3 (composite) et
    // 4 (occultation) sont prises ; l'ordre de soumission est imposé explicitement par le module, donc
    // ces identifiants n'ont pas besoin de suivre l'ordre logique.
    static constexpr rhi::ViewId kExtractView = 5;
    static constexpr rhi::ViewId kBlurHView   = 6;
    static constexpr rhi::ViewId kBlurVView   = 7;

    // Rayon (en pixels écran PLEINE résolution) que couvre le noyau à l'écartement de tap unitaire.
    // Le noyau fait 9 taps, donc ±4 taps, et un texel du quart de résolution vaut 4 pixels écran :
    // 4 taps × 4 pixels = 16. C'est ce qui rend `radius: 16` le rayon naturel, celui où l'écartement
    // vaut exactement un texel.
    static constexpr float kNaturalRadiusPx = 16.0f;

    explicit BloomPass(rhi::ShaderHandle extractShader = {}, rhi::ShaderHandle blurShader = {});

    const char* getName() const override { return "Bloom"; }
    uint32_t getSortOrder() const override { return 210; }   // après le composite, pour la lisibilité

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

    // Les cibles, possédées par le module (comme pour CompositePass).
    //   `hdr`      : la frame composée en pleine résolution — la SOURCE de l'extraction.
    //   `bloomA/B` : les deux cibles au quart. L'extraction écrit dans A, le flou H fait A→B, le
    //                flou V fait B→A. Le résultat est donc toujours dans A, ce que la passe de
    //                présentation sait (`PresentPass::setTargets` reçoit la même texture).
    void setTargets(rhi::TextureHandle hdr, rhi::TextureHandle bloomA, rhi::TextureHandle bloomB) {
        m_hdrTex = hdr;
        m_bloomATex = bloomA;
        m_bloomBTex = bloomB;
    }

    // Dimensions des cibles, nécessaires pour convertir un rayon en PIXELS en un décalage d'UV.
    // Fournies par le module, qui les possède : la passe ne peut pas les déduire d'une texture.
    void setSizes(uint16_t fullW, uint16_t fullH, uint16_t quarterW, uint16_t quarterH) {
        m_fullW = fullW; m_fullH = fullH;
        m_quarterW = quarterW; m_quarterH = quarterH;
    }

private:
    rhi::ShaderHandle  m_extractShader;
    rhi::ShaderHandle  m_blurShader;
    rhi::BufferHandle  m_quadVB;
    rhi::BufferHandle  m_quadIB;
    rhi::UniformHandle m_sceneSampler;
    rhi::UniformHandle m_extractUniform;    // (threshold, knee, texelX, texelY)
    rhi::UniformHandle m_blurUniform;       // (stepX, stepY, 0, 0)
    rhi::UniformHandle m_weightsUniform;    // 2 x vec4 : les 5 poids du demi-noyau
    rhi::TextureHandle m_hdrTex;
    rhi::TextureHandle m_bloomATex;
    rhi::TextureHandle m_bloomBTex;
    uint16_t m_fullW = 0, m_fullH = 0;
    uint16_t m_quarterW = 0, m_quarterH = 0;
};

} // namespace grove
