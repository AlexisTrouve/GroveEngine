#include "UIContext.h"
#include "UIWidget.h"
#include "../Widgets/UIButton.h"   // seul type concret encore requis ici :
                                   // updateHoverState appelle onMouseEnter/onMouseLeave,
                                   // qui n'existent que sur le bouton (hors perimetre S1a).
#include <spdlog/spdlog.h>

namespace grove {

/**
 * @brief Perform hit testing to find the topmost widget at a point
 *
 * Recursively searches the widget tree from front to back (reverse order)
 * to find the topmost visible widget containing the point.
 *
 * @param widget Root widget to search from
 * @param x Point X coordinate
 * @param y Point Y coordinate
 * @return Topmost widget at point, or nullptr
 */
UIWidget* hitTest(UIWidget* widget, float x, float y) {
    if (!widget || !widget->visible) {
        return nullptr;
    }

    // A clipping container (scroll panel, window) hides its children outside its clip rect — a point
    // outside it can't hit them, so skip the whole subtree. Mirrors the visual scissor (2a). The clip
    // rect is the widget's bounds by default, or a custom region (a window clips below its titlebar).
    bool descend = true;
    if (widget->clipsHitTest()) {
        float cx, cy, cw, ch;
        widget->hitClipRect(cx, cy, cw, ch);
        descend = (x >= cx && x <= cx + cw && y >= cy && y <= cy + ch);
    }

    // Check children first (front to back = reverse order for hit testing)
    if (descend) {
        for (auto it = widget->children.rbegin(); it != widget->children.rend(); ++it) {
            UIWidget* hit = hitTest(it->get(), x, y);
            if (hit) {
                return hit;
            }
        }
    }

    // Ce widget prend-il le clic pour lui ?
    //
    // POURQUOI un seul appel la ou il y avait ONZE branches : chaque type avait deja son predicat
    // d'opacite -- containsPoint / pointInWindow / pointInBounds -- soit trois noms pour une seule
    // question. Le nom variait, jamais la semantique. `absorbsPoint` la pose une fois ; le defaut
    // (false = transparent) laisse passer le clic, ce qui est exactement le comportement d'avant
    // pour un widget decoratif absent de la liste.
    if (widget->absorbsPoint(x, y)) {
        return widget;
    }

    return nullptr;
}

/**
 * @brief Update hover state for all widgets in tree
 *
 * Calls onMouseEnter/onMouseLeave for buttons based on hover state.
 *
 * @param widget Root widget
 * @param ctx UI context (unused; kept for signature symmetry)
 * @param hovered The single widget under the cursor this frame (pointer, may be null)
 */
void updateHoverState(UIWidget* widget, UIContext& ctx, UIWidget* hovered) {
    if (!widget) return;
    (void)ctx;

    // Hover is per-WIDGET (pointer), NOT per-id: data-driven repeater instances (e.g. fleet icons) share an
    // empty id, so the old id comparison flagged EVERY id-less button as hovered when one was. Compare the
    // actual hovered pointer; use the button's own isHovered as the prior state.
    if (widget->getType() == "button") {
        UIButton* button = static_cast<UIButton*>(widget);
        const bool isHovered = (widget == hovered);
        if (isHovered && !button->isHovered)      button->onMouseEnter();
        else if (!isHovered && button->isHovered) button->onMouseLeave();
    }

    // Recurse to children
    for (auto& child : widget->children) {
        updateHoverState(child.get(), ctx, hovered);
    }
}

/**
 * @brief Dispatch mouse button event to widget tree
 *
 * Finds the widget under the mouse and delivers the event.
 *
 * @param widget Root widget
 * @param ctx UI context
 * @param button Mouse button (0 = left, 1 = right, 2 = middle)
 * @param pressed true if button pressed, false if released
 * @return Widget that handled the event (for action publishing), or nullptr
 */
UIWidget* dispatchMouseButton(UIWidget* widget, UIContext& ctx, int button, bool pressed) {
    // Le hit-test choisit la cible, la cible se traite elle-meme.
    //
    // QUOI     : router un clic vers le widget sous le pointeur, et dire a l'appelant si UIModule
    //            doit en etre saisi.
    // POURQUOI : ce corps enumerait NEUF types concrets sur ~66 lignes, dont SIX branches
    //            rigoureusement identiques -- `X->onMouseButton(...)` derriere un static_cast. Les
    //            six methodes existaient deja avec la meme signature ; seule la declaration virtuelle
    //            manquait. Ajouter un widget interactif obligeait donc a revenir editer ce fichier,
    //            loin du widget concerne.
    // COMMENT  : deux virtuels sur UIWidget. `onMouseButton` traite (defaut inerte) ; `surfacesClick`
    //            dit quand remonter la cible (defaut : seulement si elle a consomme). Les cas
    //            particuliers -- onglets et modale au press, liste toujours, roue au relachement,
    //            bouton s'il a de quoi emettre -- vivent desormais chacun dans SON widget, en une
    //            ligne. Les widgets decoratifs heritent des defauts et n'ecrivent rien.
    //
    // Ce qui ne bouge PAS : les widgets n'ont toujours aucun acces a l'IIO. Ils signalent, UIModule
    // publie. C'est le partage qui les garde testables sans bus (cf. docs/UI_ARCHITECTURE.md).
    UIWidget* target = hitTest(widget, ctx.mouseX, ctx.mouseY);
    if (!target) return nullptr;

    const bool handled = target->onMouseButton(button, pressed, ctx.mouseX, ctx.mouseY);
    return target->surfacesClick(pressed, handled) ? target : nullptr;
}

} // namespace grove
