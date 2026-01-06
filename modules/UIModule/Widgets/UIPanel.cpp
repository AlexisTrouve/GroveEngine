#include "UIPanel.h"
#include "../Core/UIContext.h"
#include "../Core/UILayout.h"
#include "../Rendering/UIRenderer.h"
#include <spdlog/spdlog.h>

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
    // Register with renderer on first render
    if (!m_registered) {
        m_renderId = renderer.registerEntry();
        m_registered = true;
        // Set destroy callback to unregister
        setDestroyCallback([&renderer](uint32_t id) {
            renderer.unregisterEntry(id);
        });
    }

    // Retained mode: only publish if changed
    int layer = renderer.nextLayer();
    renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, layer);

    // Render children on top
    renderChildren(renderer);
}

} // namespace grove
