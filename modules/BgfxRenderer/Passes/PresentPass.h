#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"

namespace grove {

// ============================================================================
// PresentPass — amène la frame composée à l'écran en y ajoutant la lueur (plan B, tranche B2).
//
// QUOI  : un quad plein écran calculant `backbuffer = frame_composée + lueur × intensité`.
//
// POURQUOI une passe pour une addition : parce que le bloom doit LIRE la frame composée, et qu'un
//         backbuffer ne s'échantillonne pas. Le composite doit donc écrire dans une cible hors écran,
//         et il faut ensuite quelqu'un pour amener cette cible à l'écran. C'est aussi — et ce n'est
//         pas un hasard de calendrier — l'endroit exact où atterriront le tonemapping, les fondus et
//         la colorimétrie annoncés avec le bloom : cette passe existe pour la QUEUE de
//         post-traitement, pas seulement pour l'addition d'aujourd'hui.
//
// POURQUOI le HUD passe après elle : l'ordre de soumission imposé par le module met la vue 1 en
//         dernier. L'interface reste donc nette au-dessus d'un monde qui éblouit — c'est un choix,
//         pas un oubli.
//
// ⚠️ CONTOURNEMENT À COÛT NUL : `frame.bloom.intensity == 0` (le défaut) ⇒ execute() sort
//    IMMÉDIATEMENT. Sans bloom, le composite écrit directement au backbuffer et cette passe n'existe
//    pas du point de vue du GPU. Même contrat que CompositePass et BloomPass.
//
// Plan : docs/design/lighting-bloom.md
// ============================================================================

class PresentPass : public RenderPass {
public:
    static constexpr rhi::ViewId kPresentView = 8;

    explicit PresentPass(rhi::ShaderHandle shader = {});

    const char* getName() const override { return "Present"; }
    uint32_t getSortOrder() const override { return 220; }   // après le bloom, pour la lisibilité

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

    // `hdr` = ce que le composite a écrit ; `bloom` = la cible au quart où le flou vertical a fini
    // (la cible A de BloomPass — le ping-pong est conçu pour que le résultat y revienne).
    void setTargets(rhi::TextureHandle hdr, rhi::TextureHandle bloom) {
        m_hdrTex = hdr;
        m_bloomTex = bloom;
    }

private:
    rhi::ShaderHandle  m_shader;
    rhi::BufferHandle  m_quadVB;
    rhi::BufferHandle  m_quadIB;
    rhi::UniformHandle m_sceneSampler;
    rhi::UniformHandle m_bloomSampler;
    rhi::UniformHandle m_presentUniform;   // (intensity, 0, 0, 0)
    rhi::TextureHandle m_hdrTex;
    rhi::TextureHandle m_bloomTex;
};

} // namespace grove
