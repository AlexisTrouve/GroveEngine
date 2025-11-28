#include "UIScrollPanel.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>
#include <cmath>

namespace grove {

void UIScrollPanel::update(UIContext& ctx, float deltaTime) {
    if (!visible) return;

    // Compute content size from children
    computeContentSize();

    // Handle scroll interaction
    updateScrollInteraction(ctx);

    // Clamp scroll offset
    clampScrollOffset();

    // Update children with scroll offset applied
    for (auto& child : children) {
        if (child->visible) {
            // Temporarily adjust child position for scrolling
            float origX = child->x;
            float origY = child->y;

            child->x = origX - scrollOffsetX;
            child->y = origY - scrollOffsetY;

            child->update(ctx, deltaTime);

            // Restore original position
            child->x = origX;
            child->y = origY;
        }
    }
}

void UIScrollPanel::render(UIRenderer& renderer) {
    if (!visible) return;

    // Render background
    renderer.drawRect(absX, absY, width, height, bgColor);

    // Render border if needed
    if (borderWidth > 0.0f) {
        // Top border
        renderer.drawRect(absX, absY, width, borderWidth, borderColor);
        // Bottom border
        renderer.drawRect(absX, absY + height - borderWidth, width, borderWidth, borderColor);
        // Left border
        renderer.drawRect(absX, absY, borderWidth, height, borderColor);
        // Right border
        renderer.drawRect(absX + width - borderWidth, absY, borderWidth, height, borderColor);
    }

    // Render children with scroll offset and clipping
    // Note: Proper clipping would require scissor test in renderer
    // For now, we render all children but offset them
    for (auto& child : children) {
        if (child->visible) {
            // Save original absolute position
            float origAbsX = child->absX;
            float origAbsY = child->absY;

            // Apply scroll offset
            child->absX = absX + child->x - scrollOffsetX;
            child->absY = absY + child->y - scrollOffsetY;

            // Simple visibility culling - only render if in bounds
            float visX, visY, visW, visH;
            getVisibleRect(visX, visY, visW, visH);

            bool inBounds = (child->absX + child->width >= visX &&
                           child->absX <= visX + visW &&
                           child->absY + child->height >= visY &&
                           child->absY <= visY + visH);

            if (inBounds) {
                child->render(renderer);
            }

            // Restore original absolute position
            child->absX = origAbsX;
            child->absY = origAbsY;
        }
    }

    // Render scrollbar
    if (showScrollbar && scrollVertical && contentHeight > height) {
        renderScrollbar(renderer);
    }
}

void UIScrollPanel::handleMouseWheel(float wheelDelta) {
    if (scrollVertical) {
        scrollOffsetY -= wheelDelta * 20.0f; // Scroll speed
        clampScrollOffset();
    }
}

void UIScrollPanel::computeContentSize() {
    if (children.empty()) {
        contentWidth = width;
        contentHeight = height;
        return;
    }

    float maxX = 0.0f;
    float maxY = 0.0f;

    for (const auto& child : children) {
        float childRight = child->x + child->width;
        float childBottom = child->y + child->height;

        if (childRight > maxX) maxX = childRight;
        if (childBottom > maxY) maxY = childBottom;
    }

    contentWidth = std::max(maxX, width);
    contentHeight = std::max(maxY, height);
}

void UIScrollPanel::clampScrollOffset() {
    // Vertical clamping
    if (scrollVertical) {
        float maxScrollY = std::max(0.0f, contentHeight - height);
        scrollOffsetY = std::clamp(scrollOffsetY, 0.0f, maxScrollY);
    } else {
        scrollOffsetY = 0.0f;
    }

    // Horizontal clamping
    if (scrollHorizontal) {
        float maxScrollX = std::max(0.0f, contentWidth - width);
        scrollOffsetX = std::clamp(scrollOffsetX, 0.0f, maxScrollX);
    } else {
        scrollOffsetX = 0.0f;
    }
}

void UIScrollPanel::getVisibleRect(float& outX, float& outY, float& outW, float& outH) const {
    outX = absX;
    outY = absY;
    outW = width;
    outH = height;
}

bool UIScrollPanel::isScrollbarHovered(const UIContext& ctx) const {
    if (!showScrollbar || !scrollVertical || contentHeight <= height) {
        return false;
    }

    float sbX, sbY, sbW, sbH;
    getScrollbarRect(sbX, sbY, sbW, sbH);

    return ctx.isMouseInRect(sbX, sbY, sbW, sbH);
}

void UIScrollPanel::getScrollbarRect(float& outX, float& outY, float& outW, float& outH) const {
    // Scrollbar is on the right edge
    float scrollbarX = absX + width - scrollbarWidth;

    // Scrollbar height proportional to visible area
    float visibleRatio = height / contentHeight;
    float scrollbarHeight = height * visibleRatio;
    scrollbarHeight = std::max(scrollbarHeight, 20.0f); // Minimum height

    // Scrollbar position based on scroll offset
    float scrollRatio = scrollOffsetY / (contentHeight - height);
    float scrollbarY = absY + scrollRatio * (height - scrollbarHeight);

    outX = scrollbarX;
    outY = scrollbarY;
    outW = scrollbarWidth;
    outH = scrollbarHeight;
}

void UIScrollPanel::renderScrollbar(UIRenderer& renderer) {
    // Render scrollbar background track
    float trackX = absX + width - scrollbarWidth;
    renderer.drawRect(trackX, absY, scrollbarWidth, height, scrollbarBgColor);

    // Render scrollbar thumb
    float sbX, sbY, sbW, sbH;
    getScrollbarRect(sbX, sbY, sbW, sbH);

    // Use hover color if hovered (would need ctx passed to render, simplified for now)
    renderer.drawRect(sbX, sbY, sbW, sbH, scrollbarColor);
}

void UIScrollPanel::updateScrollInteraction(UIContext& ctx) {
    bool mouseInPanel = ctx.isMouseInRect(absX, absY, width, height);

    // Mouse wheel scrolling
    // Note: Mouse wheel events would need to be forwarded from UIModule
    // For now, this is a placeholder - wheel events handled externally

    // Drag to scroll
    if (dragToScroll && mouseInPanel) {
        if (ctx.mousePressed && !isDraggingContent && !isDraggingScrollbar) {
            // Check if clicked on scrollbar
            if (isScrollbarHovered(ctx)) {
                isDraggingScrollbar = true;
                dragStartY = ctx.mouseY;
                scrollStartY = scrollOffsetY;
            } else {
                // Start dragging content
                isDraggingContent = true;
                dragStartX = ctx.mouseX;
                dragStartY = ctx.mouseY;
                scrollStartX = scrollOffsetX;
                scrollStartY = scrollOffsetY;
                ctx.setActive(id);
            }
        }
    }

    // Handle drag
    if (isDraggingContent && ctx.mouseDown) {
        float deltaX = ctx.mouseX - dragStartX;
        float deltaY = ctx.mouseY - dragStartY;

        if (scrollHorizontal) {
            scrollOffsetX = scrollStartX - deltaX;
        }
        if (scrollVertical) {
            scrollOffsetY = scrollStartY - deltaY;
        }
    }

    // Handle scrollbar drag
    if (isDraggingScrollbar && ctx.mouseDown) {
        float deltaY = ctx.mouseY - dragStartY;

        // Convert mouse delta to scroll offset delta
        float scrollableHeight = height - scrollbarWidth;
        float scrollRange = contentHeight - height;
        float scrollDelta = (deltaY / scrollableHeight) * scrollRange;

        scrollOffsetY = scrollStartY + scrollDelta;
    }

    // Release drag
    if (ctx.mouseReleased) {
        if (isDraggingContent) {
            ctx.clearActive();
        }
        isDraggingContent = false;
        isDraggingScrollbar = false;
    }
}

} // namespace grove
