#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>

namespace grove {

/**
 * @brief Scrollable container widget with clipping
 *
 * ScrollPanel extends Panel with scrolling capabilities:
 * - Vertical and/or horizontal scrolling
 * - Mouse wheel support
 * - Optional scrollbars
 * - Content clipping (only render visible area)
 * - Drag-to-scroll support
 */
class UIScrollPanel : public UIWidget {
public:
    UIScrollPanel() = default;
    ~UIScrollPanel() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "scrollpanel"; }
    // Clip the hit-test to the panel bounds (slice 2b): a click outside the panel never reaches a
    // scrolled-out child, mirroring the visual scissor pushed in render().
    bool clipsHitTest() const override { return true; }

    // Scroll configuration
    bool scrollVertical = true;
    bool scrollHorizontal = false;
    bool showScrollbar = true;
    bool dragToScroll = true;

    // Scroll state
    float scrollOffsetX = 0.0f;
    float scrollOffsetY = 0.0f;
    float contentWidth = 0.0f;   // Total content size
    float contentHeight = 0.0f;

    // Scrollbar appearance
    float scrollbarWidth = 8.0f;
    uint32_t scrollbarColor = 0x666666FF;
    uint32_t scrollbarHoverColor = 0x888888FF;
    uint32_t scrollbarBgColor = 0x222222FF;

    // Style
    uint32_t bgColor = 0x2a2a2aFF;
    float borderRadius = 0.0f;
    float borderWidth = 1.0f;
    uint32_t borderColor = 0x444444FF;

    // Texture support
    int bgTextureId = 0;                  // Background texture ID (0 = solid color)
    bool useBgTexture = false;            // Use texture for background
    uint32_t bgTintColor = 0xFFFFFFFF;    // Tint for background texture

    int scrollbarTrackTextureId = 0;      // Scrollbar track texture ID (0 = solid color)
    bool useScrollbarTrackTexture = false; // Use texture for scrollbar track
    uint32_t scrollbarTrackTintColor = 0xFFFFFFFF; // Tint for scrollbar track texture

    int scrollbarThumbTextureId = 0;      // Scrollbar thumb texture ID (0 = solid color)
    bool useScrollbarThumbTexture = false; // Use texture for scrollbar thumb
    uint32_t scrollbarThumbTintColor = 0xFFFFFFFF; // Tint for scrollbar thumb texture

    // Interaction state
    bool isDraggingContent = false;
    bool isDraggingScrollbar = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    float scrollStartX = 0.0f;
    float scrollStartY = 0.0f;

    /**
     * @brief Handle mouse wheel scrolling
     */
    void handleMouseWheel(float wheelDelta);

    /**
     * @brief Compute content bounds from children
     */
    void computeContentSize();

    /**
     * @brief Clamp scroll offset to valid range
     */
    void clampScrollOffset();

    /**
     * @brief Get visible content rect (for clipping)
     */
    void getVisibleRect(float& outX, float& outY, float& outW, float& outH) const;

    /**
     * @brief Check if scrollbar is hovered
     */
    bool isScrollbarHovered(const UIContext& ctx) const;

    /**
     * @brief Get scrollbar rect (vertical)
     */
    void getScrollbarRect(float& outX, float& outY, float& outW, float& outH) const;

private:
    void renderScrollbar(UIRenderer& renderer);
    void updateScrollInteraction(UIContext& ctx);

    // Retained mode render IDs
    uint32_t m_borderTopId = 0;
    uint32_t m_borderBottomId = 0;
    uint32_t m_borderLeftId = 0;
    uint32_t m_borderRightId = 0;
    uint32_t m_scrollTrackId = 0;
    uint32_t m_scrollThumbId = 0;
};

} // namespace grove
