#include "UIRadial.h"
#include "RadialMath.h"
#include "../Core/UIContext.h"
#include "../Rendering/UIRenderer.h"
#include <vector>

namespace grove {

void UIRadial::update(UIContext& ctx, float deltaTime) {
    // Recalcule le segment survolé depuis l'offset du pointeur au centre, chaque frame.
    // POURQUOI dans update() : on a ici la position souris courante (ctx) -> le survol
    //   suit le curseur même sans clic, comme une vraie roue. (Le CONFIRM, lui, ne dépend
    //   pas de ça : il est recalculé au point exact du release dans onMouseButton.)
    // COMMENT : absX/absY = CENTRE de la roue (pour le radial, x,y sont le centre).
    const float dx = ctx.mouseX - absX;
    const float dy = ctx.mouseY - absY;
    m_selectedIndex = radial::selectIndex(dx, dy, innerRadius, outerRadius,
                                          static_cast<int>(items.size()));
    updateChildren(ctx, deltaTime);
}

std::string UIRadial::selectedAction() const {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(items.size())) {
        return "";
    }
    return items[m_selectedIndex].action;
}

bool UIRadial::containsPoint(float px, float py) const {
    // Tout le disque est interactif (jusqu'à outerRadius) ; la dead-zone se résout en
    // "annuler" plus haut (selectedAction() renvoie "" dedans).
    const float dx = px - absX;
    const float dy = py - absY;
    return (dx * dx + dy * dy) <= (outerRadius * outerRadius);
}

bool UIRadial::onMouseButton(int button, bool pressed, float x, float y) {
    if (button != 0) return false;  // bouton gauche uniquement (calque UIButton)

    if (pressed) {
        if (containsPoint(x, y)) {
            m_pressed = true;
            return true;
        }
        return false;
    }

    // Release : ne confirme que si le press a démarré sur la roue ET qu'on est encore
    // dessus (mêmes règles qu'un bouton). On recalcule le segment depuis le point EXACT
    // du release pour que le confirm soit indépendant du timing de update().
    if (m_pressed && containsPoint(x, y)) {
        m_pressed = false;
        m_selectedIndex = radial::selectIndex(x - absX, y - absY,
                                              innerRadius, outerRadius,
                                              static_cast<int>(items.size()));
        return true;
    }
    m_pressed = false;
    return false;
}

void UIRadial::render(UIRenderer& renderer) {
    const int n = static_cast<int>(items.size());

    // Enregistre les entrées retained une fois le nombre d'items connu : 1 fond +
    // n*(rect + texte). On câble m_renderId sur le fond pour que le destructeur de base
    // (qui teste m_renderId != 0) déclenche notre callback de nettoyage groupé.
    if (!m_entriesRegistered) {
        m_renderId = renderer.registerEntry();   // fond (sert aussi de déclencheur destroy)
        m_itemRectIds.resize(n);
        m_itemTextIds.resize(n);
        for (int i = 0; i < n; ++i) {
            m_itemRectIds[i] = renderer.registerEntry();
            m_itemTextIds[i] = renderer.registerEntry();
        }
        m_entriesRegistered = true;

        // Désenregistre TOUTES nos entrées à la destruction (calque UIButton qui nettoie
        // son bg + son texte). On capture les ids par valeur ; &renderer vit aussi
        // longtemps que le module (comme pour UIButton).
        std::vector<uint32_t> ids = m_itemRectIds;
        ids.insert(ids.end(), m_itemTextIds.begin(), m_itemTextIds.end());
        setDestroyCallback([&renderer, ids](uint32_t bgId) {
            renderer.unregisterEntry(bgId);                 // le fond (= m_renderId)
            for (uint32_t e : ids) renderer.unregisterEntry(e);
        });
    }

    // Fond : un carré couvrant la bounding-box de la roue (render:rect est axis-aligned ;
    // pas de primitive disque). absX/absY = centre -> coin haut-gauche = centre - rayon.
    const int bgLayer = renderer.nextLayer();
    const float diameter = outerRadius * 2.0f;
    renderer.updateRect(m_renderId, absX - outerRadius, absY - outerRadius,
                        diameter, diameter, style.bgColor, bgLayer);

    // Items sur l'anneau médian, item 0 en haut, sens horaire. Le segment survolé prend
    // hoverColor (m_selectedIndex est rafraîchi chaque update()).
    const float ring = (innerRadius + outerRadius) * 0.5f;
    const float box  = (outerRadius - innerRadius) * 0.9f;   // tuile carrée du segment
    for (int i = 0; i < n; ++i) {
        float ox, oy;
        radial::itemOffset(i, n, ring, ox, oy);
        const float cx = absX + ox;
        const float cy = absY + oy;
        const uint32_t col = (i == m_selectedIndex) ? style.hoverColor : style.itemColor;

        const int rectLayer = renderer.nextLayer();
        renderer.updateRect(m_itemRectIds[i], cx - box * 0.5f, cy - box * 0.5f,
                            box, box, col, rectLayer);

        if (!items[i].text.empty()) {
            const int textLayer = renderer.nextLayer();
            // Texte ancré au centre du segment (calque UIButton qui passe le centre).
            renderer.updateText(m_itemTextIds[i], cx, cy, items[i].text,
                                style.fontSize, style.textColor, textLayer);
        }
    }

    renderChildren(renderer);
}

} // namespace grove
