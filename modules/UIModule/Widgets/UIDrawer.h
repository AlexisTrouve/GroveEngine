#pragma once

#include "../Core/UIWidget.h"
#include "UIFrame.h"
#include <string>
#include <cstdint>

namespace grove {

/**
 * @brief Edge drawer — a panel docked to a screen edge that SLIDES in/out (UI framework slice 5b).
 *
 * A collapsible "hidden menu" on the top / bottom / left / right edge. It fills the viewport along
 * the edge and is `openExtent` deep; toggling animates it sliding between off-edge (closed) and
 * flush (open) over `slideDuration` (smoothstep). Content is CLIPPED to the drawer rect (slice-2
 * clip) and the drawer is OPAQUE while on screen (absorbs clicks). Toggled via ui:drawer:* topics.
 */
class UIDrawer : public UIWidget {
public:
    enum class Edge { Left, Right, Top, Bottom };

    ~UIDrawer() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "drawer"; }

    // Opaque au clic : delegue au predicat deja present sur ce widget.
    bool absorbsPoint(float x, float y) const override { return pointInBounds(x, y); }
    // Libère le chrome 9-slice PARESSEUX, que la base ignore. Compte aussi pour la purge que render()
    // déclenche quand le tiroir est complètement refermé. Verrouillé par IT_067.
    void releaseRenderEntries(UIRenderer& renderer) override;

    bool clipsHitTest() const override { return true; }   // clips children to the drawer rect (bounds)
    bool pointInBounds(float x, float y) const;

    // Open / close (animated). setOpen drives the slide target; isFullyClosed when off screen.
    void setOpen(bool open) { m_targetOpen = open; }
    bool isOpen() const { return m_targetOpen; }
    bool isFullyClosed() const { return m_t <= 0.0001f && !m_targetOpen; }

    // Properties (data-driven via UITree).
    Edge edge = Edge::Left;
    float openExtent = 250.0f;     // depth perpendicular to the edge (width for L/R, height for T/B)
    float slideDuration = 0.22f;   // seconds for a full open/close slide
    uint32_t bgColor = 0x1e252cF5;

    // 9-slice FRAME — see UIFrame. Dresses the sliding panel itself.
    UIFrame frame;
    uint32_t m_frameId = 0;      // 9-slice entry — registered ONLY if a frame is ever active
    bool m_frameRegistered = false;

private:
    float m_t = 0.0f;              // 0 = fully closed (off edge), 1 = fully open (flush)
    bool m_targetOpen = false;
    bool m_purged = false;         // released entries while fully closed (avoid off-screen ghosts)
};

} // namespace grove
