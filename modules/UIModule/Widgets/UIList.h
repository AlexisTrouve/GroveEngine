#pragma once

#include "../Core/UIWidget.h"
#include <string>
#include <vector>
#include <cstdint>

namespace grove {

class IDataNode;

/**
 * @brief One data-driven row of a UIList (a ship in the sidebar). Pure data — the widget renders it.
 *
 * QUOI : le modèle d'un élément de liste : un id stable + un libellé + une 2e ligne optionnelle + une
 *   icône optionnelle. POURQUOI : la liste est un REPEATER piloté par données (le jeu pousse N vaisseaux,
 *   le widget fabrique N lignes) — l'item porte juste les données, jamais de widget. `id` est ce qu'on
 *   renvoie dans ui:list:selected (une uuid de vaisseau survivra à un réordonnancement, pas l'index).
 */
struct ListItem {
    std::string id;         // stable item id, echoed in ui:list:selected (e.g. a ship uuid)
    std::string label;      // primary text (ship name)
    std::string subtitle;   // optional secondary line (class / status); "" = none
    int iconTextureId = 0;  // optional left icon sprite (0 = none)
};

/**
 * @brief A named, collapsible GROUP of items — a warship wing/squadron in the fleet sidebar.
 *
 * QUOI : un groupe = un id + un libellé d'en-tête + un état replié + ses items. POURQUOI : une flotte
 *   s'organise en groupes (escadrons) ; la liste affiche un en-tête cliquable par groupe au-dessus de ses
 *   vaisseaux, repliable. Le moteur fournit le SYSTÈME (modèle + repli + events) ; Drifterra fait l'UI.
 */
struct ListGroup {
    std::string id;                 // stable group id, echoed in ui:list:group:toggled / ui:list:selected
    std::string label;              // header text (wing name)
    bool collapsed = false;         // header folded? (items hidden from the row projection)
    std::vector<ListItem> items;    // the group's items (ships)
};

/**
 * @brief One PROJECTED row of the list — either a group header or an item. Internal flat sequence.
 *
 * QUOI : la liste rend une SÉQUENCE PLATE de lignes ; chaque ligne est soit un en-tête de groupe, soit un
 *   item. POURQUOI : projeter groupes+repli → lignes plates laisse la virtualisation/scroll/clip/hit-test
 *   opérer sans rien savoir des groupes (le mode plat = un seul groupe anonyme sans en-tête). COMMENT :
 *   rebuildRows() (re)construit ce vecteur depuis m_items (plat) ou m_groups (groupé, repli honoré).
 */
struct ListRow {
    bool isHeader = false;          // true = group header row; false = item row
    std::string groupId;            // group this row belongs to ("" for an ungrouped flat item)
    // Item payload (item rows):
    std::string itemId;             // -> ui:list:selected.itemId
    std::string label;              // header: the group label; item: the item label
    std::string subtitle;           // item only
    int iconTextureId = 0;          // item only
    int itemIndex = -1;             // item's index within its group (flat: global) -> ui:list:selected.index
    // Header payload:
    bool collapsed = false;         // header only: the group's current collapse state (for the marker)
};

/**
 * @brief Scrollable, clipped, selectable data-driven list — the "sidebar partiel avec des vaisseaux".
 *
 * QUOI : une liste verticale de lignes GÉNÉRÉES depuis des données (ListItem), scrollable à la molette,
 *   découpée à ses bornes (scissor), avec une sélection unique qui émet ui:list:selected au clic.
 * POURQUOI : c'est la brique marquante d'un jeu de flotte — une sidebar de vaisseaux qu'on parcourt et
 *   sélectionne. On la fait "à la main" (path A) pour rester cohérent avec le reste du framework UI :
 *   tout passe par des render:* via le UIRenderer, tout est E2E-testable.
 * COMMENT : suit le PATTERN CONTENEUR (cf. UITabs/UIWindow) — les lignes ne sont PAS des UIWidget enfants
 *   mais un POOL RECYCLÉ d'entrées retained géré en interne, dimensionné au VIEWPORT (virtualisation : un
 *   set {bg,icon,label,subtitle} par ligne VISIBLE, remappé au scroll), ce qui évite la limite connue de
 *   UIScrollPanel et scale à des milliers d'items. Le scroll est un offset appliqué au rendu ; pushClip
 *   scissore le débord. Les données peuvent être PLATES (setItems) ou GROUPÉES (setGroups : des wings
 *   repliables) — les deux se projettent en une séquence plate de ListRow (rebuildRows), sur laquelle tout
 *   le reste opère sans rien savoir des groupes. L'interaction (ligne cliquée → sélection OU repli, event,
 *   molette) est centralisée dans UIModule. Sélection/scroll remis à zéro à chaque setItems/setGroups.
 *
 * LIMITES (suivi documenté) : pas de scrollbar visuelle ni de drag-to-scroll (molette seule), pas de
 *   template de ligne personnalisé, sélection unique, hiérarchie à UN niveau (groupes → items, pas d'arbre).
 */
class UIList : public UIWidget {
public:
    ~UIList() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "list"; }

