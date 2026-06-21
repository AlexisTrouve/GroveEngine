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

    // RETAINED entries: the bg rect (m_renderId, also the base-destructor trigger) + one TEXT label per
    // item. The wedges themselves are EPHEMERAL pie SECTORS (drawSector below), re-published each frame
    // — they change with the hover and vanish on their own when the wheel hides, so no retained id.
    if (!m_entriesRegistered) {
        m_renderId = renderer.registerEntry();   // fond (sert aussi de déclencheur destroy)
        m_itemTextIds.resize(n);
        for (int i = 0; i < n; ++i) m_itemTextIds[i] = renderer.registerEntry();   // labels
        m_entriesRegistered = true;

        // Unregister our retained entries (bg + labels) on destruction.
        std::vector<uint32_t> ids = m_itemTextIds;
        setDestroyCallback([&renderer, ids](uint32_t bgId) {
            renderer.unregisterEntry(bgId);                 // le fond (= m_renderId)
            for (uint32_t e : ids) renderer.unregisterEntry(e);
        });
    }

    // Background DISC: a full ring-sector (r0=0) at bgColor — circular, no ugly square. Ephemeral like
    // the wedges; drawn at the lowest layer so the wedges + labels sit on top, and the centre dead-zone
    // shows the dark disc. (m_renderId is no longer drawn — it stays a registered phantom so the base
    // destructor's `m_renderId != 0` trigger + the hide-purge still fire for the retained labels.)
    const int bgLayer = renderer.nextLayer();
    renderer.drawSector(absX, absY, 0.0f, outerRadius, 0.0f, radial::kTwoPi, style.bgColor, bgLayer);

    // Items as pie WEDGES (filled ring-sectors), item 0 at top, clockwise. The hovered segment takes
    // hoverColor (m_selectedIndex is refreshed every update()). The wedge for item i is the angular
    // slice centred on item i's direction. selectIndex measures angle as "0 = top, clockwise"; the
    // sector primitive measures cos/sin from +x, so the sector angle a = theta - pi/2.
    const float ring   = (innerRadius + outerRadius) * 0.5f;
    const float sector = radial::kTwoPi / static_cast<float>(n);
    // Margins (configurable): RADIAL inset from the pie's inner/outer borders, and an ANGULAR gap
    // between adjacent slices (a `gap`-pixel arc at the inner radius). The dark bg disc shows through
    // both -> clean separators. Half the gap is shaved off EACH side of every wedge.
    const float r0 = innerRadius + style.margin;
    const float r1 = (outerRadius - style.margin > r0) ? (outerRadius - style.margin) : r0;
    const float halfGap = (innerRadius > 1e-3f) ? (style.gap * 0.5f / innerRadius) : 0.0f;   // radians
    for (int i = 0; i < n; ++i) {
        const float aCenter = static_cast<float>(i) * sector - radial::kPi * 0.5f;
        const uint32_t col = (i == m_selectedIndex) ? style.hoverColor : style.itemColor;
        const int wedgeLayer = renderer.nextLayer();
        renderer.drawSector(absX, absY, r0, r1,
                            aCenter - sector * 0.5f + halfGap, aCenter + sector * 0.5f - halfGap,
                            col, wedgeLayer);

        // Label at the segment's mid-ring anchor (same convention as the selection).
        if (!items[i].text.empty()) {
            float ox, oy;
            radial::itemOffset(i, n, ring, ox, oy);
            const int textLayer = renderer.nextLayer();
            renderer.updateText(m_itemTextIds[i], absX + ox, absY + oy, items[i].text,
                                style.fontSize, style.textColor, textLayer);
        }
    }

    renderChildren(renderer);
}

void UIRadial::releaseRenderEntries(UIRenderer& renderer) {
    // Drop our EXTRA entries (the per-item rects + texts), then let the base drop the bg (m_renderId)
    // and reset the dirty/registered flags + recurse. Clearing the id vectors + m_entriesRegistered
    // means the next render() (after a re-show) re-registers fresh entries and re-publishes :add.
    for (uint32_t id : m_itemTextIds) if (id != 0) renderer.unregisterEntry(id);
    m_itemTextIds.clear();
    m_entriesRegistered = false;
    UIWidget::releaseRenderEntries(renderer);   // bg (m_renderId) + reset + recurse
}

} // namespace grove
