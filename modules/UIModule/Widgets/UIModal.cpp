#include <grove/IDataNode.h>
#include "UIModal.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"

namespace grove {

void UIModal::dialogRect(float& outX, float& outY, float& outW, float& outH) const {
    outW = dialogWidth;
    outH = dialogHeight;
    outX = absX + (width - dialogWidth) * 0.5f;    // centered in the (full-screen) backdrop
    outY = absY + (height - dialogHeight) * 0.5f;
}

void UIModal::hitClipRect(float& outX, float& outY, float& outW, float& outH) const {
    dialogRect(outX, outY, outW, outH);            // content children are clipped to the dialog
}

bool UIModal::pointInBounds(float x, float y) const {
    return x >= absX && x <= absX + width && y >= absY && y <= absY + height;
}

bool UIModal::pointInDialog(float x, float y) const {
    float dx, dy, dw, dh;
    dialogRect(dx, dy, dw, dh);
    return x >= dx && x <= dx + dw && y >= dy && y <= dy + dh;
}

void UIModal::update(UIContext& ctx, float deltaTime) {
    if (!visible) return;

    // The backdrop fills the whole viewport (so the hit-test absorbs everything = focus-trap).
    x = 0.0f; y = 0.0f;
    width = ctx.screenWidth;
    height = ctx.screenHeight;
    computeAbsolutePosition();

    // Content children are placed relative to the (centered) dialog's top-left, then clipped to it.
    float dx, dy, dw, dh;
    dialogRect(dx, dy, dw, dh);
    (void)dw; (void)dh;
    for (auto& child : children) {
        if (!child->visible) continue;
        child->absX = dx + child->x;
        child->absY = dy + child->y;
        for (auto& grandChild : child->children) {
            grandChild->computeAbsolutePosition();
        }
        child->update(ctx, deltaTime);
    }
}

void UIModal::render(UIRenderer& renderer) {
    if (!visible) return;

    if (!m_registered) {
        m_renderId = renderer.registerEntry();      // dim backdrop
        m_dialogBgId = renderer.registerEntry();    // dialog box background
        m_registered = true;
        setDestroyCallback([&renderer, dlg = m_dialogBgId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(dlg);
        });
    }

    // Full-screen dim, then the centered dialog box, then content clipped to the dialog.
    renderer.updateRect(m_renderId, absX, absY, width, height, dimColor, renderer.nextLayer());
    float dx, dy, dw, dh;
    dialogRect(dx, dy, dw, dh);
    // The dialog box: a composed frame when authored, else the flat rect. NOT the backdrop above —
    // stretching border art across a full-screen dim veil would be meaningless.
    if (frame.active()) {
        if (!m_frameRegistered) {
            m_frameId = renderer.registerEntry();
            m_frameRegistered = true;
        }
        frame.emit(renderer, m_frameId, dx, dy, dw, dh, dialogColor, renderer.nextLayer());
        renderer.updateRect(m_dialogBgId, 0, 0, 0, 0, 0, renderer.nextLayer());   // flat dialog idle
    } else {
        renderer.updateRect(m_dialogBgId, dx, dy, dw, dh, dialogColor, renderer.nextLayer());
        if (m_frameRegistered) UIFrame::collapse(renderer, m_frameId, renderer.nextLayer());
    }

    renderer.pushClip(dx, dy, dw, dh);
    renderChildren(renderer);
    renderer.popClip();
}

void UIModal::releaseRenderEntries(UIRenderer& renderer) {
    if (m_dialogBgId != 0) { renderer.unregisterEntry(m_dialogBgId); m_dialogBgId = 0; }
    UIWidget::releaseRenderEntries(renderer);   // drops m_renderId + recurses to children
}


// Dialogue centre + piege de focus assombri (tranche 5a). L'etat ouvert = le drapeau `visible`
// (lu par parseCommonProperties) ; une modale fermee pose "visible": false.
std::unique_ptr<UIWidget> UIModal::fromNode(const IDataNode& node) {
    auto modal = std::make_unique<UIModal>();
    modal->dialogWidth = static_cast<float>(node.getDouble("dialogWidth", 400.0));
    modal->dialogHeight = static_cast<float>(node.getDouble("dialogHeight", 250.0));

    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* style = mutableNode.getChildReadOnly("style")) {
        auto hexColor = [](IDataNode* s, const char* key, uint32_t def) -> uint32_t {
            std::string v = s->getString(key, "");
            if (v.size() >= 2 && (v.substr(0, 2) == "0x" || v.substr(0, 2) == "0X")) {
                return static_cast<uint32_t>(std::stoul(v, nullptr, 16));
            }
            return def;
        };
        modal->dimColor = hexColor(style, "dimColor", modal->dimColor);
        modal->dialogColor = hexColor(style, "dialogColor", modal->dialogColor);
    }
    // 9-slice FRAME (optional `frame` block) — see UIFrame.
    if (auto* f = mutableNode.getChildReadOnly("frame")) modal->frame.parse(*f);

    return modal;
}

} // namespace grove
