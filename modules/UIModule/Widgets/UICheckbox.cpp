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
    // Render checkbox box
    float boxX = absX;
    float boxY = absY + (height - boxSize) * 0.5f;  // Vertically center the box

    // Box background
    uint32_t currentBoxColor = isHovered ? 0x475569FF : boxColor;
    renderer.drawRect(boxX, boxY, boxSize, boxSize, currentBoxColor);

    // Check mark if checked
    if (checked) {
        // Draw a smaller filled rect as checkmark
        float checkPadding = boxSize * 0.25f;
        renderer.drawRect(
            boxX + checkPadding,
            boxY + checkPadding,
            boxSize - checkPadding * 2,
            boxSize - checkPadding * 2,
            checkColor
        );
    }

    // Render label text if present
    if (!text.empty()) {
        float textX = boxX + boxSize + spacing;
        float textY = absY + height * 0.5f;
        renderer.drawText(textX, textY, text, fontSize, textColor);
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
