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
    // Register with renderer on first render (need 3 entries: track, fill, handle)
    if (!m_registered) {
        m_renderId = renderer.registerEntry();        // Track (background)
        m_fillRenderId = renderer.registerEntry();    // Fill (progress)
        m_handleRenderId = renderer.registerEntry();  // Handle
        m_registered = true;
        // Set destroy callback to unregister all three
        setDestroyCallback([&renderer, fillId = m_fillRenderId, handleId = m_handleRenderId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(fillId);
            renderer.unregisterEntry(handleId);
        });
    }

    // Render track (background)
    int trackLayer = renderer.nextLayer();
    if (useTrackTexture && trackTextureId > 0) {
        renderer.updateSprite(m_renderId, absX, absY, width, height, trackTextureId, trackTintColor, trackLayer);
    } else {
        renderer.updateRect(m_renderId, absX, absY, width, height, trackColor, trackLayer);
    }

    // Render fill (progress)
    int fillLayer = renderer.nextLayer();
    if (horizontal) {
        float fillWidth = (value - minValue) / (maxValue - minValue) * width;
        if (useFillTexture && fillTextureId > 0) {
            renderer.updateSprite(m_fillRenderId, absX, absY, fillWidth, height, fillTextureId, fillTintColor, fillLayer);
        } else {
            renderer.updateRect(m_fillRenderId, absX, absY, fillWidth, height, fillColor, fillLayer);
        }
    } else {
        float fillHeight = (value - minValue) / (maxValue - minValue) * height;
        if (useFillTexture && fillTextureId > 0) {
            renderer.updateSprite(m_fillRenderId, absX, absY + height - fillHeight, width, fillHeight, fillTextureId, fillTintColor, fillLayer);
        } else {
            renderer.updateRect(m_fillRenderId, absX, absY + height - fillHeight, width, fillHeight, fillColor, fillLayer);
        }
    }

    // Render handle
    float handleX, handleY;
    calculateHandlePosition(handleX, handleY);

    // Handle is a small square
    int handleLayer = renderer.nextLayer();
    float halfHandle = handleSize * 0.5f;

    if (useHandleTexture && handleTextureId > 0) {
        renderer.updateSprite(
            m_handleRenderId,
            handleX - halfHandle,
            handleY - halfHandle,
            handleSize,
            handleSize,
            handleTextureId,
            handleTintColor,
            handleLayer
        );
    } else {
        renderer.updateRect(
            m_handleRenderId,
            handleX - halfHandle,
            handleY - halfHandle,
            handleSize,
            handleSize,
            handleColor,
            handleLayer
        );
    }

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
