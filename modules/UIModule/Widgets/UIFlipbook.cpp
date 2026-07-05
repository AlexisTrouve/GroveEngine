#include "UIFlipbook.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"

namespace grove {

void UIFlipbook::update(UIContext& ctx, float deltaTime) {
    // QUOI : avancer l'horloge d'animation. POURQUOI : le Flipbook est son propre modèle temporel
    // (pas d'AnimationPlayer) — on lui fournit juste un temps monotone ; loop/one-shot sont gérés
    // dans entryAt(). COMMENT : n'accumule que si `playing` ; puis tick des enfants (aucun en MVP,
    // mais on garde le contrat container).
    if (playing) m_time += deltaTime;
    updateChildren(ctx, deltaTime);
}

void UIFlipbook::render(UIRenderer& renderer) {
    // Register with renderer on first render (retained mode) — même patron que UIImage.
    if (!m_registered) {
        m_renderId = renderer.registerEntry();
        m_registered = true;
        // Set destroy callback to unregister (ghost-rect fix).
        setDestroyCallback([&renderer](uint32_t id) {
            renderer.unregisterEntry(id);
        });
    }

    int layer = renderer.nextLayer();

    // QUOI : résoudre la cellule courante en UV et la publier. POURQUOI : c'est l'animation — la
    // cellule affichée change avec m_time ; updateSpriteUV est retained (ne republie que sur
    // changement d'UV/position/couleur). COMMENT : uvAt mappe frameAt(m_time) -> rect UV de la grille.
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    book.uvAt(m_time, sheet, u0, v0, u1, v1);
    renderer.updateSpriteUV(m_renderId, absX, absY, width, height, textureId, u0, v0, u1, v1, tintColor, layer);

    // Render children on top (aucun en MVP, mais garde le contrat de rendu).
    renderChildren(renderer);
}

} // namespace grove
