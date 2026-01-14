#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Progress bar widget for displaying progress
 *
 * Read-only widget that shows a progress value as a filled bar.
 * Supports horizontal and vertical orientation.
 */
class UIProgressBar : public UIWidget {
public:
    UIProgressBar() = default;
    ~UIProgressBar() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "progressbar"; }

    /**
     * @brief Set progress value (clamped to 0-1)
     */
    void setProgress(float newProgress);

    /**
     * @brief Get current progress (0-1)
     */
    float getProgress() const { return progress; }

    // Progress bar properties
    float progress = 0.5f;  // 0.0 to 1.0
    bool horizontal = true;  // true = horizontal, false = vertical
    bool showText = false;   // Show percentage text

    // Style
    uint32_t bgColor = 0x34495eFF;
    uint32_t fillColor = 0x2ecc71FF;
    uint32_t textColor = 0xFFFFFFFF;
    float fontSize = 14.0f;

    // Texture support
    int bgTextureId = 0;               // Background texture ID (0 = solid color)
    bool useBgTexture = false;         // Use texture for background
    uint32_t bgTintColor = 0xFFFFFFFF; // Tint for background texture

    int fillTextureId = 0;             // Fill texture ID (0 = solid color)
    bool useFillTexture = false;       // Use texture for fill
    uint32_t fillTintColor = 0xFFFFFFFF; // Tint for fill texture

private:
    // Retained mode render IDs
    uint32_t m_fillRenderId = 0;  // Separate ID for fill bar element
    uint32_t m_textRenderId = 0;  // Separate ID for text element
};

} // namespace grove
