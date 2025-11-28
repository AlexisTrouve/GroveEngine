#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Button state enumeration
 */
enum class ButtonState {
    Normal,    // Default state
    Hover,     // Mouse over
    Pressed,   // Mouse button down
    Disabled   // Not interactive
};

/**
 * @brief Style properties for a button state
 */
struct ButtonStyle {
    uint32_t bgColor = 0x444444FF;
    uint32_t textColor = 0xFFFFFFFF;
    uint32_t borderColor = 0x000000FF;
    float borderWidth = 0.0f;
    float borderRadius = 0.0f;
};

/**
 * @brief Interactive button widget
 *
 * Supports different visual states (normal, hover, pressed, disabled)
 * and triggers actions via IIO when clicked.
 */
class UIButton : public UIWidget {
public:
    UIButton() = default;
    ~UIButton() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "button"; }

    /**
     * @brief Check if a point is inside this button
     */
    bool containsPoint(float px, float py) const;

    /**
     * @brief Handle mouse button event
     * @return true if event was consumed
     */
    bool onMouseButton(int button, bool pressed, float x, float y);

    /**
     * @brief Handle mouse enter/leave
     */
    void onMouseEnter();
    void onMouseLeave();

    // Button properties
    std::string text;
    float fontSize = 16.0f;
    std::string onClick;  // Action to publish (e.g., "game:start")
    bool enabled = true;

    // State-specific styles
    ButtonStyle normalStyle;
    ButtonStyle hoverStyle;
    ButtonStyle pressedStyle;
    ButtonStyle disabledStyle;

    // Current state
    ButtonState state = ButtonState::Normal;
    bool isHovered = false;
    bool isPressed = false;

private:
    /**
     * @brief Get the appropriate style for current state
     */
    const ButtonStyle& getCurrentStyle() const;
};

} // namespace grove
