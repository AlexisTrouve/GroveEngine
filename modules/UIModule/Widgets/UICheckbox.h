#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Checkbox widget for boolean toggle
 *
 * Clickable checkbox with checked/unchecked states.
 * Optionally displays a label next to the checkbox.
 */
class UICheckbox : public UIWidget {
public:
    UICheckbox() = default;
    ~UICheckbox() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "checkbox"; }

    /**
     * @brief Check if a point is inside this checkbox
     */
    bool containsPoint(float px, float py) const;

    /**
     * @brief Handle mouse button event
     */
    bool onMouseButton(int button, bool pressed, float x, float y);

    /**
     * @brief Toggle checked state
     */
    void toggle();

    // Checkbox properties
    bool checked = false;
    std::string text;            // Label text
    std::string onChange;        // Action to publish when toggled

    // Style
    uint32_t boxColor = 0x34495eFF;
    uint32_t checkColor = 0x2ecc71FF;
    uint32_t textColor = 0xecf0f1FF;
    float boxSize = 24.0f;
    float fontSize = 16.0f;
    float spacing = 8.0f;  // Space between box and text

    // State
    bool isHovered = false;
    bool isPressed = false;
};

} // namespace grove
