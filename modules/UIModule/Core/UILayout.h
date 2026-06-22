#pragma once

#include <string>
#include <vector>

namespace grove {

class UIWidget;

/**
 * @brief Layout mode for widget positioning
 */
enum class LayoutMode {
    Vertical,   // Stack children vertically
    Horizontal, // Stack children horizontally
    Stack,      // Overlay children (superposed)
    Absolute,   // No automatic layout (manual positioning)
    Grid        // N-column uniform grid; cells fill the width (so it reflows on resize)
};

/**
 * @brief Alignment along main axis
 */
enum class Alignment {
    Start,   // Top/Left
    Center,  // Center
    End,     // Bottom/Right
    Stretch  // Fill available space
};

/**
 * @brief Justification along cross axis
 */
enum class Justification {
    Start,   // Top/Left
    Center,  // Center
    End,     // Bottom/Right
    SpaceBetween, // Space between items
    SpaceAround   // Space around items
};

/**
 * @brief Layout properties for a widget
 */
struct LayoutProperties {
    LayoutMode mode = LayoutMode::Absolute;

    // Spacing
    float padding = 0.0f;        // Inner padding (all sides)
    float paddingTop = 0.0f;
    float paddingRight = 0.0f;
    float paddingBottom = 0.0f;
    float paddingLeft = 0.0f;

    float margin = 0.0f;         // Outer margin (all sides)
    float marginTop = 0.0f;
    float marginRight = 0.0f;
    float marginBottom = 0.0f;
    float marginLeft = 0.0f;

    float spacing = 0.0f;        // Space between children

    // Alignment and justification
    Alignment align = Alignment::Start;
    Justification justify = Justification::Start;

    // Sizing
    float minWidth = 0.0f;
    float minHeight = 0.0f;
    float maxWidth = -1.0f;  // -1 means no limit
    float maxHeight = -1.0f;

    float flex = 0.0f;  // Flex grow factor (0 = fixed size)

    // Grid (LayoutMode::Grid). columns = number of columns; cells share the content width minus
    // gaps (gap = `spacing`, reused). rowHeight = cell height; 0 = square cells (height = width).
    int columns = 1;
    float rowHeight = 0.0f;

    /**
     * @brief Helper to get total horizontal padding
     */
    float getTotalPaddingX() const {
        return (paddingLeft > 0 ? paddingLeft : padding) +
               (paddingRight > 0 ? paddingRight : padding);
    }

    /**
     * @brief Helper to get total vertical padding
     */
    float getTotalPaddingY() const {
        return (paddingTop > 0 ? paddingTop : padding) +
               (paddingBottom > 0 ? paddingBottom : padding);
    }

    /**
     * @brief Helper to get left padding
     */
    float getLeftPadding() const {
        return paddingLeft > 0 ? paddingLeft : padding;
    }

    /**
     * @brief Helper to get top padding
     */
    float getTopPadding() const {
        return paddingTop > 0 ? paddingTop : padding;
    }
};

/**
 * @brief Anchor — pins an absolutely-positioned widget to a point of its parent content box.
 *
 * WHY: so a HUD element stays glued to a corner/edge when the window (parent) resizes — the
 *      position is derived from the parent box each layout pass, not a frozen x/y. "Fill" is
 *      NOT here: that's widthPercent/heightPercent=1 (slice 1.1); Anchor is positional only.
 * SCOPE: resolved for children of an ABSOLUTE-mode parent (flow modes position by the flow).
 *        Anchor::None = legacy behavior (use the explicit x/y).
 */
enum class Anchor {
    None,
    TopLeft,    Top,    TopRight,
    Left,       Center, Right,
    BottomLeft, Bottom, BottomRight
};

/**
 * @brief Result of layout measurement pass
 */
struct LayoutMeasurement {
    float preferredWidth = 0.0f;
    float preferredHeight = 0.0f;
};

/**
 * @brief A resolved top-left position (relative to the parent origin).
 */
struct AnchorPos {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * @brief A resolved rectangle (position relative to the grid content origin + cell size).
 */
struct CellRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

/**
 * @brief Layout engine - handles automatic positioning of widgets
 */
class UILayout {
public:
    /**
     * @brief Measure the preferred size of a widget and its children (bottom-up)
     * @param widget Widget to measure
     * @return Measurement result
     */
    static LayoutMeasurement measure(UIWidget* widget);

    /**
     * @brief Layout a widget and its children (top-down)
     * @param widget Widget to layout
     * @param availableWidth Available width for the widget
     * @param availableHeight Available height for the widget
     */
    static void layout(UIWidget* widget, float availableWidth, float availableHeight);

    /**
     * @brief Resolve a widget's top-left position so its anchor point sits at the matching point
     *        of the parent content box, then apply the offset. Pure — oracle-tested.
     * @param anchor       Which point of the box the widget pins to
     * @param boxX,boxY    Content box origin (relative to the parent origin — includes padding)
     * @param boxW,boxH    Content box size
     * @param w,h          The widget's own size
     * @param offX,offY    Pixel nudge applied after anchoring (+x right, +y down)
     * @return Top-left {x,y} relative to the parent origin. Anchor::None -> {offX, offY} pass-through.
     */
    static AnchorPos resolveAnchor(Anchor anchor,
                                   float boxX, float boxY, float boxW, float boxH,
                                   float w, float h, float offX, float offY);

    /**
     * @brief Position+size of cell `index` in a grid of `cols` columns. Pure — oracle-tested.
     *        Row-major: col = index % cols, row = index / cols. Returns {x,y} relative to the grid
     *        content origin (caller adds padding), plus the cell size {cellW, cellH}.
     */
    static CellRect gridCellRect(int index, int cols, float cellW, float cellH, float gap);

private:
    /**
     * @brief Measure children for vertical layout
     */
    static LayoutMeasurement measureVertical(UIWidget* widget);

    /**
     * @brief Measure children for horizontal layout
     */
    static LayoutMeasurement measureHorizontal(UIWidget* widget);

    /**
     * @brief Measure children for stack layout
     */
    static LayoutMeasurement measureStack(UIWidget* widget);

    /**
     * @brief Measure children for grid layout
     */
    static LayoutMeasurement measureGrid(UIWidget* widget);

    /**
     * @brief Layout children vertically
     */
    static void layoutVertical(UIWidget* widget, float availableWidth, float availableHeight);

    /**
     * @brief Layout children horizontally
     */
    static void layoutHorizontal(UIWidget* widget, float availableWidth, float availableHeight);

    /**
     * @brief Layout children in stack mode (overlay)
     */
    static void layoutStack(UIWidget* widget, float availableWidth, float availableHeight);

    /**
     * @brief Layout children in grid mode (N columns, cells fill the content width)
     */
    static void layoutGrid(UIWidget* widget, float availableWidth, float availableHeight);

    /**
     * @brief Clamp size to min/max constraints
     */
    static float clampSize(float size, float minSize, float maxSize);
};

} // namespace grove
