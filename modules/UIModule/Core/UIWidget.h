#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <iterator>   // std::next in bringToFront()
#include <map>        // eventBindings (signal -> binding)
#include <utility>    // std::pair (binding templates)
#include "UILayout.h"
#include "../Rendering/UIRenderer.h"   // releaseRenderEntries() calls renderer.unregisterEntry()

namespace grove {

class UIContext;

// Callback for when widget is destroyed (to notify renderer)
using WidgetDestroyCallback = std::function<void(uint32_t renderId)>;

/**
 * @brief Base interface for all UI widgets
 *
 * Retained-mode UI widget with hierarchical structure.
 * Each widget has position, size, visibility, and can have children.
 */
class UIWidget {
public:
    virtual ~UIWidget() {
        // Notify renderer to remove this widget's render entries
        if (m_renderId != 0 && m_destroyCallback) {
            m_destroyCallback(m_renderId);
        }
    }

    /**
     * @brief Update widget state
     * @param ctx UI context with input state
     * @param deltaTime Time since last update
     */
    virtual void update(UIContext& ctx, float deltaTime) = 0;

    /**
     * @brief Render widget via UIRenderer
     * @param renderer Renderer that publishes to IIO
     */
    virtual void render(UIRenderer& renderer) = 0;

    /**
     * @brief Get widget type name
     * @return Type string (e.g., "panel", "label", "button")
     */
    virtual std::string getType() const = 0;

    /**
     * @brief Does this widget clip its children to its own bounds for HIT-TESTING?
     *
     * WHY: a clipping container (scroll panel, window) hides children outside its rect — a click
     *      outside the container must not reach them, mirroring the visual scissor (slice 2a). Base
     *      = false (no clip). The hit-test skips a clipper's whole subtree when the point is outside
     *      [absX, absY, width, height]. Pairs with the visual clip pushed in render().
     */
    virtual bool clipsHitTest() const { return false; }

    /**
     * @brief Ce widget ABSORBE-t-il un clic en (x,y) ? (hit-test)
     *
     * QUOI     : le prédicat d'opacité au pointeur. Le hit-test descend d'abord dans les enfants ;
     *            en remontant, il demande à chaque widget s'il prend le clic pour lui.
     * POURQUOI : `hitTest` énumérait ONZE types concrets pour appeler onze prédicats qui existaient
     *            déjà — `containsPoint`, `pointInWindow`, `pointInBounds` — sous trois noms
     *            différents pour une seule question. Le nom variait, pas la sémantique.
     * COMMENT  : défaut `false` = transparent au clic, ce qui laisse passer vers ce qui est derrière.
     *            C'est le bon défaut : un widget décoratif (panneau, libellé, image) ne doit rien
     *            intercepter. Les widgets opaques délèguent à leur prédicat existant en une ligne.
     */
    virtual bool absorbsPoint(float /*x*/, float /*y*/) const { return false; }

    /**
     * @brief Handle a mouse button press/release that hit this widget.
     * @return true if the widget CONSUMED the event (it reacted to it).
     *
     * QUOI     : le point d'entrée souris d'un widget. Le hit-test choisit la cible, ceci la traite.
     * POURQUOI : six widgets écrivaient déjà cette méthode avec CETTE signature exacte, sans qu'elle
     *            soit virtuelle — le routeur devait donc connaître chaque type concret et faire un
     *            `static_cast` pour l'appeler. L'interface existait, elle n'avait jamais été déclarée.
     * COMMENT  : défaut inerte (`false`) — un widget purement décoratif (label, image, panel) ne
     *            consomme rien et n'a rien à écrire.
     *
     * ⚠️ Un widget interactif qui OUBLIE le `override` devient silencieusement inerte : ce défaut ne
     *    peut pas le signaler. Ce sont les E2E qui cliquent réellement dessus qui l'attrapent.
     */
    virtual bool onMouseButton(int /*button*/, bool /*pressed*/, float /*x*/, float /*y*/) {
        return false;
    }

    /**
     * @brief Le module doit-il être saisi de ce clic (pour publier un événement / arbitrer) ?
     *
     * QUOI     : sépare « j'ai réagi » (onMouseButton) de « le module a quelque chose à faire ».
     * POURQUOI : les widgets n'ont PAS accès à l'IIO — c'est un choix d'archi (UI_ARCHITECTURE.md),
     *            pas un oubli : c'est ce qui les rend testables sans bus. Le widget ne publie donc
     *            jamais lui-même ; il signale, et UIModule publie. Trois widgets veulent être remontés
     *            SANS avoir consommé (tabs/modal au press, list toujours), d'où un second prédicat
     *            plutôt qu'un `bool` surchargé de deux sens.
     * COMMENT  : défaut = « seulement si j'ai consommé », qui couvre slider/checkbox/saisies.
     */
    virtual bool surfacesClick(bool /*pressed*/, bool handled) const { return handled; }

