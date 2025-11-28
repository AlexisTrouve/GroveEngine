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
};

} // namespace grove
