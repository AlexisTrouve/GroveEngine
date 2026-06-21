#pragma once

#include "../Core/UIWidget.h"
#include <cstdint>
#include <string>
#include <vector>

namespace grove {

// ============================================================================
// UIRadial — action-wheel / radial menu widget.
//
// QUOI   : un menu circulaire de N segments ; on en choisit un par DIRECTION.
// POURQUOI : c'est le "quoi" de la navigation (cf. INTERFACE.md de Drifterra :
//   "zoom = où, roue = quoi"). La sélection angulaire (vs rectangulaire) est ce qui
//   la rend input-agnostique : souris / stick manette / clavier pilotent le même
//   modèle (un index de segment). On construit souris d'abord ; setSelectedIndex()
//   est la couture pour brancher manette/clavier plus tard (hooks tôt, polish tard).
// COMMENT : centré sur (x,y) — PAS coin haut-gauche comme les widgets rect. La géo et
//   la sélection vivent dans RadialMath.h (pur, testé). Le widget émet ui:action au release ;
//   UIModule l'AUTO-FERME ensuite (menu modal) en le cachant — sûr depuis que
//   releaseRenderEntries() purge les entrées retained d'un widget caché (plus de rects
//   fantômes). Le jeu le (re)positionne sur le curseur via ui:set_position.
// ============================================================================

// Un segment sélectionnable de la roue.
struct RadialItem {
    std::string action;    // publié sur ui:action quand ce segment est confirmé
    std::string text;      // libellé dessiné sur le segment
    int textureId = 0;     // icône optionnelle (0 = aucune, texte seul)
};

// Palette + géométrie fine de la roue (RGBA 0xRRGGBBAA).
struct RadialStyle {
    uint32_t bgColor    = 0x000000A0;  // disque de fond (semi-transparent, sert de "gaps" sombres)
    uint32_t itemColor  = 0x34495EFF;  // un segment au repos
    uint32_t hoverColor = 0x2ECC71FF;  // le segment sous le pointeur
    uint32_t textColor  = 0xFFFFFFFF;
    float    fontSize   = 16.0f;
    // Marges des parts (en pixels) : `gap` = espace ANGULAIRE entre tranches voisines (mesuré comme
    // une longueur d'arc au rayon intérieur) ; `margin` = retrait RADIAL de chaque part vis-à-vis des
    // bords inner/outer de la tarte. Le disque de fond apparaît dans ces marges -> séparateurs sombres.
    float    gap        = 4.0f;
    float    margin     = 3.0f;
};

class UIRadial : public UIWidget {
public:
    UIRadial() = default;
    ~UIRadial() override = default;

    void update(UIContext& ctx, float deltaTime) override;
    void render(UIRenderer& renderer) override;
    std::string getType() const override { return "radial"; }

    // Release the bg + per-item retained entries on hide (so the wheel can auto-close without leaving
    // ghost rects); resets so a re-show re-registers. Overrides the base to drop the item ids too.
    void releaseRenderEntries(UIRenderer& renderer) override;

    // Zone interactive = tout le disque (rayon <= outerRadius) : un clic n'importe où
    // sur la roue est routé ici ; la dead-zone centrale se résout en "annuler".
    bool containsPoint(float px, float py) const;

    // Comptabilité press/release (calque UIButton). Renvoie true si consommé. Au
    // release, recalcule le segment depuis le point EXACT du clic (indépendant du
    // timing de update(), qui tourne après le dispatch souris).
    bool onMouseButton(int button, bool pressed, float x, float y);

    // Action du segment courant, ou "" si dead-zone / aucun. Lu par UIModule au
    // release pour publier ui:action.
    std::string selectedAction() const;

    // Pilotage depuis une source non-pointeur (stick manette / step clavier) — la
    // couture "hooks tôt" pour la parité d'input, sans toucher rendu/logique.
    void setSelectedIndex(int idx) { m_selectedIndex = idx; }
    int  selectedIndex() const { return m_selectedIndex; }

    // Géométrie (centrée)
    float innerRadius = 40.0f;
    float outerRadius = 160.0f;

    std::vector<RadialItem> items;
    RadialStyle style;

private:
    int  m_selectedIndex = -1;   // recalculé chaque update() depuis l'angle du pointeur
    bool m_pressed = false;      // un press a démarré sur la roue dans cette interaction

    // RETAINED entries: the bg rect (m_renderId) + one text label per item. The wedges are EPHEMERAL
    // sectors (drawSector each frame), so no per-wedge id. Count known at first render.
    std::vector<uint32_t> m_itemTextIds;
    bool m_entriesRegistered = false;
};

} // namespace grove
