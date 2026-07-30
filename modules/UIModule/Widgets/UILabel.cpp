#include <grove/IDataNode.h>
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


std::unique_ptr<UIWidget> UILabel::fromNode(const IDataNode& node) {
    auto label = std::make_unique<UILabel>();
    label->text = node.getString("text", "");

    // Parse style (const_cast safe for read-only operations)
    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* style = mutableNode.getChildReadOnly("style")) {
        std::string colorStr = style->getString("color", "0xFFFFFFFF");
        if (colorStr.size() >= 2 && (colorStr.substr(0, 2) == "0x" || colorStr.substr(0, 2) == "0X")) {
            label->color = static_cast<uint32_t>(std::stoul(colorStr, nullptr, 16));
        }
        label->fontSize = static_cast<float>(style->getDouble("fontSize", 16.0));
        // Text handling: align "left"/"center"/"right" -> 0/1/2 ; bold thickens the font. Center/right
        // anchor on the label's `width`, so give a centered label a width.
        const std::string al = style->getString("align", "");
        label->align = (al == "center") ? 1 : (al == "right") ? 2 : 0;
        label->bold = style->getBool("bold", false);
    }

    return label;
}

} // namespace grove
