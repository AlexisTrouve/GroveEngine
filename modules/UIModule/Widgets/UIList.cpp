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
    // QUOI : index de la LIGNE (header ou item) sous une ordonnée écran. POURQUOI : c'est le hit-test des
    //   lignes (le pool retained n'a pas de hiérarchie cliquable — UIModule résout l'index ici puis lit
    //   rowPtr pour agir : sélection si item, repli si header). COMMENT : on rejette hors des bornes (=
    //   hors zone clippée, invisible), puis on remonte le scroll : floor((screenY-absY+scroll)/rowHeight).
    if (screenY < absY || screenY >= absY + height) return -1;
    const float localY = screenY - absY + scrollOffsetY;
    if (localY < 0.0f) return -1;
    const int i = static_cast<int>(localY / rowHeight);
    if (i < 0 || i >= static_cast<int>(m_rows.size())) return -1;
    return i;
}

void UIList::visibleRange(int& firstItem, int& count) const {
    // QUOI : la fenêtre de LIGNES réellement à l'écran. COMMENT : firstItem = la 1re ligne (éventuellement
    //   rognée en haut) = floor(scroll/rowHeight) ; on compte ensuite les lignes dont le HAUT (relatif au
    //   bord supérieur) est encore < height (donc qui touchent le viewport). Borné par ceil(height/rh)+1.
    const int n = static_cast<int>(m_rows.size());
    if (n <= 0) { firstItem = 0; count = 0; return; }
    int first = static_cast<int>(scrollOffsetY / rowHeight);
    if (first < 0) first = 0;
    if (first > n - 1) first = n - 1;
    int c = 0;
    for (int i = first; i < n; ++i) {
        const float topRel = i * rowHeight - scrollOffsetY;  // row top relative to absY
        if (topRel >= height) break;                         // starts at/below the bottom edge -> done
        ++c;
    }
    firstItem = first;
    count = c;
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

void UIList::scrollbarThumbRect(float& x, float& y, float& w, float& h) const {
    // QUOI : rect du thumb. COMMENT : largeur = colonne droite ; hauteur ∝ (viewport/contenu) avec un
    //   minimum ; position ∝ (scroll / scrollable). Identique au UIScrollPanel (cohérence).
    const float ch = contentHeight();
    const float thumbH = std::max(height * (height / ch), 20.0f);
    const float maxScroll = std::max(1.0f, ch - height);             // avoid /0 (only called when scrollable)
    const float ratio = std::clamp(scrollOffsetY / maxScroll, 0.0f, 1.0f);
    x = absX + width - scrollbarWidth;
    y = absY + ratio * (height - thumbH);
    w = scrollbarWidth;
    h = thumbH;
}

bool UIList::pointInScrollbar(float x, float y) const {
    // The whole right `scrollbarWidth` column (over the list height) is the scrollbar grab zone — a press
    // there is a scroll interaction, never a row select (even off the thumb itself).
    return isScrollable() && x >= absX + width - scrollbarWidth && x <= absX + width &&
           y >= absY && y <= absY + height;
}

// ============================================================================
// Data
// ============================================================================

void UIList::rebuildRows() {
    // Project the data source -> the flat row sequence the renderer/hit-test consume. FLAT: one item row
    // each (itemIndex = global). GROUPED: a header row per group + (unless collapsed) its item rows
    // (itemIndex = within the group). After a rebuild, re-resolve the highlight to the selected ITEM (its
    // row index can shift when a group above it collapses/expands) — that's what m_selectedItemId is for.
    m_rows.clear();
    if (m_grouped) {
        for (const ListGroup& g : m_groups) {
            ListRow h;
            h.isHeader = true; h.groupId = g.id; h.label = g.label; h.collapsed = g.collapsed;
            m_rows.push_back(std::move(h));
            if (!g.collapsed) {
                for (int j = 0; j < static_cast<int>(g.items.size()); ++j) {
                    const ListItem& it = g.items[j];
                    ListRow r;
                    r.groupId = g.id; r.itemId = it.id; r.label = it.label; r.subtitle = it.subtitle;
                    r.iconTextureId = it.iconTextureId; r.itemIndex = j;
                    m_rows.push_back(std::move(r));
                }
            }
        }
    } else {
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            const ListItem& it = m_items[i];
            ListRow r;
            r.itemId = it.id; r.label = it.label; r.subtitle = it.subtitle;
            r.iconTextureId = it.iconTextureId; r.itemIndex = i;
            m_rows.push_back(std::move(r));
        }
    }

    // Re-resolve the selected row from the selected item id (follows the item across collapses).
    m_selectedIndex = -1;
    if (!m_selectedItemId.empty()) {
        for (int r = 0; r < static_cast<int>(m_rows.size()); ++r) {
            if (!m_rows[r].isHeader && m_rows[r].itemId == m_selectedItemId) { m_selectedIndex = r; break; }
        }
    }
}

