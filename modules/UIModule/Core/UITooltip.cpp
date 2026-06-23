#include "UITooltip.h"
#include "UIContext.h"
#include "UIWidget.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>
#include <cmath>

namespace grove {

void UITooltipManager::update(UIWidget* hoveredWidget, const UIContext& ctx, float deltaTime) {
    // No widget, or one without a tooltip -> hide.
    if (!hoveredWidget || hoveredWidget->tooltip.empty()) {
        reset();
        return;
    }

    // Track the hovered widget by POINTER (data-driven repeater items all share an empty id, so an id check
    // never fired when sweeping between them and the tooltip stayed stuck on the first item). On a change: if
    // a tooltip is ALREADY showing, switch to the new one immediately (snappy); else restart the hover delay.
    if (hoveredWidget != m_currentWidget) {
        m_currentWidget = hoveredWidget;
        m_hoverTime = 0.0f;
        if (m_visible) {
            m_currentText = hoveredWidget->tooltip;
            computeTooltipSize(m_currentText);
        }
    }

    m_hoverTime += deltaTime;
    if (m_hoverTime >= hoverDelay && !m_visible) {
        m_visible = true;
        m_currentText = hoveredWidget->tooltip;
        computeTooltipSize(m_currentText);
    }

    if (m_visible) {
        computeTooltipPosition(ctx.mouseX, ctx.mouseY, ctx.screenWidth, ctx.screenHeight);
    }
}

void UITooltipManager::render(UIRenderer& renderer, float screenWidth, float screenHeight) {
    if (!m_visible || m_currentText.empty()) {
        return;
    }

    // Render background
    renderer.drawRect(m_tooltipX, m_tooltipY, m_tooltipWidth, m_tooltipHeight, bgColor);

    // Render border
    if (borderWidth > 0.0f) {
        // Top
        renderer.drawRect(m_tooltipX, m_tooltipY, m_tooltipWidth, borderWidth, borderColor);
        // Bottom
        renderer.drawRect(m_tooltipX, m_tooltipY + m_tooltipHeight - borderWidth,
                        m_tooltipWidth, borderWidth, borderColor);
        // Left
        renderer.drawRect(m_tooltipX, m_tooltipY, borderWidth, m_tooltipHeight, borderColor);
        // Right
        renderer.drawRect(m_tooltipX + m_tooltipWidth - borderWidth, m_tooltipY,
                        borderWidth, m_tooltipHeight, borderColor);
    }

    // Render text (centered in tooltip box)
    float textX = m_tooltipX + padding;
    float textY = m_tooltipY + padding;
    renderer.drawText(textX, textY, m_currentText, fontSize, textColor);
}

void UITooltipManager::reset() {
    m_visible = false;
    m_hoverTime = 0.0f;
    m_currentWidget = nullptr;
    m_currentText.clear();
}

void UITooltipManager::computeTooltipSize(const std::string& text) {
    // Approximate text width (rough estimate)
    // In a real implementation, we'd measure text properly
    const float CHAR_WIDTH = 8.0f;  // Approximate character width
    float textWidth = text.length() * CHAR_WIDTH;

    // Clamp to max width
    textWidth = std::min(textWidth, maxWidth - 2.0f * padding);

    // Compute tooltip size with padding
    m_tooltipWidth = textWidth + 2.0f * padding;
    m_tooltipHeight = fontSize + 2.0f * padding;
}

void UITooltipManager::computeTooltipPosition(float cursorX, float cursorY,
                                              float screenWidth, float screenHeight) {
    // Start with cursor offset
    float x = cursorX + offsetX;
    float y = cursorY + offsetY;

    // Prevent tooltip from going off right edge
    if (x + m_tooltipWidth > screenWidth) {
        x = cursorX - m_tooltipWidth - offsetX;
    }

    // Prevent tooltip from going off bottom edge
    if (y + m_tooltipHeight > screenHeight) {
        y = cursorY - m_tooltipHeight - offsetY;
    }

    // Clamp to screen bounds
    x = std::max(0.0f, std::min(x, screenWidth - m_tooltipWidth));
    y = std::max(0.0f, std::min(y, screenHeight - m_tooltipHeight));

    m_tooltipX = x;
    m_tooltipY = y;
}

} // namespace grove
