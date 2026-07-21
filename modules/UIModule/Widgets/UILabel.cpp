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

    // Anchor x by alignment: left = the label's left edge; center = its horizontal middle; right = its right
    // edge (TextPass interprets x per align). Center/right need a `width` to anchor against (0 -> falls back to
    // the left edge). Retained mode: only publishes on change (align/bold included in the change check).
    float ax = absX;
    if (align == 1)      ax = absX + width * 0.5f;
    else if (align == 2) ax = absX + width;
    int layer = renderer.nextLayer();
    renderer.updateText(m_renderId, ax, absY, text, fontSize, color, layer, align, bold);
}

} // namespace grove
