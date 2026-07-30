#include <grove/IDataNode.h>
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

    // 9-slice CHROME: an authored `frame` REPLACES the flat bg/texture entirely (the two would otherwise
    // co-draw). The entry is registered LAZILY — a panel without a frame must allocate no retained entry
    // and no extra layer, which is what keeps this addition free for the plain panels that make up the
    // bulk of a real layout. The destroy callback is re-set here so the frame entry is released too.
    if (frame.active()) {
        if (!m_frameRegistered) {
            m_frameId = renderer.registerEntry();
            m_frameRegistered = true;
            setDestroyCallback([&renderer, fr = m_frameId](uint32_t id) {
                renderer.unregisterEntry(id);
                renderer.unregisterEntry(fr);
            });
        }
        frame.emit(renderer, m_frameId, absX, absY, width, height, tintColor, layer);
        renderer.updateRect(m_renderId, 0, 0, 0, 0, 0, renderer.nextLayer());   // flat bg idle
        renderChildren(renderer);
        return;
    }

    // Check if fully transparent (alpha channel = 0)
    bool isFullyTransparent = (bgColor & 0xFF) == 0;

    // Render background (texture or solid color) - skip if fully transparent
    if (useTexture && textureId > 0) {
        renderer.updateSprite(m_renderId, absX, absY, width, height, textureId, tintColor, layer);
    } else if (!isFullyTransparent) {
        renderer.updateRect(m_renderId, absX, absY, width, height, bgColor, layer);
    }

    // A frame that was turned off at runtime must not ghost under the flat look. Only ever reached by a
    // panel that DID carry one — a never-framed panel skips this and keeps its single-layer cost.
    if (m_frameRegistered) {
        UIFrame::collapse(renderer, m_frameId, renderer.nextLayer());
    }

    // Render children on top
    renderChildren(renderer);
}

void UIPanel::releaseRenderEntries(UIRenderer& renderer) {
    if (m_frameId != 0) { renderer.unregisterEntry(m_frameId); m_frameId = 0; }
    m_frameRegistered = false;   // lazily re-registered on the next render that needs it
    UIWidget::releaseRenderEntries(renderer);   // drops m_renderId (bg) + recurses to children
}


std::unique_ptr<UIWidget> UIPanel::fromNode(const IDataNode& node) {
    auto panel = std::make_unique<UIPanel>();

    // Parse style (const_cast safe for read-only operations)
    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* style = mutableNode.getChildReadOnly("style")) {
        std::string bgColorStr = style->getString("bgColor", "0x333333FF");
        if (bgColorStr.size() >= 2 && (bgColorStr.substr(0, 2) == "0x" || bgColorStr.substr(0, 2) == "0X")) {
            panel->bgColor = static_cast<uint32_t>(std::stoul(bgColorStr, nullptr, 16));
        }
        panel->borderRadius = static_cast<float>(style->getDouble("borderRadius", 0.0));
    }

    // 9-slice FRAME (optional `frame` block) — same authoring as button/window, see UIFrame.
    if (auto* frame = mutableNode.getChildReadOnly("frame")) panel->frame.parse(*frame);

    return panel;
}

} // namespace grove
