#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>

namespace grove {

/**
 * @brief Container widget with background color
 *
 * Panel is the basic container widget. It renders a colored rectangle
 * and can contain child widgets.
 */
class UIPanel : public UIWidget {
public:
    UIPanel() = default;
    ~UIPanel() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "panel"; }

    // Style properties
    uint32_t bgColor = 0x333333FF;  // RGBA
    float borderRadius = 0.0f;      // For future use
    float borderWidth = 0.0f;
    uint32_t borderColor = 0x000000FF;

    // Texture support
    int textureId = 0;               // Texture ID (0 = solid color)
    bool useTexture = false;         // Use texture instead of solid color
    uint32_t tintColor = 0xFFFFFFFF; // RGBA tint for texture (white = no tint)
};

} // namespace grove
