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
    // Render the texture
    // For now, use the simple sprite rendering
    // TODO: Implement proper UV mapping and scale modes in UIRenderer

    if (scaleMode == ScaleMode::Stretch || scaleMode == ScaleMode::None) {
        // Simple case: render sprite at widget bounds
        renderer.drawSprite(absX, absY, width, height, textureId, tintColor);
    } else {
        // For Fit/Fill modes, we'd need to calculate proper dimensions
        // based on texture aspect ratio. For now, just stretch.
        renderer.drawSprite(absX, absY, width, height, textureId, tintColor);
    }

    // Render children on top
    renderChildren(renderer);
}

} // namespace grove
