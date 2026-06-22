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
    renderer.updateRect(m_dialogBgId, dx, dy, dw, dh, dialogColor, renderer.nextLayer());

    renderer.pushClip(dx, dy, dw, dh);
    renderChildren(renderer);
    renderer.popClip();
}

void UIModal::releaseRenderEntries(UIRenderer& renderer) {
    if (m_dialogBgId != 0) { renderer.unregisterEntry(m_dialogBgId); m_dialogBgId = 0; }
    UIWidget::releaseRenderEntries(renderer);   // drops m_renderId + recurses to children
}

} // namespace grove
