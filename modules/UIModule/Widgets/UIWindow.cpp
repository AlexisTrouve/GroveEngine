#include <grove/IDataNode.h>
#include "UIWindow.h"
#include "../Core/UIContext.h"
#include "../Core/UILayout.h"   // responsive content: lay out children against the content box on resize
#include "../Rendering/UIRenderer.h"

namespace grove {

namespace {
// The window's INNER box: the area inside the 9-slice border art.
//
// WHY: without a frame this IS the window box, unchanged. WITH one, every piece of chrome (title bar,
// title, close button, resize grip, content) must sit INSIDE the border instead of on top of it — a
// title bar drawn across the frame's top edge and corners is the visual clash this lifts. The 9-slice
// draws its corners at their NATIVE source size, so the on-screen border thickness is exactly the
// margin values; insetting by them lands the chrome flush against the inside of the art.
//
// Everything reads its geometry through here, so the drawn chrome and the HIT-TEST can't drift apart
// (closeRect below feeds both) — the bug you would otherwise ship is a close button you can see but
// not click.
void innerRect(const UIWindow& w, float& x, float& y, float& iw, float& ih) {
    if (!w.frame.active()) { x = w.absX; y = w.absY; iw = w.width; ih = w.height; return; }
    x  = w.absX + w.frame.left;
    y  = w.absY + w.frame.top;
    iw = w.width  - w.frame.left - w.frame.right;
    ih = w.height - w.frame.top  - w.frame.bottom;
    if (iw < 0.0f) iw = 0.0f;
    if (ih < 0.0f) ih = 0.0f;
}

// The close-button rect (screen px): a square inset into the right end of the title bar.
void closeRect(const UIWindow& w, float& x, float& y, float& cw, float& ch) {
    float ix, iy, iw, ih; innerRect(w, ix, iy, iw, ih);
    cw = ch = w.closeButtonSize;
    x = ix + iw - w.closeButtonSize - w.padding;
    y = iy + (w.titleBarHeight - w.closeButtonSize) * 0.5f;
}

// The resize grip rect (screen px): a square at the bottom-right corner.
void gripRect(const UIWindow& w, float& x, float& y, float& gw, float& gh) {
    float ix, iy, iw, ih; innerRect(w, ix, iy, iw, ih);
    gw = gh = w.resizeGripSize;
    x = ix + iw - w.resizeGripSize;
    y = iy + ih - w.resizeGripSize;
}
} // namespace

void UIWindow::contentRect(float& outX, float& outY, float& outW, float& outH) const {
    // Everything below the title bar, within the inner box (so children stay off the border art too).
    float ix, iy, iw, ih; innerRect(*this, ix, iy, iw, ih);
    outX = ix;
    outY = iy + titleBarHeight;
    outW = iw;
    outH = ih - titleBarHeight;
    if (outH < 0.0f) outH = 0.0f;
}

void UIWindow::hitClipRect(float& outX, float& outY, float& outW, float& outH) const {
    contentRect(outX, outY, outW, outH);   // children are clipped to the content area
}

bool UIWindow::pointInWindow(float x, float y) const {
    return x >= absX && x <= absX + width && y >= absY && y <= absY + height;
}

bool UIWindow::pointInTitleBar(float x, float y) const {
    // Must track the DRAWN bar (innerRect), not the outer box: with a frame the bar is inset, and a
    // drag zone that stayed full-width would let you grab the border art where no bar is visible —
    // and miss the bar's own left edge. Same reason closeRect/gripRect go through innerRect.
    float ix, iy, iw, ih; innerRect(*this, ix, iy, iw, ih);
    return x >= ix && x <= ix + iw && y >= iy && y <= iy + titleBarHeight;
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
    if (frame.active()) {
        // A window has no hover/press states, so the frame is drawn at its authored colours (WHITE tint = the
        // art as-is), NOT tinted by the dark bgColor (which would crush a coloured frame to near-black). The
        // frame art carries the whole window look (border + translucent glass); bgColor is used only for the
        // non-frame solid-fill path below.
        frame.emit(renderer, m_frameId, absX, absY, width, height, 0xFFFFFFFFu, renderer.nextLayer());
        renderer.updateRect(m_renderId, 0, 0, 0, 0, 0, renderer.nextLayer());        // solid bg idle
    } else {
        renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, renderer.nextLayer());
        UIFrame::collapse(renderer, m_frameId, renderer.nextLayer());                // frame idle
    }
    // Chrome sits in the INNER box (see innerRect): flush inside the border art when framed, exactly
    // where it always was when not.
    float ix, iy, iw, ih; innerRect(*this, ix, iy, iw, ih);
    renderer.updateRect(m_titleBarId, ix, iy, iw, titleBarHeight, titleBarColor, renderer.nextLayer());

    // Title text, vertically centered in the bar.
    renderer.updateText(m_titleTextId, ix + padding, iy + (titleBarHeight - fontSize) * 0.5f,
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


// Fenetre applicative (tranche 3b). Les enfants sont ajoutes par le loader et rendus dans
// la zone de contenu decoupee, sous la barre de titre.
std::unique_ptr<UIWidget> UIWindow::fromNode(const IDataNode& node) {
    auto win = std::make_unique<UIWindow>();
    win->title = node.getString("title", "");
    win->titleBarHeight = static_cast<float>(node.getDouble("titleBarHeight", 28.0));
    win->closable = node.getBool("closable", true);
    win->draggable = node.getBool("draggable", true);

    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* style = mutableNode.getChildReadOnly("style")) {
        auto hexColor = [](IDataNode* s, const char* key, uint32_t def) -> uint32_t {
            std::string v = s->getString(key, "");
            if (v.size() >= 2 && (v.substr(0, 2) == "0x" || v.substr(0, 2) == "0X")) {
                return static_cast<uint32_t>(std::stoul(v, nullptr, 16));
            }
            return def;
        };
        win->bgColor = hexColor(style, "bgColor", win->bgColor);
        win->titleBarColor = hexColor(style, "titleBarColor", win->titleBarColor);
        win->titleColor = hexColor(style, "titleColor", win->titleColor);
        win->closeColor = hexColor(style, "closeColor", win->closeColor);
        win->fontSize = static_cast<float>(style->getDouble("fontSize", win->fontSize));
    }

    // 9-slice FRAME (optional `frame` block): a composed border texture giving the window a continuous,
    // crisp border at any size (replaces the solid bg). `inset` sets all four margins; per-side overrides;
    // `asset` = streamed border art id, `srcW/srcH` its native px dims. Absent -> the solid-bg look.
    if (auto* frame = mutableNode.getChildReadOnly("frame")) win->frame.parse(*frame);
    return win;
}

} // namespace grove
