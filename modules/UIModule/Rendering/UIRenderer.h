#pragma once

#include <grove/IIO.h>
#include <string>
#include <cstdint>

namespace grove {

/**
 * @brief Renders UI elements by publishing to IIO topics
 *
 * UIRenderer doesn't render directly - it publishes render commands
 * via IIO topics (render:sprite, render:text) that BgfxRenderer consumes.
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

private:
    IIO* m_io;
    int m_baseLayer = 1000;  // UI renders above game content
    int m_layerOffset = 0;   // Increments per draw call for proper ordering
};

} // namespace grove
