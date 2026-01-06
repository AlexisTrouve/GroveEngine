#include "UICheckbox.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"

namespace grove {

void UICheckbox::update(UIContext& ctx, float deltaTime) {
    // Check if mouse is over checkbox
    isHovered = containsPoint(ctx.mouseX, ctx.mouseY);

    // Update children
    updateChildren(ctx, deltaTime);
}

void UICheckbox::render(UIRenderer& renderer) {
    // Register with renderer on first render (need 3 entries: box + checkmark + text)
    if (!m_registered) {
        m_renderId = renderer.registerEntry();       // Box background
        m_checkRenderId = renderer.registerEntry();  // Checkmark
        m_textRenderId = renderer.registerEntry();   // Label text
        m_registered = true;
        // Set destroy callback to unregister all entries
        setDestroyCallback([&renderer, checkId = m_checkRenderId, textId = m_textRenderId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(checkId);
            renderer.unregisterEntry(textId);
        });
    }

    // Render checkbox box
    float boxX = absX;
    float boxY = absY + (height - boxSize) * 0.5f;  // Vertically center the box

    // Box background (retained mode)
    int boxLayer = renderer.nextLayer();
    uint32_t currentBoxColor = isHovered ? 0x475569FF : boxColor;
    renderer.updateRect(m_renderId, boxX, boxY, boxSize, boxSize, currentBoxColor, boxLayer);

    // Check mark if checked (retained mode)
    int checkLayer = renderer.nextLayer();
    if (checked) {
        // Draw a smaller filled rect as checkmark
        float checkPadding = boxSize * 0.25f;
        renderer.updateRect(
            m_checkRenderId,
            boxX + checkPadding,
            boxY + checkPadding,
            boxSize - checkPadding * 2,
            boxSize - checkPadding * 2,
            checkColor,
            checkLayer
        );
    } else {
        // Hide checkmark when unchecked (zero-size rect)
        renderer.updateRect(m_checkRenderId, 0, 0, 0, 0, 0x00000000, checkLayer);
    }

    // Render label text if present (retained mode)
    if (!text.empty()) {
        int textLayer = renderer.nextLayer();
        float textX = boxX + boxSize + spacing;
        float textY = absY + height * 0.5f;
        renderer.updateText(m_textRenderId, textX, textY, text, fontSize, textColor, textLayer);
    }

    // Render children on top
    renderChildren(renderer);
}

bool UICheckbox::containsPoint(float px, float py) const {
    return px >= absX && px < absX + width &&
           py >= absY && py < absY + height;
}

bool UICheckbox::onMouseButton(int button, bool pressed, float x, float y) {
    if (button == 0) {
        if (pressed && containsPoint(x, y)) {
            isPressed = true;
            return true;
        }
        if (!pressed && isPressed && containsPoint(x, y)) {
            // Click complete - toggle
            toggle();
            isPressed = false;
            return true;
        }
        isPressed = false;
    }
    return false;
}

void UICheckbox::toggle() {
    checked = !checked;
    // Value changed event will be published by UIModule
}

} // namespace grove
