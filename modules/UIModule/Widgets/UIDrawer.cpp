#include "UIDrawer.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>

namespace grove {

bool UIDrawer::pointInBounds(float x, float y) const {
    return x >= absX && x <= absX + width && y >= absY && y <= absY + height;
}

void UIDrawer::update(UIContext& ctx, float deltaTime) {
    // Advance the slide toward the target. m_t is linear time 0..1; the POSITION below eases it
    // (smoothstep) so the drawer accelerates out and settles in.
    const float step = (slideDuration > 0.0f) ? (deltaTime / slideDuration) : 1.0f;
    m_t = std::clamp(m_t + (m_targetOpen ? step : -step), 0.0f, 1.0f);

    const float sw = ctx.screenWidth;
    const float sh = ctx.screenHeight;
    const float e = m_t * m_t * (3.0f - 2.0f * m_t);   // smoothstep(m_t)

    // Fill the viewport along the edge; slide perpendicular to it between off-edge and flush.
    switch (edge) {
        case Edge::Left:   width = openExtent; height = sh; y = 0.0f; x = -openExtent * (1.0f - e); break;
        case Edge::Right:  width = openExtent; height = sh; y = 0.0f; x = sw - openExtent * e;       break;
        case Edge::Top:    width = sw; height = openExtent; x = 0.0f; y = -openExtent * (1.0f - e); break;
        case Edge::Bottom: width = sw; height = openExtent; x = 0.0f; y = sh - openExtent * e;       break;
    }
    computeAbsolutePosition();

    // Stay "visible" in the tree (so render() is called every frame and can purge when it reaches
    // the closed state); its on-screen presence is governed by m_t, not the visible flag.
    if (isFullyClosed()) return;

    // Position content children at the drawer's top-left so they slide + clip with it.
    for (auto& child : children) {
        if (!child->visible) continue;
        child->absX = absX + child->x;
        child->absY = absY + child->y;
        for (auto& grandChild : child->children) {
            grandChild->computeAbsolutePosition();
        }
        child->update(ctx, deltaTime);
    }
}

void UIDrawer::render(UIRenderer& renderer) {
    // Fully closed -> off screen: purge our entries once (no lingering off-screen rects) and skip.
    if (isFullyClosed()) {
        if (!m_purged) { releaseRenderEntries(renderer); m_purged = true; }
        return;
    }
    m_purged = false;

    if (!m_registered) {
        m_renderId = renderer.registerEntry();
        m_registered = true;
        setDestroyCallback([&renderer](uint32_t id) { renderer.unregisterEntry(id); });
    }

    // Drawer background, then content clipped to the (sliding) drawer rect.
    renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, renderer.nextLayer());
    renderer.pushClip(absX, absY, width, height);
    renderChildren(renderer);
    renderer.popClip();
}

} // namespace grove
