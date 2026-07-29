#pragma once

#include "../Core/UIWidget.h"
#include "UIFrame.h"
#include <string>
#include <vector>
#include <cstdint>

namespace grove {

/**
 * @brief Tabbed container ("menu with sections") — UI framework slice 5c.
 *
 * A tab bar of N buttons across the top + N pages (the children). Exactly ONE page is shown at a
 * time (the others are hidden + their retained entries purged), in a content area below the bar that
 * CLIPS its children (slice-2 clip). The widget is OPAQUE (hit-test absorbs clicks in its bounds).
 * Clicking a tab switches the active page; UIModule publishes ui:tab:changed {id, index}.
 */
class UITabs : public UIWidget {
public:
    ~UITabs() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "tabs"; }

    // Opaque au clic : delegue au predicat deja present sur ce widget.
    bool absorbsPoint(float x, float y) const override { return pointInBounds(x, y); }

    // Remonté au module dès le PRESS, même sans avoir rien consommé : c'est lui qui résout le
    // changement d'onglet (il doit publier ui:tab:changed, ce qu'un widget ne peut pas faire).
    bool surfacesClick(bool pressed, bool handled) const override { return handled || pressed; }

    bool clipsHitTest() const override { return true; }
    void hitClipRect(float& outX, float& outY, float& outW, float& outH) const override;
    void releaseRenderEntries(UIRenderer& renderer) override;

    // Geometry / interaction (used by the hit-test absorb + UIModule's tab-click handling).
    bool pointInBounds(float x, float y) const;
    bool pointInTabBar(float x, float y) const;
    int tabAt(float x) const;                 // tab index at screen x within the bar; clamped to [0,N-1]
    int activeIndex() const { return m_activeIndex; }
    void setActiveIndex(int i);               // clamped to a valid page

    // Properties (data-driven via UITree).
    std::vector<std::string> tabLabels;
    float tabBarHeight = 30.0f;
    uint32_t bgColor = 0x232a31FF;
    uint32_t activeTabColor = 0x3a6ea5FF;
    uint32_t inactiveTabColor = 0x2c3540FF;
    uint32_t labelColor = 0xFFFFFFFF;
    float fontSize = 14.0f;
    float padding = 6.0f;

    // 9-slice FRAMES — see UIFrame. `frame` dresses the CONTENT background (below the tab strip);
    // `tabFrame` dresses EACH TAB in the strip.
    //
    // ONE tabFrame asset serves both states: it is emitted TINTED by activeTabColor /
    // inactiveTabColor, exactly like UIList::rowFrame rides the selected/hovered/zebra colour and a
    // button's frame rides its state colour. That keeps active/inactive working with art for free —
    // no second asset to author and keep in sync, and a tab switch just re-tints.
    UIFrame frame;
    UIFrame tabFrame;
    uint32_t m_frameId = 0;      // 9-slice entry — registered ONLY if a frame is ever active
    bool m_frameRegistered = false;

private:
    void contentRect(float& outX, float& outY, float& outW, float& outH) const;

    int m_activeIndex = 0;
    int m_lastRenderedActive = -1;            // to purge the previously-shown page on a switch
    bool m_entriesRegistered = false;
    uint32_t m_tabBarBgId = 0;
    std::vector<uint32_t> m_tabRectIds;       // per-tab button rect
    std::vector<uint32_t> m_tabLabelIds;      // per-tab label
    std::vector<uint32_t> m_tabFrameIds;      // per-tab 9-slice entries — allocated ONLY if tabFrame is set
};

} // namespace grove
