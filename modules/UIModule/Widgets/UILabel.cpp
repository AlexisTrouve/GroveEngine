#include "UILabel.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"

namespace grove {

void UILabel::update(UIContext& ctx, float deltaTime) {
    // Labels are static, no update needed
    // Future: could support animated text or data binding
}

void UILabel::render(UIRenderer& renderer) {
    if (!text.empty()) {
        renderer.drawText(absX, absY, text, fontSize, color);
    }
}

} // namespace grove