    // Container pattern: clip the hit-test to the list bounds (no children today, but consistent + future
    // proof — a click scrolled out of view never lands on a row). Default hitClipRect = full bounds.
    bool clipsHitTest() const override { return true; }
    void releaseRenderEntries(UIRenderer& renderer) override;

    // --- Geometry / interaction (used by the hit-test absorb + UIModule). ---
    bool pointInBounds(float x, float y) const;
    int  rowAt(float screenY) const;          // item index at screen y (within bounds + range), or -1
    void handleMouseWheel(float wheelDelta);  // wheel scroll (routed by UIModule, like UIScrollPanel)

    // --- Scrollbar + drag-to-scroll. ---
    bool isScrollable() const { return contentHeight() > height; }   // content taller than the viewport
    // The scrollbar thumb rect (screen px). Only meaningful when isScrollable(); the track is the right
    // `scrollbarWidth` column over the full height.
    void scrollbarThumbRect(float& x, float& y, float& w, float& h) const;
    bool pointInScrollbar(float x, float y) const;   // in the right scrollbar column (the grab zone)
    // True if the CURRENT press is a scroll interaction (thumb grab, or a content drag past the threshold)
    // and so must NOT be treated as a click-select. UIModule reads this on release. Stays valid through the
    // release frame; resets on the next press.
    bool suppressClick() const { return m_dragged; }

    // The scroll-aware window of on-screen items: `firstItem` = first (possibly top-clipped) visible row,
    // `count` = how many rows from there intersect the viewport. Bounded by ceil(height/rowHeight)+1
    // REGARDLESS of item count — this is the basis of virtualization (only these rows get render entries).
    void visibleRange(int& firstItem, int& count) const;

    // --- Data (data-driven via the UITree factory / ui:list:set_items / ui:list:set_groups). ---
    // Replace the whole item set (FLAT mode — one ungrouped sequence). Resets scroll + selection.
    // Virtualized: no renderer release needed — render() recycles the row-slot pool.
    void setItems(std::vector<ListItem> newItems);
    const std::vector<ListItem>& items() const { return m_items; }

    // Replace the data as GROUPED (warship wings). Resets scroll + selection. Each group renders a
    // collapsible header over its items. Toggle a group via toggleGroup() (UIModule wires the header click).
    void setGroups(std::vector<ListGroup> newGroups);
    bool isGrouped() const { return m_grouped; }

    // Flip a group's collapsed state (rebuilds the row projection + clamps scroll). Returns the NEW
    // collapsed state (for ui:list:group:toggled). No-op (returns false) if the id isn't found.
    bool toggleGroup(const std::string& groupId);

    // --- Projected rows (what the renderer/hit-test see; UIModule reads these to act on a click). ---
    int rowCount() const { return static_cast<int>(m_rows.size()); }
    const ListRow* rowPtr(int rowIndex) const;   // nullptr if out of range

    int  selectedIndex() const { return m_selectedIndex; }   // selected ROW index (flat: == item index)
    void setSelectedIndex(int i);             // clamped to a valid row (or -1 if empty); tracks the itemId

