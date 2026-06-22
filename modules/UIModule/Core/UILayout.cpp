#include "UILayout.h"
#include "UIWidget.h"
#include <algorithm>
#include <cmath>

namespace grove {

// =============================================================================
// Measurement (Bottom-Up)
// =============================================================================

LayoutMeasurement UILayout::measure(UIWidget* widget) {
    if (!widget) {
        return {0.0f, 0.0f};
    }

    LayoutMeasurement result;

    // Choose measurement algorithm based on layout mode
    switch (widget->layoutProps.mode) {
        case LayoutMode::Vertical:
            result = measureVertical(widget);
            break;
        case LayoutMode::Horizontal:
            result = measureHorizontal(widget);
            break;
        case LayoutMode::Stack:
            result = measureStack(widget);
            break;
        case LayoutMode::Grid:
            result = measureGrid(widget);
            break;
        case LayoutMode::Absolute:
        default:
            // For absolute layout, use explicit size or measure children
            result.preferredWidth = widget->width;
            result.preferredHeight = widget->height;
            // If size is 0, measure children and use their bounds
            if (result.preferredWidth == 0.0f || result.preferredHeight == 0.0f) {
                float maxX = 0.0f, maxY = 0.0f;
                for (auto& child : widget->children) {
                    if (child->visible) {
                        auto childMeasure = measure(child.get());
                        maxX = std::max(maxX, child->x + childMeasure.preferredWidth);
                        maxY = std::max(maxY, child->y + childMeasure.preferredHeight);
                    }
                }
                if (result.preferredWidth == 0.0f) result.preferredWidth = maxX;
                if (result.preferredHeight == 0.0f) result.preferredHeight = maxY;
            }
            break;
    }

    // Add padding
    result.preferredWidth += widget->layoutProps.getTotalPaddingX();
    result.preferredHeight += widget->layoutProps.getTotalPaddingY();

    // Apply min/max constraints
    result.preferredWidth = clampSize(result.preferredWidth,
                                      widget->layoutProps.minWidth,
                                      widget->layoutProps.maxWidth);
    result.preferredHeight = clampSize(result.preferredHeight,
                                       widget->layoutProps.minHeight,
                                       widget->layoutProps.maxHeight);

    // If explicit size is set, use it
    if (widget->width > 0) result.preferredWidth = widget->width;
    if (widget->height > 0) result.preferredHeight = widget->height;

    return result;
}

LayoutMeasurement UILayout::measureVertical(UIWidget* widget) {
    LayoutMeasurement result{0.0f, 0.0f};

    bool hasVisibleChild = false;
    for (auto& child : widget->children) {
        if (!child->visible) continue;

        auto childMeasure = measure(child.get());
        result.preferredWidth = std::max(result.preferredWidth, childMeasure.preferredWidth);
        result.preferredHeight += childMeasure.preferredHeight;

        if (hasVisibleChild) {
            result.preferredHeight += widget->layoutProps.spacing;
        }
        hasVisibleChild = true;
    }

    return result;
}

LayoutMeasurement UILayout::measureHorizontal(UIWidget* widget) {
    LayoutMeasurement result{0.0f, 0.0f};

    bool hasVisibleChild = false;
    for (auto& child : widget->children) {
        if (!child->visible) continue;

        auto childMeasure = measure(child.get());
        result.preferredWidth += childMeasure.preferredWidth;
        result.preferredHeight = std::max(result.preferredHeight, childMeasure.preferredHeight);

        if (hasVisibleChild) {
            result.preferredWidth += widget->layoutProps.spacing;
        }
        hasVisibleChild = true;
    }

    return result;
}

LayoutMeasurement UILayout::measureStack(UIWidget* widget) {
    LayoutMeasurement result{0.0f, 0.0f};

    for (auto& child : widget->children) {
        if (!child->visible) continue;

        auto childMeasure = measure(child.get());
        result.preferredWidth = std::max(result.preferredWidth, childMeasure.preferredWidth);
        result.preferredHeight = std::max(result.preferredHeight, childMeasure.preferredHeight);
    }

    return result;
}

// =============================================================================
// Layout (Top-Down)
// =============================================================================

void UILayout::layout(UIWidget* widget, float availableWidth, float availableHeight) {
    if (!widget) return;

    // Apply size constraints
    widget->width = clampSize(availableWidth, widget->layoutProps.minWidth, widget->layoutProps.maxWidth);
    widget->height = clampSize(availableHeight, widget->layoutProps.minHeight, widget->layoutProps.maxHeight);

    // Calculate content area (available space minus padding)
    float contentWidth = widget->width - widget->layoutProps.getTotalPaddingX();
    float contentHeight = widget->height - widget->layoutProps.getTotalPaddingY();

    // Layout children based on mode
    switch (widget->layoutProps.mode) {
        case LayoutMode::Vertical:
            layoutVertical(widget, contentWidth, contentHeight);
            break;
        case LayoutMode::Horizontal:
            layoutHorizontal(widget, contentWidth, contentHeight);
            break;
        case LayoutMode::Stack:
            layoutStack(widget, contentWidth, contentHeight);
            break;
        case LayoutMode::Grid:
            layoutGrid(widget, contentWidth, contentHeight);
            break;
        case LayoutMode::Absolute:
        default: {
            // Absolute children keep their explicit x/y — UNLESS anchored, in which case the
            // position is derived from this widget's content box, so it tracks the box on reflow
            // (e.g. a HUD button glued to the bottom-right corner). Percent sizing still applies.
            const float padL = widget->layoutProps.getLeftPadding();
            const float padT = widget->layoutProps.getTopPadding();
            for (auto& child : widget->children) {
                if (!child->visible) continue;
                auto childMeasure = measure(child.get());
                float cw = child->widthPercent  > 0.0f ? child->widthPercent  * contentWidth  : childMeasure.preferredWidth;
                float ch = child->heightPercent > 0.0f ? child->heightPercent * contentHeight : childMeasure.preferredHeight;
                if (child->anchor != Anchor::None) {
                    AnchorPos p = resolveAnchor(child->anchor, padL, padT, contentWidth, contentHeight,
                                                cw, ch, child->anchorOffsetX, child->anchorOffsetY);
                    child->x = p.x;
                    child->y = p.y;
                }
                layout(child.get(), cw, ch);
            }
            break;
        }
    }
}

void UILayout::layoutVertical(UIWidget* widget, float availableWidth, float availableHeight) {
    const size_t n = widget->children.size();

    // PERF: measure each visible child ONCE. measure() is recursive; the passes below used to call
    // it up to 3x per child (fixed-size pre-pass, flex assign, cross-axis). Caching gives identical
    // output (locked by UILayoutUnit) for a fraction of the work on deep trees.
    std::vector<LayoutMeasurement> m(n);
    for (size_t i = 0; i < n; ++i) {
        if (widget->children[i]->visible) m[i] = measure(widget->children[i].get());
    }

    // Count visible children + flex total; main-axis percent is a fixed reservation before flex.
    int visibleCount = 0;
    float totalFlex = 0.0f;
    float fixedHeight = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        auto& child = widget->children[i];
        if (!child->visible) continue;
        visibleCount++;
        if (child->heightPercent > 0.0f) { fixedHeight += child->heightPercent * availableHeight; continue; }
        totalFlex += child->layoutProps.flex;
        if (child->layoutProps.flex == 0.0f) fixedHeight += m[i].preferredHeight;
    }

