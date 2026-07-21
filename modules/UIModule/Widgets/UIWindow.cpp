#include "UIWindow.h"
#include "../Core/UIContext.h"
#include "../Core/UILayout.h"   // responsive content: lay out children against the content box on resize
#include "../Rendering/UIRenderer.h"

namespace grove {

namespace {
// The close-button rect (screen px): a square inset into the right end of the title bar.
void closeRect(const UIWindow& w, float& x, float& y, float& cw, float& ch) {
    cw = ch = w.closeButtonSize;
    x = w.absX + w.width - w.closeButtonSize - w.padding;
    y = w.absY + (w.titleBarHeight - w.closeButtonSize) * 0.5f;
}

// The resize grip rect (screen px): a square at the bottom-right corner.
void gripRect(const UIWindow& w, float& x, float& y, float& gw, float& gh) {
    gw = gh = w.resizeGripSize;
    x = w.absX + w.width - w.resizeGripSize;
    y = w.absY + w.height - w.resizeGripSize;
}
} // namespace

void UIWindow::contentRect(float& outX, float& outY, float& outW, float& outH) const {
    // Everything below the title bar.
    outX = absX;
    outY = absY + titleBarHeight;
    outW = width;
    outH = height - titleBarHeight;
}

void UIWindow::hitClipRect(float& outX, float& outY, float& outW, float& outH) const {
    contentRect(outX, outY, outW, outH);   // children are clipped to the content area
}

bool UIWindow::pointInWindow(float x, float y) const {
    return x >= absX && x <= absX + width && y >= absY && y <= absY + height;
}

bool UIWindow::pointInTitleBar(float x, float y) const {
    return x >= absX && x <= absX + width && y >= absY && y <= absY + titleBarHeight;
}

bool UIWindow::pointInCloseButton(float x, float y) const {
    if (!closable) return false;
    float cx, cy, cw, ch;
    closeRect(*this, cx, cy, cw, ch);
    return x >= cx && x <= cx + cw && y >= cy && y <= cy + ch;
}

bool UIWindow::pointInResizeGrip(float x, float y) const {
    if (!resizable) return false;
    float gx, gy, gw, gh;
    gripRect(*this, gx, gy, gw, gh);
    return x >= gx && x <= gx + gw && y >= gy && y <= gy + gh;
}

void UIWindow::update(UIContext& ctx, float deltaTime) {
    if (!visible) return;

    // Position content children relative to the CONTENT origin (below the title bar), persistently
    // in absX/absY so render AND hit-test agree — same approach as UIScrollPanel. (Interaction —
    // drag / close / raise — is driven centrally by UIModule in slice 3b-2.)
    float rx, ry, rw, rh;
    contentRect(rx, ry, rw, rh);

    // SMART RESIZE: lay the content out against the content box (rw x rh) every frame — this resolves
    // relative `%` sizes, 9-point anchors and flow modes, so the content REFLOWS when the window is resized.
    // (Absolute children with no anchor/percent keep their explicit x/y, so static windows are unchanged.)
    // NOTE: UILayout::layout(w, aw, ah) sets w->width/height = the available box; that's meant for a parent
    // sizing itself, but here `this` is the window and must KEEP its own size — so save + restore it.
    const float selfW = width, selfH = height;
    UILayout::measure(this);
    UILayout::layout(this, rw, rh);
    width = selfW; height = selfH;

    for (auto& child : children) {
        if (!child->visible) continue;
        child->absX = rx + child->x;   // offset the laid-out relative pos to the content origin
        child->absY = ry + child->y;
        for (auto& grandChild : child->children) {
            grandChild->computeAbsolutePosition();
        }
        child->update(ctx, deltaTime);
    }
}

void UIWindow::render(UIRenderer& renderer) {
    if (!visible) return;

    if (!m_registered) {
        m_renderId   = renderer.registerEntry();   // window background
        m_titleBarId = renderer.registerEntry();   // title bar strip
        m_titleTextId = renderer.registerEntry();  // title text
        m_closeId    = renderer.registerEntry();   // close button
        m_resizeGripId = renderer.registerEntry(); // resize grip (bottom-right)
        m_frameId    = renderer.registerEntry();   // 9-slice chrome (used only when frameAsset is set)
        m_registered = true;
        setDestroyCallback([&renderer, tb = m_titleBarId, tt = m_titleTextId, cl = m_closeId,
                            gr = m_resizeGripId, fr = m_frameId](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(tb);
            renderer.unregisterEntry(tt);
            renderer.unregisterEntry(cl);
            renderer.unregisterEntry(gr);
            renderer.unregisterEntry(fr);
        });
    }

    // Window background: a 9-slice composed frame (continuous border) when frameAsset is set, else a solid
    // rect. The two are mutually exclusive — the unused one is collapsed to zero so they never co-draw. The
    // title bar + chrome always draw ON TOP of whichever background was used.
    const bool useFrame = !frameAsset.empty() && frameSrcW > 0.0f && frameSrcH > 0.0f;
    if (useFrame) {
        // A window has no hover/press states, so the frame is drawn at its authored colours (WHITE tint = the
        // art as-is), NOT tinted by the dark bgColor (which would crush a coloured frame to near-black). The
        // frame art carries the whole window look (border + translucent glass); bgColor is used only for the
        // non-frame solid-fill path below.
        renderer.updateNineSlice(m_frameId, absX, absY, width, height, frameAsset, /*textureId=*/0,
                                 frameSrcW, frameSrcH, frameL, frameR, frameT, frameB,
                                 0xFFFFFFFFu, renderer.nextLayer());
        renderer.updateRect(m_renderId, 0, 0, 0, 0, 0, renderer.nextLayer());        // solid bg idle
    } else {
        renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, renderer.nextLayer());
        renderer.updateNineSlice(m_frameId, 0, 0, 0, 0, "", 0, 0, 0, 0, 0, 0, 0, bgColor, renderer.nextLayer()); // frame idle
    }
    renderer.updateRect(m_titleBarId, absX, absY, width, titleBarHeight, titleBarColor, renderer.nextLayer());

