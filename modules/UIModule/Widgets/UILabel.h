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

    // Data-binding: the label's text is the bindable prop ("text":"{{...}}"); colour falls through to base.
    void applyBoundProp(const std::string& prop, const std::string& s, double n, bool b) override {
        if (prop == "text") text = s;
        else if (prop == "bold") bold = b;
        else UIWidget::applyBoundProp(prop, s, n, b);
    }

    // Text content
    std::string text;

    // Style properties
    uint32_t color = 0xFFFFFFFF;  // RGBA
    float fontSize = 16.0f;
    std::string fontId;  // For future font selection
    // Text handling: horizontal alignment (0 left / 1 center / 2 right) + synthetic bold. `align` "center"/
    // "right" anchors the text on the label's box (x = left edge for left, x + width/2 for center, x + width
    // for right — so a centered label needs a `width`). Bold thickens the single-weight bitmap font.
    int align = 0;
    bool bold = false;
};

} // namespace grove
