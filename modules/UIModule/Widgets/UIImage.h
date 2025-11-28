#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Image widget for displaying textures
 *
 * Displays a texture by texture ID or path.
 * Supports tinting, scaling modes, and UV coordinates.
 */
class UIImage : public UIWidget {
public:
    UIImage() = default;
    ~UIImage() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "image"; }

    // Image properties
    int textureId = 0;           // Texture ID (0 = white texture)
    std::string texturePath;     // Path to texture file (alternative to ID)
    uint32_t tintColor = 0xFFFFFFFF;  // RGBA tint (white = no tint)

    // UV coordinates (for sprite sheets)
    float uvX = 0.0f;
    float uvY = 0.0f;
    float uvWidth = 1.0f;
    float uvHeight = 1.0f;

    // Scaling mode
    enum class ScaleMode {
        Stretch,    // Stretch to fill widget bounds
        Fit,        // Fit inside bounds (maintain aspect ratio)
        Fill,       // Fill bounds (may crop, maintain aspect ratio)
        None        // No scaling (1:1 pixel mapping)
    };

    ScaleMode scaleMode = ScaleMode::Stretch;
};

} // namespace grove