void UIList::setItems(std::vector<ListItem> newItems) {
    // Replace the data (FLAT mode) and reset the view: selection cleared, scrolled to the top. Virtualized,
    // so no renderer release is needed — render() recycles the row-slot pool over the new projection.
    m_items = std::move(newItems);
    m_groups.clear();
    m_grouped = false;
    m_selectedItemId.clear();
    m_hoverIndex = -1;
    scrollOffsetY = 0.0f;
    rebuildRows();
}

void UIList::setGroups(std::vector<ListGroup> newGroups) {
    // Replace the data (GROUPED mode — warship wings). Same reset semantics as setItems.
    m_groups = std::move(newGroups);
    m_items.clear();
    m_grouped = true;
    m_selectedItemId.clear();
    m_hoverIndex = -1;
    scrollOffsetY = 0.0f;
    rebuildRows();
}

bool UIList::toggleGroup(const std::string& groupId) {
    // Flip a group's collapse state, re-project the rows, and keep the scroll valid (collapsing shrinks the
    // content). Returns the NEW collapsed state for ui:list:group:toggled. No-op if the id is unknown.
    for (ListGroup& g : m_groups) {
        if (g.id == groupId) {
            g.collapsed = !g.collapsed;
            rebuildRows();
            clampScroll();
            return g.collapsed;
        }
    }
    return false;
}

const ListRow* UIList::rowPtr(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) return nullptr;
    return &m_rows[rowIndex];
}

