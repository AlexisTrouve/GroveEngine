#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"

namespace grove {

// ============================================================================
// OcclusionPass — draws opaque occluders into the occlusion map (lighting W2).
//
// QUOI  : one black quad per occluder, on a dedicated view whose target is cleared to WHITE.
//         White = vacuum (transmittance 1), black = opaque (transmittance 0).
//
// POURQUOI a whole pass for flat quads: the light march samples this map per step, so the matter has
//         to exist as PIXELS, not as a list the shader would have to iterate. That is what makes the
//         cost independent of how many walls there are — the classic 2D-shadow trade.
//
// COMMENT: no new shader. The occluders are flat coloured quads, which is exactly what the existing
//         "color" program (vs_color/fs_color, PosColor) already draws for DebugPass and SectorPass.
//         A fifth shader pair to compile and maintain, for black rectangles, would have been waste.
//
// ⚠️ Costs nothing when no occluder is published: execute() returns on an empty list, and the view's
//    clear alone leaves the map白 — vacuum, which the march multiplies by 1.
// ============================================================================

class OcclusionPass : public RenderPass {
public:
    // View the occluders draw into. Must be SUBMITTED BEFORE the light view, since the light march
    // samples what this pass wrote — the module imposes that with rhi::setViewOrder.
    static constexpr rhi::ViewId kOcclusionView = 4;

    explicit OcclusionPass(rhi::ShaderHandle shader = {});

    const char* getName() const override { return "Occlusion"; }
    // Before the lights (180) so the graph reads in the order things happen. Submission order is
    // settled by the view order, not by this.
    uint32_t getSortOrder() const override { return 170; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

private:
    // 6 vertices per rect (two triangles). Bounded like DebugPass: a vertex buffer is a fixed
    // allocation, and writing past it corrupts GPU memory rather than dropping a wall.
    static constexpr uint32_t MAX_OCCLUDERS = 4096;

    rhi::ShaderHandle m_shader;
    rhi::BufferHandle m_vb;
};

} // namespace grove
