#include "UITabs.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <algorithm>
#include <string>

namespace grove {

void UITabs::contentRect(float& outX, float& outY, float& outW, float& outH) const {
    outX = absX;
    outY = absY + tabBarHeight;
    outW = width;
    outH = height - tabBarHeight;
}

void UITabs::hitClipRect(float& outX, float& outY, float& outW, float& outH) const {
    contentRect(outX, outY, outW, outH);   // active page is clipped to the content area
}

bool UITabs::pointInBounds(float x, float y) const {
    return x >= absX && x <= absX + width && y >= absY && y <= absY + height;
}

bool UITabs::pointInTabBar(float x, float y) const {
    return x >= absX && x <= absX + width && y >= absY && y <= absY + tabBarHeight;
}

int UITabs::tabAt(float x) const {
    const int n = static_cast<int>(children.size());
    if (n <= 0) return -1;
    const float tabW = width / static_cast<float>(n);
    int i = static_cast<int>((x - absX) / tabW);
    return std::clamp(i, 0, n - 1);
}

void UITabs::setActiveIndex(int i) {
    const int n = static_cast<int>(children.size());
    if (n <= 0) { m_activeIndex = 0; return; }
    m_activeIndex = std::clamp(i, 0, n - 1);
}

void UITabs::update(UIContext& ctx, float deltaTime) {
    if (!visible) return;

    const int n = static_cast<int>(children.size());
    if (m_activeIndex >= n) m_activeIndex = (n > 0) ? n - 1 : 0;

    float rx, ry, rw, rh;
    contentRect(rx, ry, rw, rh);
    (void)rw; (void)rh;

    // Only the active page is visible (so hit-test ignores the others) and updated; it is positioned
    // at the content origin (below the tab bar), persistently in absX/absY — same as window/scrollpanel.
    for (int i = 0; i < n; ++i) {
        auto& child = children[i];
        const bool active = (i == m_activeIndex);
        child->visible = active;
        if (active) {
            child->absX = rx + child->x;
            child->absY = ry + child->y;
            for (auto& grandChild : child->children) {
                grandChild->computeAbsolutePosition();
            }
            child->update(ctx, deltaTime);
        }
    }
}

void UITabs::render(UIRenderer& renderer) {
    if (!visible) return;

    const int n = static_cast<int>(children.size());
    if (!m_entriesRegistered) {
        m_renderId = renderer.registerEntry();    // content background
        m_tabBarBgId = renderer.registerEntry();  // tab bar background
        m_tabRectIds.resize(n);
        m_tabLabelIds.resize(n);
        for (int i = 0; i < n; ++i) {
            m_tabRectIds[i] = renderer.registerEntry();
            m_tabLabelIds[i] = renderer.registerEntry();
        }
        m_entriesRegistered = true;
        m_registered = true;
        // Unregister EVERY entry on destroy (capture the id lists by value).
        setDestroyCallback([&renderer, barBg = m_tabBarBgId,
                            rects = m_tabRectIds, labels = m_tabLabelIds](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(barBg);
            for (uint32_t r : rects) renderer.unregisterEntry(r);
            for (uint32_t l : labels) renderer.unregisterEntry(l);
        });
    }

    // Content background + tab bar background.
    renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, renderer.nextLayer());
    renderer.updateRect(m_tabBarBgId, absX, absY, width, tabBarHeight, inactiveTabColor, renderer.nextLayer());

    // Tab buttons (equal width) + labels; the active tab is highlighted.
    const float tabW = (n > 0) ? width / static_cast<float>(n) : width;
    const int tabLayer = renderer.nextLayer();
    const int labelLayer = renderer.nextLayer();
    for (int i = 0; i < n; ++i) {
        const float tx = absX + i * tabW;
        const uint32_t col = (i == m_activeIndex) ? activeTabColor : inactiveTabColor;
        renderer.updateRect(m_tabRectIds[i], tx, absY, tabW, tabBarHeight, col, tabLayer);
        const std::string label = (i < static_cast<int>(tabLabels.size())) ? tabLabels[i] : std::to_string(i + 1);
        renderer.updateText(m_tabLabelIds[i], tx + padding, absY + (tabBarHeight - fontSize) * 0.5f,
                            label, fontSize, labelColor, labelLayer);
    }

    // On a switch, purge the previously-shown page's retained entries (no ghosts), then render the
    // active page clipped to the content area.
    if (m_lastRenderedActive != m_activeIndex && m_lastRenderedActive >= 0 && m_lastRenderedActive < n) {
        children[m_lastRenderedActive]->releaseRenderEntries(renderer);
    }
    m_lastRenderedActive = m_activeIndex;

    float rx, ry, rw, rh;
    contentRect(rx, ry, rw, rh);
    renderer.pushClip(rx, ry, rw, rh);
    if (m_activeIndex < n && children[m_activeIndex]->visible) {
        children[m_activeIndex]->render(renderer);
    }
    renderer.popClip();
}

void UITabs::releaseRenderEntries(UIRenderer& renderer) {
    if (m_tabBarBgId != 0) { renderer.unregisterEntry(m_tabBarBgId); m_tabBarBgId = 0; }
    for (uint32_t r : m_tabRectIds) if (r != 0) renderer.unregisterEntry(r);
    for (uint32_t l : m_tabLabelIds) if (l != 0) renderer.unregisterEntry(l);
    m_tabRectIds.clear();
    m_tabLabelIds.clear();
    m_entriesRegistered = false;
    m_lastRenderedActive = -1;
    UIWidget::releaseRenderEntries(renderer);   // drops m_renderId + recurses to children
}

} // namespace grove
