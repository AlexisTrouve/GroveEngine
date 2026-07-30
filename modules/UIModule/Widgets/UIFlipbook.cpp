#include <grove/IDataNode.h>
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


// Panneau anime sur planche de sprites (tranche 6a).
// QUOI : parse la geometrie de planche (columns/rows/count) + le timing (fps/loop) et construit un
//   grove::anim::SpriteSheet + Flipbook.
// POURQUOI : le widget ne fait que JOUER ; toute la maths planche/timing vit dans grove::anim
//   (header-only, reutilisable, teste).
// COMMENT : frames = 0..frameCount-1 (ordre naturel de la grille ; un ordre custom via un tableau
//   scalaire est un follow-on, le parse de tableau scalaire IIO etant un piege connu) ; setFps()
//   remplit les durees.
std::unique_ptr<UIWidget> UIFlipbook::fromNode(const IDataNode& node) {
    auto fb = std::make_unique<UIFlipbook>();
    fb->textureId = node.getInt("textureId", 0);

    // Sheet geometry.
    fb->sheet.columns = node.getInt("columns", 1);
    fb->sheet.rows    = node.getInt("rows", 1);
    fb->sheet.count   = node.getInt("count", 0);   // 0 => grille pleine columns*rows

    // Timing : loop + fps (durées uniformes). Frames = ordre naturel 0..frameCount-1.
    fb->book.loop = node.getBool("loop", true);
    const int frameCount = fb->sheet.frameCount();
    fb->book.frames.clear();
    fb->book.frames.reserve(static_cast<size_t>(frameCount));
    for (int i = 0; i < frameCount; ++i) fb->book.frames.push_back(i);
    fb->book.setFps(static_cast<float>(node.getDouble("fps", 12.0)));

    auto& mutableNode = const_cast<IDataNode&>(node);
    if (auto* style = mutableNode.getChildReadOnly("style")) {
        std::string tintStr = style->getString("tintColor", "0xFFFFFFFF");
        if (tintStr.size() >= 2 && (tintStr.substr(0, 2) == "0x" || tintStr.substr(0, 2) == "0X")) {
            fb->tintColor = static_cast<uint32_t>(std::stoul(tintStr, nullptr, 16));
        }
    }

    return fb;
}

} // namespace grove
