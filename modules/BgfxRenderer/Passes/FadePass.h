#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"

namespace grove {

// ============================================================================
// FadePass — un fondu plein écran par-dessus TOUT (plan F2).
//
// QUOI  : un quad plein écran de couleur unie, mélangé en alpha, sur une vue soumise EN DERNIER.
//
// POURQUOI une passe à part et pas la passe de présentation — c'est la décision de la tranche, et elle
//         corrige une affirmation du plan du bloom :
//
//         1. **La présentation passe AVANT le HUD** (l'ordre est monde → … → présentation → HUD,
//            précisément pour que l'interface ne brille pas). Un fondu posé là laisserait le HUD
//            parfaitement net sur un écran noir — l'inverse de ce qu'on veut d'une transition.
//         2. Un fondu n'a besoin de RIEN : ni cible HDR, ni éclairage. C'est un quad par-dessus le
//            backbuffer, quel que soit ce qui a été dessiné avant. Il fonctionne donc **avec ou sans
//            `render:ambient`** — donc les trois consommateurs actuels, qui n'éclairent pas, peuvent
//            s'en servir immédiatement. Le ranger avec le bloom l'aurait rendu inaccessible pour eux.
//         3. Et même en oubliant le HUD, il doit venir APRÈS la courbe de tonemapping : un fondu au
//            BLANC appliqué avant ne pourrait jamais atteindre le blanc, puisque Reinhard tend vers 1
//            sans l'atteindre. Une transition qui ne finit pas sa course est un bug visible.
//
// ⚠️ CONTOURNEMENT À COÛT NUL : `frame.fade.amount == 0` (le défaut) ⇒ execute() sort immédiatement.
//    C'est le plus simple des quatre contournements de cette famille — il n'y a même aucune cible à
//    construire ou à libérer.
//
// Plan : docs/design/lighting-fade.md
// ============================================================================

class FadePass : public RenderPass {
public:
    // Après la présentation (8) ET après le HUD (1). L'ordre de soumission est imposé explicitement par
    // le module quand l'éclairage est actif ; sans éclairage, l'ordre croissant des ids suffit — 9 est
    // au-delà de 1, donc le fondu passe après le HUD dans les deux cas.
    static constexpr rhi::ViewId kFadeView = 9;

    explicit FadePass(rhi::ShaderHandle shader = {});

    const char* getName() const override { return "Fade"; }
    uint32_t getSortOrder() const override { return 230; }   // après la présentation, pour la lisibilité

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

private:
    rhi::ShaderHandle  m_shader;
    rhi::BufferHandle  m_quadVB;
    rhi::BufferHandle  m_quadIB;
    rhi::UniformHandle m_fadeUniform;   // (r, g, b, amount)
};

} // namespace grove
