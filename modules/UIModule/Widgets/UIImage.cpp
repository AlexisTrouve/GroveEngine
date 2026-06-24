#include "UIImage.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"

namespace grove {

void UIImage::update(UIContext& ctx, float deltaTime) {
    // Images don't have interactive behavior
    // Update children if any
    updateChildren(ctx, deltaTime);
}

void UIImage::render(UIRenderer& renderer) {
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

    // TODO: Implement proper UV mapping and scale modes in UIRenderer
    // For now, all scale modes use the same rendering (stretch to bounds).
    // A streamed asset id wins over the numeric textureId (renderer resolves it via the AssetManager).
    if (!assetId.empty()) {
        renderer.updateSprite(m_renderId, absX, absY, width, height, assetId, tintColor, layer);
    } else {
        renderer.updateSprite(m_renderId, absX, absY, width, height, textureId, tintColor, layer);
    }

    // Render children on top
    renderChildren(renderer);
}

} // namespace grove
