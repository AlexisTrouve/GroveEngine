#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"
#include "OcclusionPass.h"   // shares its view: matter is matter, whatever its shape

namespace grove {

// ============================================================================
// NebulaPass — soft radial media into the occlusion map (lighting A4).
//
// QUOI  : one quad per nebula, on the SAME view and with the SAME multiplicative blend as the rect
//         matter. The fragment shader varies the density across the quad instead of writing a flat
//         colour, so the volume fades to pure vacuum at its own rim and no square is ever visible.
//
// POURQUOI a pass of its own rather than a third loop inside OcclusionPass: that pass draws FLAT
//         quads with the shared "color" program, in one batched vertex buffer. A nebula needs its
//         own shader and one draw per volume (its parameters are uniforms, not vertex colours), so
//         folding it in would mean two programs and two batching strategies in one execute().
//
// ⚠️ Order does not matter between this pass and OcclusionPass, and that is not luck: the blend is
//    multiplicative and a product is commutative. Same property the socle relies on everywhere else.
//
// ⚠️ Costs nothing when no nebula is published: execute() returns on an empty list.
// ============================================================================

class NebulaPass : public RenderPass {
public:
    explicit NebulaPass(rhi::ShaderHandle shader = {});

    const char* getName() const override { return "Nebula"; }
    // Between the rect matter (170) and the lights (180): everything that writes into the occlusion
    // map runs before anything that reads it. Submission order is settled by the view order.
    uint32_t getSortOrder() const override { return 175; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

private:
    rhi::ShaderHandle  m_shader;
    rhi::BufferHandle  m_quadVB;
    rhi::BufferHandle  m_quadIB;
    rhi::UniformHandle m_placementUniform;  // `u_nebula` (cx, cy, radius) — read by vs_nebula
    rhi::UniformHandle m_fogUniform;        // (density, scatter, _, _)
    rhi::UniformHandle m_fogColorUniform;   // rgb = colour, a unused
};

} // namespace grove
