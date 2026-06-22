#include "UIList.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <grove/IDataNode.h>
#include <algorithm>
#include <string>

namespace grove {

// ============================================================================
// Geometry / interaction
// ============================================================================

bool UIList::pointInBounds(float x, float y) const {
    return x >= absX && x <= absX + width && y >= absY && y <= absY + height;
}

int UIList::rowAt(float screenY) const {
    // QUOI : index de l'item sous une ordonnée écran. POURQUOI : c'est le hit-test des lignes (le pool
    //   d'entrées retained n'a pas de hiérarchie cliquable — UIModule résout l'index ici). COMMENT :
    //   on rejette hors des bornes (= hors de la zone clippée, donc invisible), puis on remonte le scroll :
    //   y_local = screenY - absY + scroll ; index = floor(y_local / rowHeight) ; borné à [0, N).
    if (screenY < absY || screenY >= absY + height) return -1;
    const float localY = screenY - absY + scrollOffsetY;
    if (localY < 0.0f) return -1;
    const int i = static_cast<int>(localY / rowHeight);
    if (i < 0 || i >= static_cast<int>(m_items.size())) return -1;
    return i;
}

void UIList::handleMouseWheel(float wheelDelta) {
    // Same convention/speed as UIScrollPanel: a positive wheel scrolls UP (offset decreases).
    scrollOffsetY -= wheelDelta * 20.0f;
    clampScroll();
}

void UIList::clampScroll() {
    const float maxScroll = std::max(0.0f, contentHeight() - height);
    scrollOffsetY = std::clamp(scrollOffsetY, 0.0f, maxScroll);
}

// ============================================================================
// Data
// ============================================================================

void UIList::setItems(std::vector<ListItem> newItems) {
    // Replace the data and reset the view: selection cleared, scrolled back to the top. The caller
    // (UITree factory / UIModule set_items handler) follows with releaseRenderEntries() so the next
    // render() rebuilds the row-id pool at the new count — purging the previous rows' entries first.
    m_items = std::move(newItems);
    m_selectedIndex = -1;
    m_hoverIndex = -1;
    scrollOffsetY = 0.0f;
}

void UIList::setSelectedIndex(int i) {
    const int n = static_cast<int>(m_items.size());
    if (n <= 0) { m_selectedIndex = -1; return; }
    m_selectedIndex = std::clamp(i, 0, n - 1);
}

std::vector<ListItem> UIList::parseItems(IDataNode& containerNode) {
    // Read an `items` array-of-objects (keyed by index "0","1",... — strings-in-arrays don't round-trip
    // through the JSON tree, so every element is an OBJECT, exactly like tabs[] / radial items[]).
    std::vector<ListItem> out;
    IDataNode* arr = containerNode.getChildReadOnly("items");
    if (!arr) return out;
    int i = 0;
    while (IDataNode* it = arr->getChildReadOnly(std::to_string(i))) {
        ListItem item;
        item.id            = it->getString("id", std::to_string(i));
        item.label         = it->getString("label", "");
        item.subtitle      = it->getString("subtitle", "");
        item.iconTextureId = it->getInt("icon", 0);
        out.push_back(std::move(item));
        ++i;
    }
    return out;
}

// ============================================================================
// Update
// ============================================================================

void UIList::update(UIContext& ctx, float deltaTime) {
    (void)deltaTime;
    if (!visible) return;

    // Keep the scroll valid (a shrink via setItems can leave a stale offset past the new max).
    clampScroll();

    // Hover row (cosmetic highlight); -1 when the cursor is off the list.
    m_hoverIndex = pointInBounds(ctx.mouseX, ctx.mouseY) ? rowAt(ctx.mouseY) : -1;
}

// ============================================================================
// Render
// ============================================================================

void UIList::render(UIRenderer& renderer) {
    if (!visible) return;

    const int n = static_cast<int>(m_items.size());

    // First render (or after a setItems-driven release): register the panel bg + one id-set per row.
    if (!m_entriesRegistered) {
        m_renderId = renderer.registerEntry();   // panel background
        m_rowBgIds.resize(n);
        m_rowIconIds.resize(n);
        m_rowLabelIds.resize(n);
        m_rowSubtitleIds.resize(n);
        for (int i = 0; i < n; ++i) {
            m_rowBgIds[i]       = renderer.registerEntry();
            m_rowIconIds[i]     = renderer.registerEntry();
            m_rowLabelIds[i]    = renderer.registerEntry();
            m_rowSubtitleIds[i] = renderer.registerEntry();
        }
        m_entriesRegistered = true;
        m_registered = true;
        // Unregister EVERY entry on destruction (capture the id pools by value). renderer outlives the
        // widget (shutdown resets m_root before m_renderer), so the &renderer capture stays valid.
        setDestroyCallback([&renderer, bgs = m_rowBgIds, icons = m_rowIconIds,
                            labels = m_rowLabelIds, subs = m_rowSubtitleIds](uint32_t id) {
            renderer.unregisterEntry(id);
            for (uint32_t e : bgs)    renderer.unregisterEntry(e);
            for (uint32_t e : icons)  renderer.unregisterEntry(e);
            for (uint32_t e : labels) renderer.unregisterEntry(e);
            for (uint32_t e : subs)   renderer.unregisterEntry(e);
        });
    }

    // Panel background (full bounds).
    renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, renderer.nextLayer());

