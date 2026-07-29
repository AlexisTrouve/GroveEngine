#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"

namespace grove {

// ============================================================================
// LightPass — accumulates radial lights into the light buffer (lighting L2).
//
// QUOI  : one ADDITIVE quad per light, drawn on the light view (CompositePass::kLightView) into the
//         RGBA16F accumulation target the composite then multiplies the scene by.
//
// POURQUOI additive: light is a sum. Two lamps overlapping are brighter than either alone, and the
//         result is allowed to exceed 1.0 — the target is half-float precisely so that overbright
//         survives for the bloom pass to extract.
//
// COMMENT: ONE DRAW PER LIGHT, deliberately not instanced. A lit scene has TENS of lights, so the
//         per-draw cost is noise, while an instance format would be one more layout to keep in sync
//         with a shader for no measurable gain. Same reasoning that ruled out a bulk IIO path for
//         render:light. If a project ever ships thousands of lights, THAT is when to measure and
//         revisit — not before.
//
// The quad mesh is a unit square spanning -1..1; the vertex shader scales it by the light's radius
// and moves it to its centre in WORLD space. The fragment shader gets that local -1..1 position, so
// the rim sits at length == 1 whatever the radius or zoom, and the falloff needs no world
// coordinate. The curve mirrors grove::light::attenuation, which LightMathUnit pins headlessly.
//
// ⚠️ Costs nothing when no light is published: execute() returns on an empty list.
// ============================================================================

class LightPass : public RenderPass {
public:
    explicit LightPass(rhi::ShaderHandle shader = {});

    const char* getName() const override { return "Light"; }
    // Between the world passes and the composite (200). Only readability: the light view is a
    // separate view, so submission order is settled by rhi::setViewOrder, not by the graph.
    uint32_t getSortOrder() const override { return 180; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

private:
    rhi::ShaderHandle  m_shader;
    rhi::BufferHandle  m_quadVB;
    rhi::BufferHandle  m_quadIB;
    rhi::UniformHandle m_lightUniform;       // (cx, cy, radius, intensity)
    rhi::UniformHandle m_lightColorUniform;  // (r, g, b, unused)
};

} // namespace grove
