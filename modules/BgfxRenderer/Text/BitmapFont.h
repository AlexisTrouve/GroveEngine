#pragma once

#include "../RHI/RHITypes.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace grove {

namespace rhi { class IRHIDevice; }

// ============================================================================
// Glyph Info - Metrics for a single character
// ============================================================================

struct GlyphInfo {
    float u0, v0, u1, v1;  // UV coordinates in atlas
    float width, height;    // Glyph size in pixels
    float offsetX, offsetY; // Offset from cursor position
    float advance;          // Cursor advance after this glyph
};

// ============================================================================
// BitmapFont - Simple bitmap font for text rendering
// ============================================================================

class BitmapFont {
public:
    BitmapFont() = default;
    ~BitmapFont() = default;

    // Non-copyable
    BitmapFont(const BitmapFont&) = delete;
    BitmapFont& operator=(const BitmapFont&) = delete;

    /**
     * @brief Initialize with embedded 8x8 monospace font
     * @param device RHI device for texture creation
     * @return true on success
     */
    bool initDefault(rhi::IRHIDevice& device);

    /**
     * @brief Load font from BMFont format (.fnt + .png)
     * @param device RHI device for texture creation
     * @param fntPath Path to .fnt file
     * @param pngPath Path to .png atlas
     * @return true on success
     */
    bool loadBMFont(rhi::IRHIDevice& device, const std::string& fntPath, const std::string& pngPath);

    /**
     * @brief Cleanup GPU resources
     */
    void shutdown(rhi::IRHIDevice& device);

    /**
     * @brief Get glyph info for a character
     * @param codepoint Unicode codepoint (ASCII for now)
     * @return Glyph info, or default space if not found
     */
    const GlyphInfo& getGlyph(uint32_t codepoint) const;

    /**
     * @brief Get font atlas texture
     */
    rhi::TextureHandle getTexture() const { return m_texture; }

    /**
     * @brief Get line height (for multi-line text)
     */
    float getLineHeight() const { return m_lineHeight; }

    /**
     * @brief Get base font size (pixels)
     */
    float getBaseSize() const { return m_baseSize; }

    /**
     * @brief Calculate text width in pixels
     */
    float measureWidth(const char* text) const;

    /**
     * @brief Check if font is loaded
     */
    bool isValid() const { return m_texture.isValid(); }

private:
    void generateDefaultGlyphs();

    rhi::TextureHandle m_texture;
    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;
    GlyphInfo m_defaultGlyph;
    float m_lineHeight = 8.0f;
    float m_baseSize = 8.0f;
    uint16_t m_atlasWidth = 128;
    uint16_t m_atlasHeight = 64;
};

} // namespace grove
