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
    // Register with renderer on first render (bg + text + border rect + optional 9-slice frame)
    if (!m_registered) {
        m_renderId = renderer.registerEntry();       // Background
        m_textRenderId = renderer.registerEntry();   // Text
        m_borderId = renderer.registerEntry();       // Border frame (flat-rect look)
        m_frameId = renderer.registerEntry();        // 9-slice chrome (used only when frameAsset is set)
        m_registered = true;
        // Set destroy callback to unregister all four
        setDestroyCallback([&renderer, textId = m_textRenderId, borderId = m_borderId,
                            frameId = m_frameId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(textId);
            renderer.unregisterEntry(borderId);
            renderer.unregisterEntry(frameId);
        });
    }

    const ButtonStyle& style = getCurrentStyle();

    // Text placement, shared by the 9-slice and the flat path. Vertically centred (top = middle − ½font).
    // Horizontally: left = padding in; centre = the button's middle; right = padding from the right edge.
    // TextPass interprets x per `textAlign`, so a centred label is truly centred (not "starts at centre").
    const float tTextX = (textAlign == 0) ? (absX + padding)
                        : (textAlign == 2) ? (absX + width - padding)
                        : (absX + width * 0.5f);
    const float tTextY = absY + (height - fontSize) * 0.5f;

    // 9-slice CHROME path: a composed border texture replaces the flat border-rect + bg fill entirely. The
    // whole box is one render:nineslice, TINTED by the state's bgColor (hover/pressed re-tint for free). We
    // still draw the text on top. The flat-look entries (border rect + bg) are collapsed to zero so they
    // don't double-draw. `frameSrcW/H > 0` is required (no source dims -> can't map UVs) — else fall through.
    if (!frameAsset.empty() && frameSrcW > 0.0f && frameSrcH > 0.0f) {
        int frameLayer = renderer.nextLayer();
        renderer.updateNineSlice(m_frameId, absX, absY, width, height, frameAsset, /*textureId=*/0,
                                 frameSrcW, frameSrcH, frameL, frameR, frameT, frameB,
                                 style.bgColor, frameLayer);
        // Collapse the flat-look entries so they never co-draw with the frame.
        renderer.updateRect(m_borderId, 0, 0, 0, 0, 0, renderer.nextLayer());
        renderer.updateRect(m_renderId, 0, 0, 0, 0, 0, renderer.nextLayer());

        if (!text.empty()) {
            int textLayer = renderer.nextLayer();
            renderer.updateText(m_textRenderId, tTextX, tTextY, text, fontSize, style.textColor, textLayer, textAlign, bold);
        } else {
            renderer.updateText(m_textRenderId, 0, 0, "", fontSize, 0, renderer.nextLayer());
        }
        renderChildren(renderer);
        return;
    }
    // No 9-slice: keep the frame entry idle (collapsed) so a widget that toggles frameAsset off recovers.
    renderer.updateNineSlice(m_frameId, 0, 0, 0, 0, "", 0, 0, 0, 0, 0, 0, 0, 0, renderer.nextLayer());

    static int logCount = 0;
    if (logCount < 10) {  // Log first 10 buttons to see all textured ones
        spdlog::info("UIButton[{}]::render() id='{}', state={}, normalStyle.textureId={}, useTexture={}",
            logCount, id, (int)state, normalStyle.textureId, normalStyle.useTexture);
        spdlog::info("  current style: textureId={}, useTexture={}", style.textureId, style.useTexture);
        logCount++;
    }

    // Border frame first (drawn BEHIND), then the bg/texture INSET by borderWidth so the border reads as a
    // frame around the button. This is what makes hover/selection borders visible (borderWidth 0 -> no border,
    // bg fills the whole button, unchanged behaviour).
    const float bw = (style.borderWidth > 0.0f) ? style.borderWidth : 0.0f;
    int borderLayer = renderer.nextLayer();
    if (bw > 0.0f) {
        renderer.updateRect(m_borderId, absX, absY, width, height, style.borderColor, borderLayer);
    } else {
        renderer.updateRect(m_borderId, 0, 0, 0, 0, 0, borderLayer);
    }

    int bgLayer = renderer.nextLayer();
    const float ix = absX + bw, iy = absY + bw, iw = width - 2.0f * bw, ih = height - 2.0f * bw;
    if (!assetId.empty()) {
        // Streamed asset: draw the bg as a sprite resolved by the AssetManager (atlas-aware). Wins over a
        // numeric texture id — the renderer turns the `asset` string into a texture + UV.
        renderer.updateSprite(m_renderId, ix, iy, iw, ih, assetId, style.bgColor, bgLayer);
    } else if (style.useTexture && style.textureId > 0) {
        renderer.updateSprite(m_renderId, ix, iy, iw, ih, style.textureId, style.bgColor, bgLayer);
    } else {
        renderer.updateRect(m_renderId, ix, iy, iw, ih, style.bgColor, bgLayer);
    }

    // Render text (aligned + optionally bold, vertically centred)
    if (!text.empty()) {
        int textLayer = renderer.nextLayer();
        renderer.updateText(m_textRenderId, tTextX, tTextY, text, fontSize, style.textColor, textLayer, textAlign, bold);
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

void UIButton::releaseRenderEntries(UIRenderer& renderer) {
    if (m_textRenderId != 0) { renderer.unregisterEntry(m_textRenderId); m_textRenderId = 0; }
    if (m_borderId != 0)     { renderer.unregisterEntry(m_borderId);     m_borderId = 0; }
    if (m_frameId != 0)      { renderer.unregisterEntry(m_frameId);      m_frameId = 0; }
    UIWidget::releaseRenderEntries(renderer);   // drops m_renderId (bg) + recurses to children
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
    } else if (button == 1) {  // Right mouse button — symmetric, so a right-click can surface (on:rightClick)
        if (pressed) {
            if (containsPoint(x, y)) { isRightPressed = true; return true; }
        } else {
            if (isRightPressed && containsPoint(x, y)) { isRightPressed = false; return true; }
            isRightPressed = false;
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