void UIList::setSelectedIndex(int i) {
    // Select by ROW index (clamped). Track the selected item's id so the highlight survives a rebuild.
    const int n = static_cast<int>(m_rows.size());
    if (n <= 0) { m_selectedIndex = -1; m_selectedItemId.clear(); return; }
    m_selectedIndex = std::clamp(i, 0, n - 1);
    const ListRow& r = m_rows[m_selectedIndex];
    m_selectedItemId = r.isHeader ? std::string() : r.itemId;
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

std::vector<ListGroup> UIList::parseGroups(IDataNode& containerNode) {
    // Read a `groups` array-of-objects: each {id, label, collapsed?, items:[...]}. The nested items[] are
    // parsed by reusing parseItems on the group node (it has its own "items" child). Same json-backed
    // constraint as items[] over IIO (see UI_TOPICS): the array must live in the node's JSON.
    std::vector<ListGroup> out;
    IDataNode* arr = containerNode.getChildReadOnly("groups");
    if (!arr) return out;
    int i = 0;
    while (IDataNode* g = arr->getChildReadOnly(std::to_string(i))) {
        ListGroup group;
        group.id        = g->getString("id", std::to_string(i));
        group.label     = g->getString("label", "");
        group.collapsed = g->getBool("collapsed", false);
        group.items     = parseItems(*g);
        out.push_back(std::move(group));
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

    // --- Scroll-drag (thumb + content). Runs AFTER UIModule's click dispatch in the frame, so the
    //     release-select reads m_dragged from prior frames before we clear the grab here. ---
    const float mx = ctx.mouseX, my = ctx.mouseY;

    if (ctx.mousePressed && pointInBounds(mx, my)) {
        m_pressY = my;
        m_pressScroll = scrollOffsetY;
        if (pointInScrollbar(mx, my)) {
            // Grab the scrollbar: any press in the bar column is a scroll interaction (never a select).
            m_grab = Grab::Thumb;
            m_dragged = true;
        } else {
            // Content press: a CLICK until/unless it moves past the threshold (then it's a drag-scroll).
            m_grab = Grab::Content;
            m_dragged = false;
        }
    }

    if (ctx.mouseDown && m_grab == Grab::Thumb && isScrollable()) {
        // Map the mouse delta along the track to a scroll delta (proportional to content/track ratio).
        float tx, ty, tw, th; scrollbarThumbRect(tx, ty, tw, th);
        const float trackSpan = std::max(1.0f, height - th);
        const float scrollSpan = std::max(0.0f, contentHeight() - height);
        scrollOffsetY = m_pressScroll + (my - m_pressY) / trackSpan * scrollSpan;
        clampScroll();
    } else if (ctx.mouseDown && m_grab == Grab::Content) {
        if (std::abs(my - m_pressY) > dragThreshold) m_dragged = true;
        if (m_dragged) {                                // drag the content 1:1 (grab-and-pull)
            scrollOffsetY = m_pressScroll - (my - m_pressY);
            clampScroll();
        }
    }

    if (ctx.mouseReleased) {
        m_grab = Grab::None;        // end the grab; m_dragged stays for UIModule's release-select THIS frame
    }

    // Keep the scroll valid (a shrink via setItems can leave a stale offset past the new max).
    clampScroll();

    // Hover row (cosmetic highlight); -1 when the cursor is off the list.
    m_hoverIndex = pointInBounds(mx, my) ? rowAt(my) : -1;
}

// ============================================================================
// Render
// ============================================================================

void UIList::ensurePool(UIRenderer& renderer, int neededSlots) {
    bool changed = false;
    if (m_renderId == 0) {
        m_renderId = renderer.registerEntry();   // panel background
        m_trackId  = renderer.registerEntry();   // scrollbar track
        m_thumbId  = renderer.registerEntry();   // scrollbar thumb
        changed = true;
    }

    const int have = static_cast<int>(m_rowBgIds.size());
    if (neededSlots > have) {
        for (int i = have; i < neededSlots; ++i) {
            m_rowBgIds.push_back(renderer.registerEntry());
            m_rowIconIds.push_back(renderer.registerEntry());
            m_rowLabelIds.push_back(renderer.registerEntry());
            m_rowSubtitleIds.push_back(renderer.registerEntry());
        }
        changed = true;
    }

    if (changed) {
        m_registered = true;
        // (Re)set the destroy callback to capture the current ids by value (renderer outlives the widget —
        // shutdown resets m_root before m_renderer — so &renderer stays valid). `id` = m_renderId (the bg).
        setDestroyCallback([&renderer, track = m_trackId, thumb = m_thumbId, bgs = m_rowBgIds,
                            icons = m_rowIconIds, labels = m_rowLabelIds, subs = m_rowSubtitleIds](uint32_t id) {
            renderer.unregisterEntry(id);
            renderer.unregisterEntry(track);
            renderer.unregisterEntry(thumb);
            for (uint32_t e : bgs)    renderer.unregisterEntry(e);
            for (uint32_t e : icons)  renderer.unregisterEntry(e);
            for (uint32_t e : labels) renderer.unregisterEntry(e);
            for (uint32_t e : subs)   renderer.unregisterEntry(e);
        });
    }
}

void UIList::render(UIRenderer& renderer) {
    if (!visible) return;

    const int n = static_cast<int>(m_rows.size());

    // VIRTUALIZATION: only the on-screen window of ROWS gets render entries. We size the pool to that
    // window (grow-only) and remap its slots to rows [first .. first+count) — a 10k-row list still
    // registers only ~viewport-many entries. Slots beyond the visible count are hidden (no ghosts).
    int first = 0, count = 0;
    visibleRange(first, count);
    ensurePool(renderer, count);

    // Panel background (full bounds).
    renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, renderer.nextLayer());

    // Rows, clipped to the panel: a partially-scrolled row is scissored at the edge. Two stacked layers
    // so content (icon/text) always draws over the row backgrounds.
    const int bgLayer      = renderer.nextLayer();
    const int contentLayer = renderer.nextLayer();
    const int slots = static_cast<int>(m_rowBgIds.size());
    renderer.pushClip(absX, absY, width, height);
    for (int s = 0; s < slots; ++s) {
        const int i = first + s;   // the row this slot shows this frame

        // Slot beyond the visible window (or past the end) -> hide it (zero rect / icon / empty text).
        if (s >= count || i >= n) {
            renderer.updateRect(m_rowBgIds[s], 0, 0, 0, 0, 0, bgLayer);
            renderer.updateRect(m_rowIconIds[s], 0, 0, 0, 0, 0, contentLayer);
            renderer.updateText(m_rowLabelIds[s], 0, 0, "", fontSize, labelColor, contentLayer);
            renderer.updateText(m_rowSubtitleIds[s], 0, 0, "", subtitleFontSize, subtitleColor, contentLayer);
            continue;
        }

        const ListRow& row = m_rows[i];
        const float rowY = absY + i * rowHeight - scrollOffsetY;

        if (row.isHeader) {
            // Group header: distinct bg + a collapse marker ("v"/">" — ASCII, font-safe) + the wing label.
            // No icon / subtitle. (Drifterra restyles; the engine only needs a functional distinct header.)
            renderer.updateRect(m_rowBgIds[s], absX, rowY, width, rowHeight, headerColor, bgLayer);
            renderer.updateRect(m_rowIconIds[s], 0, 0, 0, 0, 0, contentLayer);   // no icon on a header
            const std::string marker = row.collapsed ? "> " : "v ";
            const float ly = rowY + (rowHeight - fontSize) * 0.5f;
            renderer.updateText(m_rowLabelIds[s], absX + padding, ly, marker + row.label, fontSize,
                                headerLabelColor, contentLayer);
            renderer.updateText(m_rowSubtitleIds[s], 0, 0, "", subtitleFontSize, subtitleColor, contentLayer);
            continue;
        }

        // Item row background: selected > hovered > zebra base (zebra by item index so each group restarts).
        uint32_t col = (row.itemIndex % 2 == 0) ? rowColor : rowAltColor;
        if (i == m_hoverIndex)    col = hoverColor;
        if (i == m_selectedIndex) col = selectedColor;
        renderer.updateRect(m_rowBgIds[s], absX, rowY, width, rowHeight, col, bgLayer);

        // Optional left icon. Group items get a small left indent so they read as nested under the header.
        const float indent = m_grouped ? padding : 0.0f;
        float textX = absX + padding + indent;
        if (row.iconTextureId > 0) {
            const float iy = rowY + (rowHeight - iconSize) * 0.5f;
            renderer.updateSprite(m_rowIconIds[s], absX + padding + indent, iy, iconSize, iconSize,
                                  row.iconTextureId, 0xFFFFFFFF, contentLayer);
            textX = absX + padding + indent + iconSize + padding;
        } else {
            renderer.updateRect(m_rowIconIds[s], 0, 0, 0, 0, 0, contentLayer);   // hide
        }

        // Label (+ optional subtitle). With a subtitle, the two stack; otherwise the label is centred.
        if (row.subtitle.empty()) {
            const float ly = rowY + (rowHeight - fontSize) * 0.5f;
            renderer.updateText(m_rowLabelIds[s], textX, ly, row.label, fontSize, labelColor, contentLayer);
            renderer.updateText(m_rowSubtitleIds[s], textX, ly, "", subtitleFontSize, subtitleColor, contentLayer);
        } else {
            const float ly = rowY + padding * 0.5f;
            const float sy = ly + fontSize + 2.0f;
            renderer.updateText(m_rowLabelIds[s], textX, ly, row.label, fontSize, labelColor, contentLayer);
            renderer.updateText(m_rowSubtitleIds[s], textX, sy, row.subtitle, subtitleFontSize, subtitleColor, contentLayer);
        }
    }
    renderer.popClip();

    // Scrollbar (track + thumb), drawn ON TOP of the rows at the right edge — outside the row clip so it's
    // always fully visible. Shown only when the content overflows; hidden (zero-size) otherwise.
    if (scrollbarWidth > 0.0f && isScrollable()) {
        const int sbLayer = renderer.nextLayer();
        const float trackX = absX + width - scrollbarWidth;
        renderer.updateRect(m_trackId, trackX, absY, scrollbarWidth, height, scrollbarTrackColor, sbLayer);
        float tx, ty, tw, th; scrollbarThumbRect(tx, ty, tw, th);
        renderer.updateRect(m_thumbId, tx, ty, tw, th, scrollbarColor, renderer.nextLayer());
    } else {
        renderer.updateRect(m_trackId, 0, 0, 0, 0, 0, renderer.nextLayer());
        renderer.updateRect(m_thumbId, 0, 0, 0, 0, 0, renderer.nextLayer());
    }
}

void UIList::releaseRenderEntries(UIRenderer& renderer) {
    // Purge the whole recycled pool, then let the base drop m_renderId. The next render() re-grows from
    // scratch. (Called on hide / ui:set_visible false; a runtime setItems() does NOT need this — the
    // recycled slots are simply rewritten or hidden each frame, so repopulating leaves no ghosts.)
    for (uint32_t e : m_rowBgIds)       if (e != 0) renderer.unregisterEntry(e);
    for (uint32_t e : m_rowIconIds)     if (e != 0) renderer.unregisterEntry(e);
    for (uint32_t e : m_rowLabelIds)    if (e != 0) renderer.unregisterEntry(e);
    for (uint32_t e : m_rowSubtitleIds) if (e != 0) renderer.unregisterEntry(e);
    if (m_trackId != 0) { renderer.unregisterEntry(m_trackId); m_trackId = 0; }
    if (m_thumbId != 0) { renderer.unregisterEntry(m_thumbId); m_thumbId = 0; }
    m_rowBgIds.clear();
    m_rowIconIds.clear();
    m_rowLabelIds.clear();
    m_rowSubtitleIds.clear();
    UIWidget::releaseRenderEntries(renderer);   // drops m_renderId + recurses (no children) + sets dirty
}

} // namespace grove