    /**
     * @brief The rect (screen px) a clipping widget clips its CHILDREN's hit-test to.
     *
     * WHY: a scroll panel clips to its full bounds, but a window clips to the area BELOW its
     *      titlebar (the content region). Default = full bounds; clippers that differ override it.
     *      Only consulted when clipsHitTest() is true. Mirrors the visual clip pushed in render().
     */
    virtual void hitClipRect(float& outX, float& outY, float& outW, float& outH) const {
        outX = absX; outY = absY; outW = width; outH = height;
    }

    /**
     * @brief Publish render:*:remove for this widget's retained entries and RESET its registration,
     *        so a later re-show re-registers and re-publishes :add.
     *
     * WHY: a widget set visible=false stops calling render(), so without this its last-published
     *      retained entries linger on screen ("ghost rects"). Called when the widget becomes hidden
     *      (ui:set_visible false, or a self-close like the radial). Recurses to children — hiding a
     *      parent hides the whole subtree. Multi-entry widgets (button text, radial items) OVERRIDE
     *      to release their EXTRA ids too, then call this base (which drops the primary m_renderId).
     */
    virtual void releaseRenderEntries(UIRenderer& renderer) {
        if (m_renderId != 0) { renderer.unregisterEntry(m_renderId); m_renderId = 0; }
        m_registered = false;
        m_geometryDirty = true;   // re-show -> render() re-registers + re-publishes :add
        m_appearanceDirty = true;
        for (auto& child : children) child->releaseRenderEntries(renderer);
    }

    // Identity
    std::string id;
    std::string tooltip;  // Tooltip text (empty = no tooltip)

    // Position and size (relative to parent)
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool visible = true;

    // Relative (percent) sizing — UI framework slice 1.1.
    // QUOI : taille exprimée en FRACTION du conteneur parent sur un axe (0 = inactif, on
    //   utilise width/height absolus ; >0 = fraction du content-box parent). La racine a pour
    //   "parent" le viewport (screenWidth/Height), d'où "widthPercent:1.0" = remplir l'écran.
    // POURQUOI : c'est la brique du reflow — résolue À CHAQUE passe de layout, donc un widget
    //   en % suit automatiquement son parent quand la fenêtre est redimensionnée (ui:resize).
    // COMMENT : résolu dans UILayout (axe principal = fixe prélevé avant le flex ; axe croisé /
    //   stack / absolute = fraction directe) et, pour la racine, dans UIModule::relayoutRoot().
    float widthPercent = 0.0f;
    float heightPercent = 0.0f;

    // Anchoring — UI framework slice 1.2.
    // QUOI : épingle ce widget à un point du content-box parent (coin/bord/centre) + un offset px.
    // POURQUOI : un élément de HUD reste collé à son coin quand la fenêtre (le parent) est
    //   redimensionnée — la position est dérivée du box parent à chaque passe, pas figée en x/y.
    // COMMENT : résolu dans la branche ABSOLUTE de UILayout via resolveAnchor() ; None = legacy
    //   (on garde x/y explicites). En flow (vertical/horizontal) l'ancre est ignorée (le flux place).
    Anchor anchor = Anchor::None;
    float anchorOffsetX = 0.0f;
    float anchorOffsetY = 0.0f;

    // Layout properties (Phase 2)
    LayoutProperties layoutProps;

    // ---- Data-binding & declarative events (JSON-UI templating engine — grove::uibind) ----
    // QUOI : bindings = props liées à des chemins de données par {{path}} ; eventBindings = events
    //   déclaratifs (signal -> {nom, args}). POURQUOI : c'est la moitié WIDGET du moteur data-driven —
    //   UIModule résout les bindings contre le scope du widget et applique via applyBoundProp ; il publie
    //   un eventBinding (args interpolés au scope) quand le signal arrive. COMMENT : remplis au parse
    //   (UITree::parseBindings) ; aucun couplage json ici (juste des chaînes de template).
    struct EventBinding {
        std::string eventName;                                  // IIO topic to publish (e.g. "fleet:recall")
        std::vector<std::pair<std::string, std::string>> args;  // arg key -> {{}} template
    };
    std::vector<std::pair<std::string, std::string>> bindings;  // property name -> {{}} template
    std::map<std::string, EventBinding> eventBindings;          // signal ("click") -> binding

    // Repeater (step 4): a host with `repeat`+`template` instantiates the template per data-array element.
    //   repeatPath        = the data path the host repeats over (e.g. "fleet"; "" = not a repeater).
    //   repeatTemplateJson= the template subtree serialized as a json string (re-parsed per item at expand).
    //   scopePath         = THIS widget's data scope (e.g. "fleet.0"); "" = the root scope. Bindings AND
    //                       declarative events resolve {{path}} against this scope — so a row's widgets see
    //                       their item's data, and a row button's event carries that item for free.
    std::string repeatPath;
    std::string repeatTemplateJson;
    std::string scopePath;

    // Conditional (step 5): `"if":"{{flag}}"` — the widget renders only when the bound bool is true. When
    // it goes false the subtree is hidden AND its retained entries are released (no ghost). "" = no condition.
    std::string ifBinding;

