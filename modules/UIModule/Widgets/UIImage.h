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
    std::string assetId;         // Streamed asset id (string) — wins over textureId, resolved by AssetManager
    uint32_t tintColor = 0xFFFFFFFF;  // RGBA tint (white = no tint)

    // Data-binding: `asset` (streamed id, wins) + `texture`/`textureId` (numeric). Other props -> base.
    void applyBoundProp(const std::string& prop, const std::string& s, double n, bool b) override {
        if (prop == "asset") assetId = s;
        else if (prop == "texture" || prop == "textureId") textureId = static_cast<int>(n);
        else UIWidget::applyBoundProp(prop, s, n, b);
    }

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
