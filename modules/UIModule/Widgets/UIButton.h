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
    int textureId = 0;  // 0 = no texture (solid color), >0 = texture ID
    bool useTexture = false;
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

    // Track if styles were explicitly set (for auto-generation)
    bool hoverStyleSet = false;
    bool pressedStyleSet = false;

    // Current state
    ButtonState state = ButtonState::Normal;
    bool isHovered = false;
    bool isPressed = false;

    /**
     * @brief Auto-generate hover/pressed styles from normal style
     * Call this after setting normalStyle if hover/pressed weren't explicitly set
     */
    void generateDefaultStyles();

private:
    /**
     * @brief Get the appropriate style for current state
     */
    const ButtonStyle& getCurrentStyle() const;

    /**
     * @brief Adjust color brightness
     * @param color RGBA color
     * @param factor >1 to lighten, <1 to darken
     * @return Adjusted color
     */
    static uint32_t adjustBrightness(uint32_t color, float factor);
};

} // namespace grove
