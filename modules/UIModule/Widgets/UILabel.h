#pragma once

#include "../Core/UIWidget.h"
#include <string>
#include <cstdint>

namespace grove {

/**
 * @brief Text display widget
 *
 * Displays static or dynamic text with configurable font size and color.
 */
class UILabel : public UIWidget {
public:
    UILabel() = default;
    ~UILabel() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "label"; }

    // Text content
    std::string text;

    // Style properties
    uint32_t color = 0xFFFFFFFF;  // RGBA
    float fontSize = 16.0f;
    std::string fontId;  // For future font selection
};

} // namespace grove
