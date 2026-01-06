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
    // Register with renderer on first render (need 3 entries: bg + fill + text)
    if (!m_registered) {
        m_renderId = renderer.registerEntry();       // Background track
        m_fillRenderId = renderer.registerEntry();   // Fill bar
        m_textRenderId = renderer.registerEntry();   // Text
        m_registered = true;
        // Set destroy callback to unregister all
        setDestroyCallback([&renderer, fillId = m_fillRenderId, textId = m_textRenderId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(fillId);
            renderer.unregisterEntry(textId);
        });
    }

    // Retained mode: only publish if changed
    int bgLayer = renderer.nextLayer();
    renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, bgLayer);

    // Render fill based on progress
    int fillLayer = renderer.nextLayer();
    if (horizontal) {
        float fillWidth = progress * width;
        renderer.updateRect(m_fillRenderId, absX, absY, fillWidth, height, fillColor, fillLayer);
    } else {
        float fillHeight = progress * height;
        renderer.updateRect(m_fillRenderId, absX, absY + height - fillHeight, width, fillHeight, fillColor, fillLayer);
    }

    // Render percentage text if enabled
    if (showText) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << (progress * 100.0f) << "%";
        std::string progressText = oss.str();

        int textLayer = renderer.nextLayer();
        float textX = absX + width * 0.5f;
        float textY = absY + height * 0.5f;
        renderer.updateText(m_textRenderId, textX, textY, progressText, fontSize, textColor, textLayer);
    }

    // Render children on top
    renderChildren(renderer);
}

void UIProgressBar::setProgress(float newProgress) {
    progress = std::max(0.0f, std::min(1.0f, newProgress));
}

} // namespace grove
