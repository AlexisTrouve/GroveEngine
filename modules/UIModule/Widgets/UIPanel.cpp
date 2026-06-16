#include "UIPanel.h"
#include "../Core/UIContext.h"
#include "../Core/UILayout.h"
#include "../Rendering/UIRenderer.h"
#include <spdlog/spdlog.h>

namespace grove {

void UIPanel::update(UIContext& ctx, float deltaTime) {
    // Apply layout if this panel has a non-absolute layout mode
    if (layoutProps.mode != LayoutMode::Absolute) {
        // Measure and layout children — this rewrites each child's RELATIVE x/y.
        UILayout::measure(this);
        UILayout::layout(this, width, height);

        // FIX #6 : re-dériver les positions absolues du sous-arbre après le layout.
        // POURQUOI : UILayout ne fixe que les x/y RELATIFS ; absX/absY (utilisés par le
        //   rendu ET le hit-test) ne sont sinon calculés qu'une fois au load — avant que
        //   le layout ne tourne — donc clics et dessin tombaient sur des coordonnées
        //   pré-layout (périmées). C'est exactement pourquoi toutes les fixtures E2E
        //   devaient être en "absolute".
        // COMMENT : notre propre absX est déjà correct ici (posé par la passe d'un
        //   ancêtre ou au load — update() est top-down), donc recomputer depuis `this`
        //   corrige nos enfants ; un panel imbriqué refera sa propre passe à son update.
        computeAbsolutePosition();
    }

    // Update children
    updateChildren(ctx, deltaTime);
}

void UIPanel::render(UIRenderer& renderer) {
    // Register with renderer on first render
    if (!m_registered) {
        m_renderId = renderer.registerEntry();
        m_registered = true;
        // Set destroy callback to unregister
        setDestroyCallback([&renderer](uint32_t id) {
            renderer.unregisterEntry(id);
        });
    }

    // Retained mode: only publish if changed
    int layer = renderer.nextLayer();

    // Check if fully transparent (alpha channel = 0)
    bool isFullyTransparent = (bgColor & 0xFF) == 0;

    // Render background (texture or solid color) - skip if fully transparent
    if (useTexture && textureId > 0) {
        renderer.updateSprite(m_renderId, absX, absY, width, height, textureId, tintColor, layer);
    } else if (!isFullyTransparent) {
        renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, layer);
    }

    // Render children on top
    renderChildren(renderer);
}

} // namespace grove
