#include "UIButton.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace grove {

void UIButton::update(UIContext& ctx, float deltaTime) {
    // Update state based on enabled flag
    if (!enabled) {
        state = ButtonState::Disabled;
        isHovered = false;
        isPressed = false;
    } else {
        // State is managed by UIContext during hit testing
        // We just update our visual state enum here
        if (isPressed) {
            state = ButtonState::Pressed;
        } else if (isHovered) {
            state = ButtonState::Hover;
        } else {
            state = ButtonState::Normal;
        }
    }

    // Update children (buttons typically don't have children, but support it)
    updateChildren(ctx, deltaTime);
}

void UIButton::render(UIRenderer& renderer) {
    // Register with renderer on first render (need 2 entries: bg + text)
    if (!m_registered) {
        m_renderId = renderer.registerEntry();       // Background
        m_textRenderId = renderer.registerEntry();   // Text
        m_registered = true;
        // Set destroy callback to unregister both
        setDestroyCallback([&renderer, textId = m_textRenderId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(textId);
        });
    }

    const ButtonStyle& style = getCurrentStyle();

    // Retained mode: only publish if changed
    int bgLayer = renderer.nextLayer();

    // Render background (texture or solid color)
    if (style.useTexture && style.textureId > 0) {
        renderer.updateSprite(m_renderId, absX, absY, width, height, style.textureId, style.bgColor, bgLayer);
    } else {
        renderer.updateRect(m_renderId, absX, absY, width, height, style.bgColor, bgLayer);
    }

    // Render text centered
    if (!text.empty()) {
        int textLayer = renderer.nextLayer();
        float textX = absX + width * 0.5f;
        float textY = absY + height * 0.5f;

        renderer.updateText(m_textRenderId, textX, textY, text, fontSize, style.textColor, textLayer);
    }

    // Render children on top
    renderChildren(renderer);
}

void UIButton::generateDefaultStyles() {
    // If hover style wasn't explicitly set, lighten normal color
    if (!hoverStyleSet) {
        hoverStyle = normalStyle;
        hoverStyle.bgColor = adjustBrightness(normalStyle.bgColor, 1.2f);
    }

    // If pressed style wasn't explicitly set, darken normal color
    if (!pressedStyleSet) {
        pressedStyle = normalStyle;
        pressedStyle.bgColor = adjustBrightness(normalStyle.bgColor, 0.7f);
    }

    // Disabled style: desaturate and dim
    disabledStyle = normalStyle;
    disabledStyle.bgColor = adjustBrightness(normalStyle.bgColor, 0.5f);
    disabledStyle.textColor = 0x888888FF;
}

uint32_t UIButton::adjustBrightness(uint32_t color, float factor) {
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = color & 0xFF;

    // Adjust RGB, clamp to 0-255
    r = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, r * factor)));
    g = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, g * factor)));
    b = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, b * factor)));

    return (r << 24) | (g << 16) | (b << 8) | a;
}

bool UIButton::containsPoint(float px, float py) const {
    return px >= absX && px < absX + width &&
           py >= absY && py < absY + height;
}

bool UIButton::onMouseButton(int button, bool pressed, float x, float y) {
    if (!enabled) return false;

    if (button == 0) {  // Left mouse button
        if (pressed) {
            // Mouse down
            if (containsPoint(x, y)) {
                isPressed = true;
                return true;
            }
        } else {
            // Mouse up - only trigger click if still hovering
            if (isPressed && containsPoint(x, y)) {
                // Button clicked! Event will be published by UIModule
                isPressed = false;
                return true;
            }
            isPressed = false;
        }
    }

    return false;
}

void UIButton::onMouseEnter() {
    if (enabled) {
        isHovered = true;
    }
}

void UIButton::onMouseLeave() {
    isHovered = false;
    isPressed = false;  // Cancel press if mouse leaves
}

const ButtonStyle& UIButton::getCurrentStyle() const {
    switch (state) {
        case ButtonState::Hover:
            return hoverStyle;
        case ButtonState::Pressed:
            return pressedStyle;
        case ButtonState::Disabled:
            return disabledStyle;
        case ButtonState::Normal:
        default:
            return normalStyle;
    }
}

} // namespace grove
