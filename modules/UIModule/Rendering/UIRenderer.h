#pragma once

#include <grove/IIO.h>
#include <string>
#include <cstdint>
#include <unordered_map>

namespace grove {

// Render entry types
enum class RenderEntryType {
    Rect,
    Sprite,
    Text
};

// Cached state for a render entry (to detect changes)
struct RenderEntry {
    RenderEntryType type;
    float x, y, w, h;
    uint32_t color;
    int textureId;
    int layer;
    std::string text;
    float fontSize;
};

/**
 * @brief Renders UI elements by publishing to IIO topics
 *
 * UIRenderer supports two modes:
 * - Immediate mode (legacy): drawRect/drawText/drawSprite publish every call
 * - Retained mode (new): updateRect/updateText/updateSprite only publish on change
 *
 * Retained mode dramatically reduces IIO message traffic for static UIs.
 */
class UIRenderer {
public:
    explicit UIRenderer(IIO* io);
    ~UIRenderer() = default;

    /**
     * @brief Draw a filled rectangle
     * @param x X position
     * @param y Y position
     * @param w Width
     * @param h Height
     * @param color RGBA color (0xRRGGBBAA)
     */
    void drawRect(float x, float y, float w, float h, uint32_t color);

    /**
     * @brief Draw text
     * @param x X position
     * @param y Y position
     * @param text Text string
     * @param fontSize Font size
     * @param color RGBA color
     */
    void drawText(float x, float y, const std::string& text, float fontSize, uint32_t color);

    /**
     * @brief Draw a textured sprite
     * @param x X position
     * @param y Y position
     * @param w Width
     * @param h Height
     * @param textureId Texture ID
     * @param color Tint color
     */
    void drawSprite(float x, float y, float w, float h, int textureId, uint32_t color = 0xFFFFFFFF);

    /**
     * @brief Set the base layer for UI rendering
     * UI elements should render above game sprites (layer 1000+)
     */
    void setBaseLayer(int layer) { m_baseLayer = layer; }

    /**
     * @brief Get current layer and increment
     */
    int nextLayer() { return m_baseLayer + m_layerOffset++; }

    /**
     * @brief Reset layer offset for new frame
     */
    void beginFrame() { m_layerOffset = 0; }

    // ========================================================================
    // Retained Mode API
    // ========================================================================

    /**
     * @brief Register a new render entry and get its ID
     * @return Unique render ID for this entry
     */
    uint32_t registerEntry();

    /**
     * @brief Unregister a render entry (widget destroyed)
     * @param renderId ID to remove
     */
    void unregisterEntry(uint32_t renderId);

    /**
     * @brief Update a rectangle (only publishes if changed)
     * @return true if published (changed), false if skipped (unchanged)
     */
    bool updateRect(uint32_t renderId, float x, float y, float w, float h, uint32_t color, int layer);

    /**
     * @brief Update text (only publishes if changed)
     * @return true if published (changed), false if skipped (unchanged)
     */
    bool updateText(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer);

    /**
     * @brief Update a textured sprite (only publishes if changed)
     * @return true if published (changed), false if skipped (unchanged)
     */
    bool updateSprite(uint32_t renderId, float x, float y, float w, float h, int textureId, uint32_t color, int layer);

private:
    IIO* m_io;
    int m_baseLayer = 1000;  // UI renders above game content
    int m_layerOffset = 0;   // Increments per draw call for proper ordering

    // Retained mode state
    uint32_t m_nextRenderId = 1;
    std::unordered_map<uint32_t, RenderEntry> m_entries;

    // Publish helpers
    void publishSpriteAdd(uint32_t renderId, float x, float y, float w, float h, int textureId, uint32_t color, int layer);
    void publishSpriteUpdate(uint32_t renderId, float x, float y, float w, float h, int textureId, uint32_t color, int layer);
    void publishSpriteRemove(uint32_t renderId);
    void publishTextAdd(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer);
    void publishTextUpdate(uint32_t renderId, float x, float y, const std::string& text, float fontSize, uint32_t color, int layer);
    void publishTextRemove(uint32_t renderId);
};

} // namespace grove
