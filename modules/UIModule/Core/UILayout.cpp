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
        case LayoutMode::Absolute:
        default:
            // For absolute layout, just layout children with their preferred sizes
            for (auto& child : widget->children) {
                if (!child->visible) continue;
                auto childMeasure = measure(child.get());
                layout(child.get(), childMeasure.preferredWidth, childMeasure.preferredHeight);
            }
            break;
    }
}

void UILayout::layoutVertical(UIWidget* widget, float availableWidth, float availableHeight) {
    // Count visible children and calculate flex total
    int visibleCount = 0;
    float totalFlex = 0.0f;
    float fixedHeight = 0.0f;

    for (auto& child : widget->children) {
        if (!child->visible) continue;
        visibleCount++;
        totalFlex += child->layoutProps.flex;

        if (child->layoutProps.flex == 0.0f) {
            auto childMeasure = measure(child.get());
            fixedHeight += childMeasure.preferredHeight;
        }
    }

    if (visibleCount == 0) return;

    // Calculate spacing height
    float totalSpacing = (visibleCount - 1) * widget->layoutProps.spacing;
    float remainingHeight = availableHeight - fixedHeight - totalSpacing;

    // First pass: assign sizes
    std::vector<float> childHeights;
    for (auto& child : widget->children) {
        if (!child->visible) {
            childHeights.push_back(0.0f);
            continue;
        }

        float childHeight;
        if (child->layoutProps.flex > 0.0f && totalFlex > 0.0f) {
            childHeight = (child->layoutProps.flex / totalFlex) * remainingHeight;
        } else {
            auto childMeasure = measure(child.get());
            childHeight = childMeasure.preferredHeight;
        }

        childHeights.push_back(childHeight);
    }

    // Second pass: position children
    float offsetY = widget->layoutProps.getTopPadding();

    for (size_t i = 0; i < widget->children.size(); i++) {
        auto& child = widget->children[i];
        if (!child->visible) continue;

        float childHeight = childHeights[i];
        float childWidth;

        // Handle alignment
        if (widget->layoutProps.align == Alignment::Stretch) {
            childWidth = availableWidth;
        } else {
            auto childMeasure = measure(child.get());
            childWidth = childMeasure.preferredWidth;
        }

        // Position based on alignment
        float childX = widget->layoutProps.getLeftPadding();
        switch (widget->layoutProps.align) {
            case Alignment::Center:
                childX += (availableWidth - childWidth) * 0.5f;
                break;
            case Alignment::End:
                childX += availableWidth - childWidth;
                break;
            default:
                break;
        }

        child->x = childX;
        child->y = offsetY;

        layout(child.get(), childWidth, childHeight);

        offsetY += childHeight + widget->layoutProps.spacing;
    }
}

void UILayout::layoutHorizontal(UIWidget* widget, float availableWidth, float availableHeight) {
    // Count visible children and calculate flex total
    int visibleCount = 0;
    float totalFlex = 0.0f;
    float fixedWidth = 0.0f;

    for (auto& child : widget->children) {
        if (!child->visible) continue;
        visibleCount++;
        totalFlex += child->layoutProps.flex;

        if (child->layoutProps.flex == 0.0f) {
            auto childMeasure = measure(child.get());
            fixedWidth += childMeasure.preferredWidth;
        }
    }

    if (visibleCount == 0) return;

    // Calculate spacing width
    float totalSpacing = (visibleCount - 1) * widget->layoutProps.spacing;
    float remainingWidth = availableWidth - fixedWidth - totalSpacing;

    // First pass: assign sizes
    std::vector<float> childWidths;
    for (auto& child : widget->children) {
        if (!child->visible) {
            childWidths.push_back(0.0f);
            continue;
        }

        float childWidth;
        if (child->layoutProps.flex > 0.0f && totalFlex > 0.0f) {
            childWidth = (child->layoutProps.flex / totalFlex) * remainingWidth;
        } else {
            auto childMeasure = measure(child.get());
            childWidth = childMeasure.preferredWidth;
        }

        childWidths.push_back(childWidth);
    }

    // Second pass: position children
    float offsetX = widget->layoutProps.getLeftPadding();

    for (size_t i = 0; i < widget->children.size(); i++) {
        auto& child = widget->children[i];
        if (!child->visible) continue;

        float childWidth = childWidths[i];
        float childHeight;

        // Handle alignment
        if (widget->layoutProps.align == Alignment::Stretch) {
            childHeight = availableHeight;
        } else {
            auto childMeasure = measure(child.get());
            childHeight = childMeasure.preferredHeight;
        }

        // Position based on alignment
        float childY = widget->layoutProps.getTopPadding();
        switch (widget->layoutProps.align) {
            case Alignment::Center:
                childY += (availableHeight - childHeight) * 0.5f;
                break;
            case Alignment::End:
                childY += availableHeight - childHeight;
                break;
            default:
                break;
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

        // Handle alignment
        if (widget->layoutProps.align == Alignment::Stretch) {
            childWidth = availableWidth;
            childHeight = availableHeight;
        } else {
            auto childMeasure = measure(child.get());
            childWidth = childMeasure.preferredWidth;
            childHeight = childMeasure.preferredHeight;
        }

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

} // namespace grove