    // Rows, clipped to the panel: a partially-scrolled row is scissored at the edge; a fully-scrolled-out
    // row is published off-screen but scissored away (no ghost). Two stacked layers so content (icon/text)
    // always draws over the row backgrounds.
    const int bgLayer      = renderer.nextLayer();
    const int contentLayer = renderer.nextLayer();
    renderer.pushClip(absX, absY, width, height);
    for (int i = 0; i < n; ++i) {
        const ListItem& item = m_items[i];
        const float rowY = absY + i * rowHeight - scrollOffsetY;

        // Row background: selected > hovered > zebra base.
        uint32_t col = (i % 2 == 0) ? rowColor : rowAltColor;
        if (i == m_hoverIndex)    col = hoverColor;
        if (i == m_selectedIndex) col = selectedColor;
        renderer.updateRect(m_rowBgIds[i], absX, rowY, width, rowHeight, col, bgLayer);

        // Optional left icon.
        float textX = absX + padding;
        if (item.iconTextureId > 0) {
            const float iy = rowY + (rowHeight - iconSize) * 0.5f;
            renderer.updateSprite(m_rowIconIds[i], absX + padding, iy, iconSize, iconSize,
                                  item.iconTextureId, 0xFFFFFFFF, contentLayer);
            textX = absX + padding + iconSize + padding;
        } else {
            renderer.updateRect(m_rowIconIds[i], 0, 0, 0, 0, 0, contentLayer);   // hide
        }

        // Label (+ optional subtitle). With a subtitle, the two stack; otherwise the label is centred.
        if (item.subtitle.empty()) {
            const float ly = rowY + (rowHeight - fontSize) * 0.5f;
            renderer.updateText(m_rowLabelIds[i], textX, ly, item.label, fontSize, labelColor, contentLayer);
            renderer.updateText(m_rowSubtitleIds[i], textX, ly, "", subtitleFontSize, subtitleColor, contentLayer);
        } else {
            const float ly = rowY + padding * 0.5f;
            const float sy = ly + fontSize + 2.0f;
            renderer.updateText(m_rowLabelIds[i], textX, ly, item.label, fontSize, labelColor, contentLayer);
            renderer.updateText(m_rowSubtitleIds[i], textX, sy, item.subtitle, subtitleFontSize, subtitleColor, contentLayer);
        }
    }
    renderer.popClip();
}

void UIList::releaseRenderEntries(UIRenderer& renderer) {
    // Purge the per-row pool, then let the base drop m_renderId. Resetting m_entriesRegistered makes the
    // next render() re-register at the current item count — this is how a runtime setItems() repopulates
    // without leaving ghost rows.
    for (uint32_t e : m_rowBgIds)       if (e != 0) renderer.unregisterEntry(e);
    for (uint32_t e : m_rowIconIds)     if (e != 0) renderer.unregisterEntry(e);
    for (uint32_t e : m_rowLabelIds)    if (e != 0) renderer.unregisterEntry(e);
    for (uint32_t e : m_rowSubtitleIds) if (e != 0) renderer.unregisterEntry(e);
    m_rowBgIds.clear();
    m_rowIconIds.clear();
    m_rowLabelIds.clear();
    m_rowSubtitleIds.clear();
    m_entriesRegistered = false;
    UIWidget::releaseRenderEntries(renderer);   // drops m_renderId + recurses (no children) + sets dirty
}

} // namespace grove
