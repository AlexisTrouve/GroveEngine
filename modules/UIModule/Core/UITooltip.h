#pragma once

#include <string>
#include <cstdint>

namespace grove {

class UIContext;
class UIRenderer;
class UIWidget;

/**
 * @brief Tooltip system for displaying hover text
 *
 * Manages tooltip display with hover delay, positioning,
 * and rendering. Tooltips are shown when hovering over
 * widgets with tooltip text for a specified duration.
 */
class UITooltipManager {
public:
    UITooltipManager() = default;
    ~UITooltipManager() = default;

    /**
     * @brief Update tooltip state based on hovered widget
     * @param hoveredWidget Current hovered widget (nullptr if none)
     * @param ctx UI context
     * @param deltaTime Time since last update
     */
    void update(UIWidget* hoveredWidget, const UIContext& ctx, float deltaTime);

    /**
     * @brief Render tooltip if visible
     * @param renderer UI renderer
     * @param screenWidth Screen width for positioning
     * @param screenHeight Screen height for positioning
     */
    void render(UIRenderer& renderer, float screenWidth, float screenHeight);

    /**
     * @brief Reset tooltip state (call when mouse leaves widget)
     */
    void reset();

    /**
     * @brief Check if tooltip is currently visible
     */
    bool isVisible() const { return m_visible; }

    // Configuration
    float hoverDelay = 0.5f;  // Seconds before showing tooltip
    float padding = 8.0f;
    float offsetX = 10.0f;    // Offset from cursor
    float offsetY = 10.0f;

    // Styling
    uint32_t bgColor = 0x2a2a2aEE;      // Semi-transparent background
    uint32_t textColor = 0xFFFFFFFF;
    uint32_t borderColor = 0x666666FF;
    float borderWidth = 1.0f;
    float fontSize = 14.0f;
    float maxWidth = 300.0f;

private:
    bool m_visible = false;
    float m_hoverTime = 0.0f;
    std::string m_currentText;
    UIWidget* m_currentWidget = nullptr;   // tracked by POINTER, not id (repeater items share an empty id)
    float m_tooltipX = 0.0f;
    float m_tooltipY = 0.0f;
    float m_tooltipWidth = 0.0f;
    float m_tooltipHeight = 0.0f;

    void computeTooltipSize(const std::string& text);
    void computeTooltipPosition(float cursorX, float cursorY, float screenWidth, float screenHeight);
};

} // namespace grove
