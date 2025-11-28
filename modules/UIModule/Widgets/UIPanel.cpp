#include "UIPanel.h"
#include "../Core/UIContext.h"
#include "../Core/UILayout.h"
#include "../Rendering/UIRenderer.h"

namespace grove {

void UIPanel::update(UIContext& ctx, float deltaTime) {
    // Apply layout if this panel has a non-absolute layout mode
    if (layoutProps.mode != LayoutMode::Absolute) {
        // Measure and layout children
        UILayout::measure(this);
        UILayout::layout(this, width, height);
    }

    // Update children
    updateChildren(ctx, deltaTime);
}

void UIPanel::render(UIRenderer& renderer) {
    // Render background rectangle
    renderer.drawRect(absX, absY, width, height, bgColor);

    // Render children on top
    renderChildren(renderer);
}

} // namespace grove