    // Parse an `items` array-of-objects child off `containerNode` (factory node OR set_items message).
    // Each entry: {id, label, subtitle?, icon?}. Shared by UITree + UIModule + parseGroups (no dup).
    static std::vector<ListItem> parseItems(IDataNode& containerNode);
    // Parse a `groups` array-of-objects: each {id, label, collapsed?, items:[...]} (items via parseItems).
    static std::vector<ListGroup> parseGroups(IDataNode& containerNode);

    // --- Properties (data-driven). ---
    float rowHeight = 36.0f;
    float padding = 8.0f;
    float fontSize = 14.0f;
    float subtitleFontSize = 11.0f;
    float iconSize = 24.0f;
    uint32_t bgColor = 0x1d2430FF;
    uint32_t rowColor = 0x232c3aFF;       // even rows
    uint32_t rowAltColor = 0x202836FF;    // odd rows (zebra)
    uint32_t hoverColor = 0x2c3a4eFF;
    uint32_t selectedColor = 0x3a6ea5FF;
    uint32_t labelColor = 0xFFFFFFFF;
    uint32_t subtitleColor = 0x9fb0c4FF;
    uint32_t headerColor = 0x2c3540FF;        // group header row background (distinct from item rows)
    uint32_t headerLabelColor = 0xFFFFFFFF;   // group header label
    float scrollbarWidth = 8.0f;              // right scrollbar column width (0 = no visual scrollbar)
    uint32_t scrollbarColor = 0x5a6b80FF;     // thumb
    uint32_t scrollbarTrackColor = 0x161b24FF;// track
    float dragThreshold = 5.0f;               // px a content press must move before it becomes a scroll-drag

    // --- Scroll state. ---
    float scrollOffsetY = 0.0f;

private:
    void clampScroll();
    void rebuildRows();   // project m_items (flat) or m_groups (grouped, collapse-honoured) -> m_rows
    float contentHeight() const { return rowHeight * static_cast<float>(m_rows.size()); }

    // Grow the recycled render-id pool to at least `neededSlots` row-slots (and register the panel bg
    // once). VIRTUALIZATION: the pool is sized to the VISIBLE window, not the item count — slots are
    // remapped to whichever items are scrolled into view each frame. Grow-only (handles a viewport that
    // enlarges, e.g. on resize); never shrinks the allocation (unused slots are just hidden in render()).
    void ensurePool(UIRenderer& renderer, int neededSlots);

    std::vector<ListItem> m_items;      // FLAT-mode source (empty when grouped)
    std::vector<ListGroup> m_groups;    // GROUPED-mode source (empty when flat)
    bool m_grouped = false;
    std::vector<ListRow> m_rows;        // projected visible sequence (renderer / hit-test / virtualization)

    int m_selectedIndex = -1;           // selected ROW index (for the highlight)
    std::string m_selectedItemId;       // selected item's id — so the highlight follows it across a rebuild
    int m_hoverIndex = -1;              // hovered ROW index

    // Scroll-drag state (driven in update() from the UIContext mouse). A press grabs either the THUMB
    // (proportional drag) or the CONTENT (drag-to-scroll past dragThreshold). m_dragged stays set through
    // the release frame so UIModule's release-select can skip a drag; it resets on the next press.
    enum class Grab { None, Thumb, Content };
    Grab  m_grab = Grab::None;
    bool  m_dragged = false;            // current press became a scroll interaction (suppresses click-select)
    float m_pressY = 0.0f;             // mouse y at press
    float m_pressScroll = 0.0f;        // scrollOffsetY at press

    // Recycled retained render-id pool: the panel background (m_renderId) + a viewport-bounded number of
    // row id-sets {bg, icon, label, subtitle}. Slot s shows item (firstVisible + s) for the current frame.
    std::vector<uint32_t> m_rowBgIds;
    std::vector<uint32_t> m_rowIconIds;
    std::vector<uint32_t> m_rowLabelIds;
    std::vector<uint32_t> m_rowSubtitleIds;
    uint32_t m_trackId = 0;            // scrollbar track render entry
    uint32_t m_thumbId = 0;            // scrollbar thumb render entry
};

} // namespace grove