    if (visibleCount == 0) return;

    const float totalSpacing = (visibleCount - 1) * widget->layoutProps.spacing;
    const float remainingHeight = availableHeight - fixedHeight - totalSpacing;

    // Assign main-axis (height) sizes.
    std::vector<float> childHeights(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        auto& child = widget->children[i];
        if (!child->visible) continue;
        if (child->heightPercent > 0.0f) {
            childHeights[i] = child->heightPercent * availableHeight;
        } else if (child->layoutProps.flex > 0.0f && totalFlex > 0.0f) {
            childHeights[i] = (child->layoutProps.flex / totalFlex) * remainingHeight;
        } else {
            childHeights[i] = m[i].preferredHeight;
        }
    }

    // Position children + assign cross-axis (width).
    float offsetY = widget->layoutProps.getTopPadding();
    for (size_t i = 0; i < n; ++i) {
        auto& child = widget->children[i];
        if (!child->visible) continue;

        const float childHeight = childHeights[i];
        float childWidth;
        if (child->widthPercent > 0.0f) {
            childWidth = child->widthPercent * availableWidth;
        } else if (widget->layoutProps.align == Alignment::Stretch) {
            childWidth = availableWidth;
        } else {
            childWidth = m[i].preferredWidth;
        }

        float childX = widget->layoutProps.getLeftPadding();
        switch (widget->layoutProps.align) {
            case Alignment::Center: childX += (availableWidth - childWidth) * 0.5f; break;
            case Alignment::End:    childX += availableWidth - childWidth; break;
            default: break;
        }

        child->x = childX;
        child->y = offsetY;
        layout(child.get(), childWidth, childHeight);
        offsetY += childHeight + widget->layoutProps.spacing;
    }
}