    // Apply a resolved bound value to property `prop`. UIModule passes it pre-resolved as string / number /
    // bool; the widget picks the form it needs. Base handles common geometry/visibility; widgets OVERRIDE
    // for their own props (label: text, progressbar: value), then call this base for the rest.
    virtual void applyBoundProp(const std::string& prop, const std::string& s, double n, bool b) {
        if (prop == "visible")     visible = b;
        else if (prop == "x")      x = static_cast<float>(n);
        else if (prop == "y")      y = static_cast<float>(n);
        else if (prop == "width")  width = static_cast<float>(n);
        else if (prop == "height") height = static_cast<float>(n);
        else if (prop == "tooltip") tooltip = s;   // data-driven hover text (e.g. a repeater item's {{name}})
        (void)s;
    }

    // Hierarchy
    UIWidget* parent = nullptr;
    std::vector<std::unique_ptr<UIWidget>> children;

    // Computed absolute position (after layout)
    float absX = 0.0f;
    float absY = 0.0f;

    /**
     * @brief Compute absolute position from parent chain
     */
    void computeAbsolutePosition() {
        if (parent) {
            absX = parent->absX + x;
            absY = parent->absY + y;
        } else {
            absX = x;
            absY = y;
        }
        for (auto& child : children) {
            child->computeAbsolutePosition();
        }
    }

    /**
     * @brief Add a child widget
     * @param child Widget to add
     */
    void addChild(std::unique_ptr<UIWidget> child) {
        child->parent = this;
        children.push_back(std::move(child));
    }

    /**
     * @brief Raise this widget above its siblings (z-order — UI framework slice 3a).
     *
     * WHY: an in-app window clicked should come to the FRONT. We model z-order by sibling ORDER —
     *      a widget rendered last gets the highest layers (drawn on top) and the reverse-order
     *      hit-test finds it first (topmost wins input). So "bring to front" = move to the END of
     *      the parent's children. One move covers both render order and hit-test priority.
     * HOW: find self in parent->children, move the owning unique_ptr to the back (preserving the
     *      others' relative order). No-op if there's no parent or it's already last.
     */
    void bringToFront() {
        if (!parent) return;
        auto& sibs = parent->children;
        for (auto it = sibs.begin(); it != sibs.end(); ++it) {
            if (it->get() == this) {
                if (std::next(it) == sibs.end()) return;  // already frontmost
                std::unique_ptr<UIWidget> self = std::move(*it);
                sibs.erase(it);
                sibs.push_back(std::move(self));
                return;
            }
        }
    }

    /**
     * @brief Find widget by ID recursively
     * @param targetId ID to search for
     * @return Widget pointer or nullptr
     */
    UIWidget* findById(const std::string& targetId) {
        if (id == targetId) {
            return this;
        }
        for (auto& child : children) {
            if (UIWidget* found = child->findById(targetId)) {
                return found;
            }
        }
        return nullptr;
    }

protected:
    /**
     * @brief Update all children
     */
    void updateChildren(UIContext& ctx, float deltaTime) {
        for (auto& child : children) {
            if (child->visible) {
                child->update(ctx, deltaTime);
            }
        }
    }

    /**
     * @brief Render all children
     */
    void renderChildren(UIRenderer& renderer) {
        for (auto& child : children) {
            if (child->visible) {
                child->render(renderer);
            }
        }
    }

    // ========================================================================
    // Retained Mode / Dirty Tracking
    // ========================================================================
protected:
    uint32_t m_renderId = 0;           // Unique ID for render system (0 = not registered)
    bool m_geometryDirty = true;       // Position/size changed, needs re-render
    bool m_appearanceDirty = true;     // Color/style changed, needs re-render
    bool m_registered = false;         // Has been registered with renderer
    WidgetDestroyCallback m_destroyCallback;  // Called on destruction

public:
    /**
     * @brief Get render ID (0 if not registered)
     */
    uint32_t getRenderId() const { return m_renderId; }

    /**
     * @brief Set render ID (called by UIRenderer on registration)
     */
    void setRenderId(uint32_t id) { m_renderId = id; }

    /**
     * @brief Check if widget needs re-rendering
     */
    bool isDirty() const { return m_geometryDirty || m_appearanceDirty; }

    /**
     * @brief Check if registered with renderer
     */
    bool isRegistered() const { return m_registered; }

    /**
     * @brief Mark as registered
     */
    void setRegistered(bool reg) { m_registered = reg; }

    /**
     * @brief Mark geometry as dirty (position, size changed)
     */
    void markGeometryDirty() {
        m_geometryDirty = true;
    }

    /**
     * @brief Mark appearance as dirty (color, style changed)
     */
    void markAppearanceDirty() {
        m_appearanceDirty = true;
    }

    /**
     * @brief Clear dirty flags after rendering
     */
    void clearDirtyFlags() {
        m_geometryDirty = false;
        m_appearanceDirty = false;
    }

    /**
     * @brief Set callback for widget destruction
     */
    void setDestroyCallback(WidgetDestroyCallback callback) {
        m_destroyCallback = std::move(callback);
    }
};

} // namespace grove
