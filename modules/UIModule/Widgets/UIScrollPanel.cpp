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

    // QUOI : appliquer le scroll aux positions ABSOLUES des enfants, de façon
    //   PERSISTANTE (l'offset reste dans absX/absY après cette passe).
    // POURQUOI : l'ancien code décalait child->x temporairement (sans effet sur absX,
    //   utilisé par le hit-test et le hover), et render() posait/​restaurait child->absX
    //   → le rendu était scrollé mais le hit-test voyait la position non-scrollée, donc
    //   cliquer un enfant scrollé ratait. En posant absX scrollé ici et en le laissant,
    //   rendu + hit-test (frame suivante) + hover coïncident.
    // COMMENT : absX scrollé = (notre absX) + (x relatif intact) - offset. On propage aux
    //   petits-enfants via leur computeAbsolutePosition() (qui dérive du child->absX
    //   désormais scrollé). On ne modifie PAS child->x (computeContentSize en dépend).
    // LIMITE connue : un enfant qui est lui-même un panel en layout non-absolute rappelle
    //   computeAbsolutePosition() dans son update() et se "dé-scrolle" → cas rare, à
    //   traiter dans le rework scissor/clipping (le scrollpanel reste candidat rewrite).
    for (auto& child : children) {
        if (!child->visible) continue;

        child->absX = absX + child->x - scrollOffsetX;
        child->absY = absY + child->y - scrollOffsetY;
        for (auto& grandChild : child->children) {
            grandChild->computeAbsolutePosition();
        }

        child->update(ctx, deltaTime);
    }
}

void UIScrollPanel::render(UIRenderer& renderer) {
    if (!visible) return;

    // Register with renderer on first render
    // Need 7 entries: background + 4 borders + scrollbar track + scrollbar thumb
    if (!m_registered) {
        m_renderId = renderer.registerEntry();        // Background
        m_borderTopId = renderer.registerEntry();     // Border top
        m_borderBottomId = renderer.registerEntry();  // Border bottom
        m_borderLeftId = renderer.registerEntry();    // Border left
        m_borderRightId = renderer.registerEntry();   // Border right
        m_scrollTrackId = renderer.registerEntry();   // Scrollbar track
        m_scrollThumbId = renderer.registerEntry();   // Scrollbar thumb
        m_registered = true;

        // Set destroy callback to unregister all entries
        setDestroyCallback([&renderer,
                           borderTopId = m_borderTopId,
                           borderBottomId = m_borderBottomId,
                           borderLeftId = m_borderLeftId,
                           borderRightId = m_borderRightId,
                           scrollTrackId = m_scrollTrackId,
                           scrollThumbId = m_scrollThumbId](uint32_t id) {
            renderer.unregisterEntry(id);              // Background
            renderer.unregisterEntry(borderTopId);
            renderer.unregisterEntry(borderBottomId);
            renderer.unregisterEntry(borderLeftId);
            renderer.unregisterEntry(borderRightId);
            renderer.unregisterEntry(scrollTrackId);
            renderer.unregisterEntry(scrollThumbId);
        });
    }

    // Render background
    int bgLayer = renderer.nextLayer();
    if (useBgTexture && bgTextureId > 0) {
        renderer.updateSprite(m_renderId, absX, absY, width, height, bgTextureId, bgTintColor, bgLayer);
    } else {
        renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, bgLayer);
    }

    // Render border if needed
    if (borderWidth > 0.0f) {
        int borderLayer = renderer.nextLayer();
        // Top border
        renderer.updateRect(m_borderTopId, absX, absY, width, borderWidth, borderColor, borderLayer);
        // Bottom border
        renderer.updateRect(m_borderBottomId, absX, absY + height - borderWidth, width, borderWidth, borderColor, borderLayer);
        // Left border
        renderer.updateRect(m_borderLeftId, absX, absY, borderWidth, height, borderColor, borderLayer);
        // Right border
        renderer.updateRect(m_borderRightId, absX + width - borderWidth, absY, borderWidth, height, borderColor, borderLayer);
    } else {
        // Hide borders by setting zero size when not needed
        int borderLayer = renderer.nextLayer();
        renderer.updateRect(m_borderTopId, 0, 0, 0, 0, 0, borderLayer);
        renderer.updateRect(m_borderBottomId, 0, 0, 0, 0, 0, borderLayer);
        renderer.updateRect(m_borderLeftId, 0, 0, 0, 0, 0, borderLayer);
        renderer.updateRect(m_borderRightId, 0, 0, 0, 0, 0, borderLayer);
    }

    // Render children. Leur absX/absY reflètent DÉJÀ le scroll (posé dans update()),
    // donc rendu et hit-test partagent les mêmes coordonnées — on ne décale/restaure
    // plus rien ici.
    // Note : pas encore de scissor/clipping ; on cull les enfants entièrement hors du
    // rect visible, mais un enfant partiellement visible n'est pas découpé.
    for (auto& child : children) {
        if (!child->visible) continue;

        float visX, visY, visW, visH;
        getVisibleRect(visX, visY, visW, visH);

        bool inBounds = (child->absX + child->width >= visX &&
                       child->absX <= visX + visW &&
                       child->absY + child->height >= visY &&
                       child->absY <= visY + visH);

        if (inBounds) {
            child->render(renderer);
        }
    }

    // Render scrollbar
    if (showScrollbar && scrollVertical && contentHeight > height) {
        renderScrollbar(renderer);
    } else {
        // Hide scrollbar elements when not needed
        int scrollLayer = renderer.nextLayer();
        renderer.updateRect(m_scrollTrackId, 0, 0, 0, 0, 0, scrollLayer);
        renderer.updateRect(m_scrollThumbId, 0, 0, 0, 0, 0, scrollLayer);
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
    int trackLayer = renderer.nextLayer();

    if (useScrollbarTrackTexture && scrollbarTrackTextureId > 0) {
        renderer.updateSprite(m_scrollTrackId, trackX, absY, scrollbarWidth, height, scrollbarTrackTextureId, scrollbarTrackTintColor, trackLayer);
    } else {
        renderer.updateRect(m_scrollTrackId, trackX, absY, scrollbarWidth, height, scrollbarBgColor, trackLayer);
    }

    // Render scrollbar thumb
    float sbX, sbY, sbW, sbH;
    getScrollbarRect(sbX, sbY, sbW, sbH);

    // Use hover color if hovered (would need ctx passed to render, simplified for now)
    int thumbLayer = renderer.nextLayer();

    if (useScrollbarThumbTexture && scrollbarThumbTextureId > 0) {
        renderer.updateSprite(m_scrollThumbId, sbX, sbY, sbW, sbH, scrollbarThumbTextureId, scrollbarThumbTintColor, thumbLayer);
    } else {
        renderer.updateRect(m_scrollThumbId, sbX, sbY, sbW, sbH, scrollbarColor, thumbLayer);
    }
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