void UILayout::layoutHorizontal(UIWidget* widget, float availableWidth, float availableHeight) {
    const size_t n = widget->children.size();

    // PERF: measure each visible child once (see layoutVertical). Identical output, less work.
    std::vector<LayoutMeasurement> m(n);
    for (size_t i = 0; i < n; ++i) {
        if (widget->children[i]->visible) m[i] = measure(widget->children[i].get());
    }

    int visibleCount = 0;
    float totalFlex = 0.0f;
    float fixedWidth = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        auto& child = widget->children[i];
        if (!child->visible) continue;
        visibleCount++;
        if (child->widthPercent > 0.0f) { fixedWidth += child->widthPercent * availableWidth; continue; }
        totalFlex += child->layoutProps.flex;
        if (child->layoutProps.flex == 0.0f) fixedWidth += m[i].preferredWidth;
    }

    if (visibleCount == 0) return;

    const float totalSpacing = (visibleCount - 1) * widget->layoutProps.spacing;
    const float remainingWidth = availableWidth - fixedWidth - totalSpacing;

    std::vector<float> childWidths(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        auto& child = widget->children[i];
        if (!child->visible) continue;
        if (child->widthPercent > 0.0f) {
            childWidths[i] = child->widthPercent * availableWidth;
        } else if (child->layoutProps.flex > 0.0f && totalFlex > 0.0f) {
            childWidths[i] = (child->layoutProps.flex / totalFlex) * remainingWidth;
        } else {
            childWidths[i] = m[i].preferredWidth;
        }
    }

    float offsetX = widget->layoutProps.getLeftPadding();
    for (size_t i = 0; i < n; ++i) {
        auto& child = widget->children[i];
        if (!child->visible) continue;

        const float childWidth = childWidths[i];
        float childHeight;
        if (child->heightPercent > 0.0f) {
            childHeight = child->heightPercent * availableHeight;
        } else if (widget->layoutProps.align == Alignment::Stretch) {
            childHeight = availableHeight;
        } else {
            childHeight = m[i].preferredHeight;
        }

        float childY = widget->layoutProps.getTopPadding();
        switch (widget->layoutProps.align) {
            case Alignment::Center: childY += (availableHeight - childHeight) * 0.5f; break;
            case Alignment::End:    childY += availableHeight - childHeight; break;
            default: break;
        }

        child->x = offsetX;
        child->y = childY;
        layout(child.get(), childWidth, childHeight);
        offsetX += childWidth + widget->layoutProps.spacing;
    }
}

void UILayout::layoutStack(UIWidget* widget, float availableWidth, float availableHeight) {
    float offsetX = widget->layoutProps.getLeftPadding();
    float offsetY = widget->layoutProps.getTopPadding();

    for (auto& child : widget->children) {
        if (!child->visible) continue;

        float childWidth, childHeight;

        // Per axis: percent wins (fraction of the content box), else Stretch fills, else measured.
        auto childMeasure = measure(child.get());
        const bool stretch = (widget->layoutProps.align == Alignment::Stretch);
        childWidth  = child->widthPercent  > 0.0f ? child->widthPercent  * availableWidth
                    : (stretch ? availableWidth  : childMeasure.preferredWidth);
        childHeight = child->heightPercent > 0.0f ? child->heightPercent * availableHeight
                    : (stretch ? availableHeight : childMeasure.preferredHeight);

        // Position based on alignment
        float childX = offsetX;
        float childY = offsetY;

        switch (widget->layoutProps.align) {
            case Alignment::Center:
                childX += (availableWidth - childWidth) * 0.5f;
                childY += (availableHeight - childHeight) * 0.5f;
                break;
            case Alignment::End:
                childX += availableWidth - childWidth;
                childY += availableHeight - childHeight;
                break;
            default:
                break;
        }

        child->x = childX;
        child->y = childY;

        layout(child.get(), childWidth, childHeight);
    }
}

