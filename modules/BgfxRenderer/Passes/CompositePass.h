#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"

namespace grove {

// ============================================================================
// CompositePass — multiplies the rendered scene by the light term (lighting L1).
//
// QUOI  : one full-screen quad computing `final = scene.rgb * (ambient + lightAccum.rgb)`, drawn on
//         its own view into the backbuffer.
//
// POURQUOI a separate pass rather than lighting each sprite: it leaves EVERY existing pass
//         untouched — no sprite, tilemap, particle or text shader changes — and it does not care how
//         many lights there are, since they were already accumulated into a texture. It is also the
//         exact hook the post-processing pass will need next.
//
// COMMENT: the pass records onto a DEDICATED view id, not "after the other passes" in graph order.
//         The HUD is emitted by SpritePass/TextPass themselves (they draw view 0 then view 1), so
//         there is no point in the graph where the composite could sit between world and HUD.
//         Ordering is settled by view submission order (rhi::setViewOrder), which is precisely why
//         that call had to be added — see docs/design/lighting-2d.md §2.
//
// ⚠️ ZERO COST WHEN UNUSED: with no ambient set (FramePacket::ambientColor == 0) execute() returns
//    IMMEDIATELY, recording nothing. Every consumer that never lights anything must keep paying
//    exactly what it paid before this pass existed.
// ============================================================================

class CompositePass : public RenderPass {
public:
    // View the composite draws into. World = 0 and HUD = 1 are taken; the light accumulation buffer
    // uses kLightView. Submission order is imposed explicitly, so these ids need not be contiguous
    // with their logical order.
    static constexpr rhi::ViewId kLightView     = 2;
    static constexpr rhi::ViewId kCompositeView = 3;

    explicit CompositePass(rhi::ShaderHandle shader = {});

    const char* getName() const override { return "Composite"; }
    uint32_t getSortOrder() const override { return 200; }   // after every world pass, for readability

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

    // The two offscreen colour targets, owned by the renderer module (it also owns their resize).
    // Absent (invalid handles) the pass still records its draw — the mock has no textures at all and
    // the headless test must still be able to observe the draw.
    // `occlusion` is read for its ALPHA only — the scattering coefficient (A2). The module hands it
    // the same texture the light march samples, which is the 1x1 white placeholder when no matter
    // was published; white means alpha 1, hence zero scattering, hence a term that vanishes.
    void setTargets(rhi::TextureHandle scene, rhi::TextureHandle light, rhi::TextureHandle occlusion) {
        m_sceneTex = scene;
        m_lightTex = light;
        m_occlusionTex = occlusion;
    }

private:
    rhi::ShaderHandle  m_shader;
    rhi::BufferHandle  m_quadVB;
    rhi::BufferHandle  m_quadIB;
    rhi::UniformHandle m_sceneSampler;
    rhi::UniformHandle m_lightSampler;
    rhi::UniformHandle m_occlusionSampler;
    rhi::UniformHandle m_ambientUniform;
    rhi::TextureHandle m_sceneTex;
    rhi::TextureHandle m_lightTex;
    rhi::TextureHandle m_occlusionTex;
};

} // namespace grove
