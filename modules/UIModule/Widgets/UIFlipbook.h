#pragma once

#include "../Core/UIWidget.h"
#include <grove/anim/Flipbook.h>
#include <grove/anim/SpriteSheet.h>
#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Animated sprite-sheet (flipbook) panel widget — UI framework slice 6a.
 *
 * QUOI  : Un widget-feuille qui joue une animation image-par-image (sprite sheet). Il tient une grille
 *         SpriteSheet (columns x rows) + un Flipbook (ordre des cellules + timing + loop) et fait
 *         défiler la cellule affichée au fil du temps, publiée en `render:sprite` avec l'UV de la
 *         cellule courante.
 * POURQUOI : c'est la brique "scène animée 2D" de l'ask UI de Drifterra. `grove::anim` (header-only)
 *         porte déjà toute la maths sheet/timing — le widget n'est que le pont retained-render : il
 *         avance une horloge, calcule l'UV, publie. Aucune dépendance renderer/IIO nouvelle (le lien
 *         est header-only, cf. CMakeLists include ../../include).
 * COMMENT : update(dt) avance m_time tant que `playing`. render() résout uvAt(m_time) -> (u0,v0,u1,v1)
 *         et appelle UIRenderer::updateSpriteUV — retained : ne republie que quand la cellule change.
 *         Non-interactif (pas de hit-test). MVP : sheet uniforme, frames = 0..frameCount-1 ; un ordre
 *         de frames custom / un topic play-pause sont des follow-ons.
 */
class UIFlipbook : public UIWidget {
public:
    UIFlipbook() = default;
    ~UIFlipbook() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "flipbook"; }

    // Texture source: numeric id (une texture-sheet dédiée). MVP = numérique seulement ; une sheet en
    // `asset` streamé demanderait de composer cell-UV × atlas-UV — reporté.
    int textureId = 0;
    uint32_t tintColor = 0xFFFFFFFF;  // RGBA tint (white = no tint)

    // Sheet geometry + playback timing (grove::anim). Construits par la factory UITree "flipbook".
    anim::SpriteSheet sheet;   // grille columns x rows -> UV de cellule
    anim::Flipbook book;       // ordre des frames + durées par frame + loop

    bool playing = true;       // avance l'horloge (un futur ui:anim:set pourra le basculer)

private:
    float m_time = 0.0f;       // secondes écoulées ; enroulé par Flipbook::loop
};

} // namespace grove
