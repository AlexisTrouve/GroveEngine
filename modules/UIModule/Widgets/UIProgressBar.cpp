#include "UIProgressBar.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace grove {

void UIProgressBar::update(UIContext& ctx, float deltaTime) {
    // Progress bars are read-only, no interaction
    // Update children
    updateChildren(ctx, deltaTime);
}

void UIProgressBar::render(UIRenderer& renderer) {
    // Render background
    renderer.drawRect(absX, absY, width, height, bgColor);

    // Render fill based on progress
    if (horizontal) {
        float fillWidth = progress * width;
        renderer.drawRect(absX, absY, fillWidth, height, fillColor);
    } else {
        float fillHeight = progress * height;
        renderer.drawRect(absX, absY + height - fillHeight, width, fillHeight, fillColor);
    }

    // Render percentage text if enabled
    if (showText) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << (progress * 100.0f) << "%";
        std::string progressText = oss.str();

        float textX = absX + width * 0.5f;
        float textY = absY + height * 0.5f;
        renderer.drawText(textX, textY, progressText, fontSize, textColor);
    }

    // Render children on top
    renderChildren(renderer);
}

void UIProgressBar::setProgress(float newProgress) {
    progress = std::max(0.0f, std::min(1.0f, newProgress));
}

} // namespace grove