    // Title text, vertically centered in the bar.
    renderer.updateText(m_titleTextId, absX + padding, absY + (titleBarHeight - fontSize) * 0.5f,
                        title, fontSize, titleColor, renderer.nextLayer());

    // Close button (or hidden at zero size when not closable).
    int closeLayer = renderer.nextLayer();
    if (closable) {
        float cx, cy, cw, ch;
        closeRect(*this, cx, cy, cw, ch);
        renderer.updateRect(m_closeId, cx, cy, cw, ch, closeColor, closeLayer);
    } else {
        renderer.updateRect(m_closeId, 0, 0, 0, 0, 0, closeLayer);
    }

    // Resize grip (bottom-right), or hidden when not resizable.
    int gripLayer = renderer.nextLayer();
    if (resizable) {
        float gx, gy, gw, gh;
        gripRect(*this, gx, gy, gw, gh);
        renderer.updateRect(m_resizeGripId, gx, gy, gw, gh, titleBarColor, gripLayer);
    } else {
        renderer.updateRect(m_resizeGripId, 0, 0, 0, 0, 0, gripLayer);
    }

    // Content children, clipped to the area below the title bar (the slice-2 clip stack).
    float rx, ry, rw, rh;
    contentRect(rx, ry, rw, rh);
    renderer.pushClip(rx, ry, rw, rh);
    renderChildren(renderer);
    renderer.popClip();
}

void UIWindow::releaseRenderEntries(UIRenderer& renderer) {
    // Drop our EXTRA entries (the base drops m_renderId + recurses to children).
    if (m_titleBarId != 0)   { renderer.unregisterEntry(m_titleBarId);   m_titleBarId = 0; }
    if (m_titleTextId != 0)  { renderer.unregisterEntry(m_titleTextId);  m_titleTextId = 0; }
    if (m_closeId != 0)      { renderer.unregisterEntry(m_closeId);      m_closeId = 0; }
    if (m_resizeGripId != 0) { renderer.unregisterEntry(m_resizeGripId); m_resizeGripId = 0; }
    if (m_frameId != 0)      { renderer.unregisterEntry(m_frameId);      m_frameId = 0; }
    UIWidget::releaseRenderEntries(renderer);
}

} // namespace grove
