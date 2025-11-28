#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Slider widget for numeric value input
 *
 * Draggable slider for selecting a value within a range.
 * Supports horizontal and vertical orientation.
 */
class UISlider : public UIWidget {
public:
    UISlider() = default;
    ~UISlider() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "slider"; }

    /**
     * @brief Check if a point is inside this slider
     */
    bool containsPoint(float px, float py) const;

    /**
     * @brief Handle mouse button event
     */
    bool onMouseButton(int button, bool pressed, float x, float y);

    /**
     * @brief Handle mouse drag
     */
    void onMouseDrag(float x, float y);

    /**
     * @brief Set value (clamped to min/max)
     */
    void setValue(float newValue);

    /**
     * @brief Get current value
     */
    float getValue() const { return value; }

    // Slider properties
    float minValue = 0.0f;
    float maxValue = 100.0f;
    float value = 50.0f;
    float step = 0.0f;  // 0 = continuous, >0 = snap to steps

    bool horizontal = true;  // true = horizontal, false = vertical
    std::string onChange;    // Action to publish when value changes

    // Style
    uint32_t trackColor = 0x34495eFF;
    uint32_t fillColor = 0x3498dbFF;
    uint32_t handleColor = 0xecf0f1FF;
    float handleSize = 16.0f;

    // State
    bool isDragging = false;
    bool isHovered = false;

private:
    /**
     * @brief Calculate handle position from value
     */
    void calculateHandlePosition(float& handleX, float& handleY) const;

    /**
     * @brief Calculate value from mouse position
     */
    float calculateValueFromPosition(float x, float y) const;
};

} // namespace grove
