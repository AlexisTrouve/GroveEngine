#include "UISlider.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>
#include <cmath>

namespace grove {

void UISlider::update(UIContext& ctx, float deltaTime) {
    // Check if mouse is over slider
    isHovered = containsPoint(ctx.mouseX, ctx.mouseY);

    // Handle dragging
    if (isDragging && ctx.mouseDown) {
        onMouseDrag(ctx.mouseX, ctx.mouseY);
    } else if (isDragging && !ctx.mouseDown) {
        isDragging = false;
    }

    // Update children
    updateChildren(ctx, deltaTime);
}

void UISlider::render(UIRenderer& renderer) {
    // Render track (background)
    renderer.drawRect(absX, absY, width, height, trackColor);

    // Render fill (progress)
    if (horizontal) {
        float fillWidth = (value - minValue) / (maxValue - minValue) * width;
        renderer.drawRect(absX, absY, fillWidth, height, fillColor);
    } else {
        float fillHeight = (value - minValue) / (maxValue - minValue) * height;
        renderer.drawRect(absX, absY + height - fillHeight, width, fillHeight, fillColor);
    }

    // Render handle
    float handleX, handleY;
    calculateHandlePosition(handleX, handleY);

    // Handle is a small square
    float halfHandle = handleSize * 0.5f;
    renderer.drawRect(
        handleX - halfHandle,
        handleY - halfHandle,
        handleSize,
        handleSize,
        handleColor
    );

    // Render children on top
    renderChildren(renderer);
}

bool UISlider::containsPoint(float px, float py) const {
    return px >= absX && px < absX + width &&
           py >= absY && py < absY + height;
}

bool UISlider::onMouseButton(int button, bool pressed, float x, float y) {
    if (button == 0 && pressed && containsPoint(x, y)) {
        isDragging = true;
        onMouseDrag(x, y);
        return true;
    }
    if (button == 0 && !pressed && isDragging) {
        isDragging = false;
        return true;
    }
    return false;
}

void UISlider::onMouseDrag(float x, float y) {
    float newValue = calculateValueFromPosition(x, y);
    setValue(newValue);
}

void UISlider::setValue(float newValue) {
    // Clamp to range
    newValue = std::max(minValue, std::min(maxValue, newValue));

    // Apply step if needed
    if (step > 0.0f) {
        newValue = std::round(newValue / step) * step;
    }

    // Only update if changed
    if (newValue != value) {
        value = newValue;
        // Value changed event will be published by UIModule
    }
}

void UISlider::calculateHandlePosition(float& handleX, float& handleY) const {
    float t = (value - minValue) / (maxValue - minValue);

    if (horizontal) {
        handleX = absX + t * width;
        handleY = absY + height * 0.5f;
    } else {
        handleX = absX + width * 0.5f;
        handleY = absY + height - (t * height);
    }
}

float UISlider::calculateValueFromPosition(float x, float y) const {
    float t;

    if (horizontal) {
        t = (x - absX) / width;
    } else {
        t = 1.0f - (y - absY) / height;
    }

    t = std::max(0.0f, std::min(1.0f, t));
    return minValue + t * (maxValue - minValue);
}

} // namespace grove
