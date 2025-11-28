#pragma once

#include <string>
#include <cstdint>

namespace grove {

/**
 * @brief Global UI state for input handling and focus management
 *
 * Tracks mouse position, button states, keyboard focus, and
 * provides hit-testing utilities for widgets.
 */
class UIContext {
public:
    // Mouse state
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool mouseDown = false;
    bool mousePressed = false;   // Just pressed this frame
    bool mouseReleased = false;  // Just released this frame

    // Keyboard state
    bool keyPressed = false;
    int keyCode = 0;
    char keyChar = 0;

    // Mouse wheel state
    float mouseWheelDelta = 0.0f;

    // Focus/hover tracking
    std::string hoveredWidgetId;
    std::string focusedWidgetId;
    std::string activeWidgetId;  // Currently being interacted with (e.g., dragging)

    // Screen size for coordinate normalization
    float screenWidth = 1280.0f;
    float screenHeight = 720.0f;

    /**
     * @brief Reset per-frame state
     * Call at the start of each frame before processing input
     */
    void beginFrame() {
        mousePressed = false;
        mouseReleased = false;
        keyPressed = false;
        keyCode = 0;
        keyChar = 0;
        mouseWheelDelta = 0.0f;
        hoveredWidgetId.clear();
    }

    /**
     * @brief Check if a point is inside a rectangle
     */
    static bool pointInRect(float px, float py, float rx, float ry, float rw, float rh) {
        return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
    }

    /**
     * @brief Check if mouse is inside a rectangle
     */
    bool isMouseInRect(float rx, float ry, float rw, float rh) const {
        return pointInRect(mouseX, mouseY, rx, ry, rw, rh);
    }

    /**
     * @brief Set hover state for a widget
     */
    void setHovered(const std::string& widgetId) {
        hoveredWidgetId = widgetId;
    }

    /**
     * @brief Check if widget is hovered
     */
    bool isHovered(const std::string& widgetId) const {
        return hoveredWidgetId == widgetId;
    }

    /**
     * @brief Check if widget is focused
     */
    bool isFocused(const std::string& widgetId) const {
        return focusedWidgetId == widgetId;
    }

    /**
     * @brief Check if widget is active (being interacted with)
     */
    bool isActive(const std::string& widgetId) const {
        return activeWidgetId == widgetId;
    }

    /**
     * @brief Set focus to a widget
     */
    void setFocus(const std::string& widgetId) {
        focusedWidgetId = widgetId;
    }

    /**
     * @brief Set active widget
     */
    void setActive(const std::string& widgetId) {
        activeWidgetId = widgetId;
    }

    /**
     * @brief Clear active widget
     */
    void clearActive() {
        activeWidgetId.clear();
    }
};

} // namespace grove
