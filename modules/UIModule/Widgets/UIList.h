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
 * @brief Scrollable, clipped, selectable data-driven list — the "sidebar partiel avec des vaisseaux".
 *
 * QUOI : une liste verticale de lignes GÉNÉRÉES depuis des données (ListItem), scrollable à la molette,
 *   découpée à ses bornes (scissor), avec une sélection unique qui émet ui:list:selected au clic.
 * POURQUOI : c'est la brique marquante d'un jeu de flotte — une sidebar de vaisseaux qu'on parcourt et
 *   sélectionne. On la fait "à la main" (path A) pour rester cohérent avec le reste du framework UI :
 *   tout passe par des render:* via le UIRenderer, tout est E2E-testable.
 * COMMENT : suit le PATTERN CONTENEUR (cf. UITabs/UIWindow) — les lignes ne sont PAS des UIWidget enfants
 *   mais un POOL d'entrées retained gérées en interne (un set {bg,icon,label,subtitle} par item), ce qui
 *   évite la limite connue de UIScrollPanel (un enfant panel se "dé-scrolle" en se re-layoutant). Le scroll
 *   est un simple offset appliqué à la position des lignes au rendu ; pushClip(bornes) scissore le débord.
 *   L'interaction (résoudre la ligne cliquée, publier l'event, router la molette) est centralisée dans
 *   UIModule, comme pour les autres conteneurs. La sélection/scroll sont remis à zéro à chaque setItems().
 *
 * LIMITES (suivi documenté, hors-scope MVP) : non VIRTUALISÉ (O(N) updateRect/frame pendant le scroll, ok
 *   pour ~dizaines de lignes ; la virtualisation = render des seules lignes visibles est le follow-on perf),
 *   pas de scrollbar visuelle ni de drag-to-scroll (molette seule), pas de template de ligne personnalisé,
 *   sélection unique uniquement.
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

    // The scroll-aware window of on-screen items: `firstItem` = first (possibly top-clipped) visible row,
    // `count` = how many rows from there intersect the viewport. Bounded by ceil(height/rowHeight)+1
    // REGARDLESS of item count — this is the basis of virtualization (only these rows get render entries).
    void visibleRange(int& firstItem, int& count) const;

    // --- Data (data-driven via the UITree factory or ui:list:set_items). ---
    // Replace the whole item set; resets scroll + selection. The CALLER must follow with
    // releaseRenderEntries(renderer) so render() re-registers the row-id pool with the new count
    // (mirrors ui:radial:set_items) — the widget has no renderer handle of its own.
    void setItems(std::vector<ListItem> newItems);
    const std::vector<ListItem>& items() const { return m_items; }

    int  selectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int i);             // clamped to a valid item (or -1 if empty)

    // Parse an `items` array-of-objects child off `containerNode` (the factory node OR a set_items
    // message). Each entry: {id, label, subtitle?, icon?}. Shared by UITree + UIModule (no dup).
    static std::vector<ListItem> parseItems(IDataNode& containerNode);

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

    // --- Scroll state. ---
    float scrollOffsetY = 0.0f;

private:
    void clampScroll();
    float contentHeight() const { return rowHeight * static_cast<float>(m_items.size()); }

    // Grow the recycled render-id pool to at least `neededSlots` row-slots (and register the panel bg
    // once). VIRTUALIZATION: the pool is sized to the VISIBLE window, not the item count — slots are
    // remapped to whichever items are scrolled into view each frame. Grow-only (handles a viewport that
    // enlarges, e.g. on resize); never shrinks the allocation (unused slots are just hidden in render()).
    void ensurePool(UIRenderer& renderer, int neededSlots);

    std::vector<ListItem> m_items;
    int m_selectedIndex = -1;
    int m_hoverIndex = -1;

    // Recycled retained render-id pool: the panel background (m_renderId) + a viewport-bounded number of
    // row id-sets {bg, icon, label, subtitle}. Slot s shows item (firstVisible + s) for the current frame.
    std::vector<uint32_t> m_rowBgIds;
    std::vector<uint32_t> m_rowIconIds;
    std::vector<uint32_t> m_rowLabelIds;
    std::vector<uint32_t> m_rowSubtitleIds;
};

} // namespace grove
