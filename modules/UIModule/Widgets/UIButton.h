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
    // Release the EXTRA text entry too (the base only drops the bg + children), so hiding/closing a window
    // with buttons doesn't leave their text as a ghost.
    void releaseRenderEntries(UIRenderer& renderer) override;

    // Streamed asset id for the button's sprite (alternative to the numeric texture). Non-empty -> the button
    // draws its background as a sprite resolved by the AssetManager (atlas-aware, on-demand stream) instead of
    // a hardcoded texture id. Set as a literal JSON "asset" or data-bound via {{...}} (applyBoundProp).
    std::string assetId;

    // Data-binding: a ship "part" is a clickable button bound to data — "color" (a "0xRRGGBBAA" block),
    // "texture" (a numeric sprite id; >0 -> draw the sprite) and "asset" (a streamed asset id string, wins
    // over texture). Applied to all states (flat part). Other props fall through to the base.
    void applyBoundProp(const std::string& prop, const std::string& s, double n, bool b) override {
        if (prop == "color" || prop == "bgColor") {
            uint32_t c = 0;
            if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                try { c = static_cast<uint32_t>(std::stoul(s, nullptr, 16)); } catch (...) { c = 0; }
            }
            normalStyle.bgColor = hoverStyle.bgColor = pressedStyle.bgColor = c;
        } else if (prop == "texture" || prop == "textureId") {
            const int tex = static_cast<int>(n);
            normalStyle.textureId = hoverStyle.textureId = pressedStyle.textureId = tex;
            normalStyle.useTexture = hoverStyle.useTexture = pressedStyle.useTexture = (tex > 0);
        } else if (prop == "asset") {
            // Streamed asset id (string) — e.g. an inventory icon or ship part by stable id. Bound from the
            // item scope in a repeater ("asset":"{{icon}}"); render() emits it as the sprite's `asset` field.
            assetId = s;
        } else if (prop == "text") {
            // Bindable label — e.g. a data-driven fleet vignette whose caption is {{name}}. The button
            // already renders `text`; this just lets the repeater/binding write it from the item scope.
            text = s;
        } else if (prop == "borderColor") {
            // Bindable RESTING border — used for selection highlight (a selected fleet icon binds its border
            // to a highlight colour; hover/pressed keep their own feedback borders).
            uint32_t c = 0;
            if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                try { c = static_cast<uint32_t>(std::stoul(s, nullptr, 16)); } catch (...) { c = 0; }
            }
            normalStyle.borderColor = c;
        } else {
            UIWidget::applyBoundProp(prop, s, n, b);
        }
    }

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
    bool isRightPressed = false;   // right-button press in progress (for on:rightClick)

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

    // Retained mode render IDs
    uint32_t m_textRenderId = 0;    // Separate ID for text element
    uint32_t m_borderId = 0;        // Border frame (drawn behind the bg; enables hover/selection borders)
};

} // namespace grove
