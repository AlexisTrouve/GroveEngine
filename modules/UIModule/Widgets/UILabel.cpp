#include "UILabel.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"

namespace grove {

void UILabel::update(UIContext& ctx, float deltaTime) {
    // Labels are static, no update needed
    // Future: could support animated text or data binding
}

void UILabel::render(UIRenderer& renderer) {
    if (text.empty()) return;

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
    renderer.updateText(m_renderId, absX, absY, text, fontSize, color, layer);
}

} // namespace grove