// =============================================================================
// Grid
// =============================================================================

LayoutMeasurement UILayout::measureGrid(UIWidget* widget) {
    const int cols = std::max(1, widget->layoutProps.columns);
    int visible = 0;
    for (auto& child : widget->children) {
        if (child->visible) visible++;
    }
    const int rows = (visible + cols - 1) / cols;

    LayoutMeasurement result{0.0f, 0.0f};
    // Width is left to the container (cells fill it). Height = rows * rowHeight + inter-row gaps,
    // or 0 when rowHeight is unset (the cell height then derives from the width at layout time).
    const float gap = widget->layoutProps.spacing;
    const float rowH = widget->layoutProps.rowHeight;
    if (rows > 0) {
        result.preferredHeight = rows * rowH + (rows - 1) * gap;
    }
    return result;
}

void UILayout::layoutGrid(UIWidget* widget, float availableWidth, float availableHeight) {
    (void)availableHeight;  // rows grow downward; the container scrolls/clips if it overflows
    const int cols = std::max(1, widget->layoutProps.columns);
    const float gap = widget->layoutProps.spacing;
    const float padL = widget->layoutProps.getLeftPadding();
    const float padT = widget->layoutProps.getTopPadding();

    // Cells share the content width minus inter-column gaps -> they grow/shrink with the container,
    // so the grid REFLOWS on resize. Cell height = explicit rowHeight, else square (= cell width).
    const float cellW = (availableWidth - (cols - 1) * gap) / static_cast<float>(cols);
    const float rowH = widget->layoutProps.rowHeight;
    const float cellH = rowH > 0.0f ? rowH : cellW;

    int i = 0;
    for (auto& child : widget->children) {
        if (!child->visible) continue;
        CellRect c = gridCellRect(i, cols, cellW, cellH, gap);
        child->x = padL + c.x;
        child->y = padT + c.y;
        layout(child.get(), c.w, c.h);
        ++i;
    }
}

CellRect UILayout::gridCellRect(int index, int cols, float cellW, float cellH, float gap) {
    const int c = (cols > 0) ? cols : 1;
    const int col = index % c;
    const int row = index / c;
    CellRect r;
    r.x = col * (cellW + gap);   // each column steps over the cell + one gap
    r.y = row * (cellH + gap);
    r.w = cellW;
    r.h = cellH;
    return r;
}

// =============================================================================
// Utilities
// =============================================================================

float UILayout::clampSize(float size, float minSize, float maxSize) {
    if (minSize > 0.0f) {
        size = std::max(size, minSize);
    }
    if (maxSize > 0.0f) {
        size = std::min(size, maxSize);
    }
    return size;
}

AnchorPos UILayout::resolveAnchor(Anchor anchor,
                                  float boxX, float boxY, float boxW, float boxH,
                                  float w, float h, float offX, float offY) {
    // None = no anchoring: the offset passes straight through (the caller keeps explicit x/y).
    if (anchor == Anchor::None) {
        return { offX, offY };
    }

    // Horizontal: left edge (default) / centered / right-aligned, per the anchor's column.
    float x = boxX;
    switch (anchor) {
        case Anchor::Top: case Anchor::Center: case Anchor::Bottom:
            x = boxX + (boxW - w) * 0.5f; break;
        case Anchor::TopRight: case Anchor::Right: case Anchor::BottomRight:
            x = boxX + boxW - w; break;
        default: break;  // TopLeft / Left / BottomLeft -> left edge
    }

    // Vertical: top edge (default) / centered / bottom-aligned, per the anchor's row.
    float y = boxY;
    switch (anchor) {
        case Anchor::Left: case Anchor::Center: case Anchor::Right:
            y = boxY + (boxH - h) * 0.5f; break;
        case Anchor::BottomLeft: case Anchor::Bottom: case Anchor::BottomRight:
            y = boxY + boxH - h; break;
        default: break;  // TopLeft / Top / TopRight -> top edge
    }

    return { x + offX, y + offY };
}

} // namespace grove
