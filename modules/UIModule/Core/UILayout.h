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
    Absolute    // No automatic layout (manual positioning)
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
 * @brief Result of layout measurement pass
 */
struct LayoutMeasurement {
    float preferredWidth = 0.0f;
    float preferredHeight = 0.0f;
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
     * @brief Clamp size to min/max constraints
     */
    static float clampSize(float size, float minSize, float maxSize);
};

} // namespace grove
