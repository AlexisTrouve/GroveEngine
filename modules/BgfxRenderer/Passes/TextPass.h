#pragma once

#include "../RenderGraph/RenderPass.h"
#include "../RHI/RHITypes.h"
#include "../Text/BitmapFont.h"

namespace grove {

// ============================================================================
// Text Pass - Renders 2D text with instanced quads
// ============================================================================

class TextPass : public RenderPass {
public:
    /**
     * @brief Construct TextPass with required shader
     * @param shader The shader program to use (sprite shader works)
     */
    explicit TextPass(rhi::ShaderHandle shader);

    const char* getName() const override { return "Text"; }
    uint32_t getSortOrder() const override { return 150; }  // After sprites, before debug
    std::vector<const char*> getDependencies() const override { return {"Sprites"}; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd) override;

    /**
     * @brief Get the bitmap font (for external texture loading)
     */
    BitmapFont& getFont() { return m_font; }
    const BitmapFont& getFont() const { return m_font; }

    // The optional BOLD face. Left invalid, a bold TextCommand falls back to the synthetic
    // double-draw (unchanged). Loaded with a real face, bold text uses THAT face — proper weight,
    // proper (wider) advances, instead of the same glyphs smeared 1px sideways.
    BitmapFont& getFontBold() { return m_fontBold; }
    const BitmapFont& getFontBold() const { return m_fontBold; }

private:
    // Render one text set (build glyph instances, batch, submit) to a specific bgfx view.
    // Called for world text (view 0) and HUD text (view 1) so HUD labels stay screen-fixed.
    void renderTextSet(rhi::IRHIDevice& device, rhi::RHICommandBuffer& cmd,
                       const TextCommand* texts, size_t count, rhi::ViewId viewId);

    rhi::ShaderHandle m_shader;
    rhi::BufferHandle m_quadVB;
    rhi::BufferHandle m_quadIB;
    rhi::BufferHandle m_instanceBuffer;
    rhi::UniformHandle m_textureSampler;

    BitmapFont m_font;
    BitmapFont m_fontBold;   // optional real bold face; invalid = synthetic bold

    // Reusable buffer for glyph instances
    std::vector<SpriteInstance> m_glyphInstances;

    static constexpr uint32_t MAX_GLYPHS_PER_BATCH = 4096;
};

} // namespace grove
