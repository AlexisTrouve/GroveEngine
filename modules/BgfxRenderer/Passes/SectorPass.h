#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"
#include <cstdint>

namespace grove {

// ============================================================================
// SectorPass — draws filled ring-sectors / pie wedges (render:sector) as coloured triangles.
//
// WHAT : tessellates each FramePacket SectorCommand into a triangle list (grove::geom::appendSector)
//        and draws it with the plain position+colour shader (same as DebugPass — no texture, no new
//        shader). World sectors -> view 0 (zoom with the camera); HUD sectors (space:"screen") ->
//        view 1 (fixed overlay). Sort order 120: above sprites (100), below text (150), so the
//        action-wheel's wedges sit over its background and under its labels.
//
// WHY  : the renderer had no arc/sector primitive; the UIRadial action wheel (and rings/gauges/radars)
//        wants real pie slices. Ephemeral (rebuilt each frame from the packet, like DebugPass), so a
//        hidden wheel's wedges vanish the moment it stops publishing — no retained bookkeeping.
// ============================================================================
class SectorPass : public RenderPass {
public:
    explicit SectorPass(rhi::ShaderHandle shader);

    const char* getName() const override { return "Sectors"; }
    uint32_t getSortOrder() const override { return 120; }   // sprites(100) < sectors < text(150)
    std::vector<const char*> getDependencies() const override { return {"Clear"}; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

private:
    rhi::ShaderHandle m_shader;     // position + colour (reuses the "debug" program)
    rhi::BufferHandle m_worldVB;    // view 0 (world) sector triangles
    rhi::BufferHandle m_hudVB;      // view 1 (HUD) sector triangles — separate so two draws don't clobber

    // Cap on triangle vertices per buffer. A wheel/ring is a few hundred verts; 60k is generous and
    // bounds the dynamic upload. Excess sectors are dropped with a warning (no silent truncation).
    static constexpr uint32_t MAX_SECTOR_VERTICES = 60000;
};

} // namespace grove
