#include "UIModule.h"
#include "Core/UIContext.h"
#include "Core/UITree.h"
#include "Core/UIWidget.h"
#include "Core/UILayout.h"   // relayoutRoot() drives measure/layout on viewport resize
#include "Core/UITooltip.h"
#include "Core/UIBinding.h"   // {{path}} resolver + Scope (data-binding in / events out)
#include <cmath>              // std::ceil (template-list windowing)
#include <vector>            // repeater / template-list host collection
#include "Rendering/UIRenderer.h"
#include "Widgets/UIButton.h"
#include "Widgets/UISlider.h"
#include "Widgets/UICheckbox.h"
#include "Widgets/UITextInput.h"
#include "Widgets/UITextArea.h"
#include "Core/UITextEditWidget.h"
#include "Widgets/UIScrollPanel.h"
#include "Widgets/UILabel.h"
#include "Widgets/UIRadial.h"
#include "Widgets/UIWindow.h"
#include "Widgets/UITabs.h"
#include "Widgets/UIDrawer.h"
#include "Widgets/UIModal.h"
#include "Widgets/UIList.h"

#include <grove/JsonDataNode.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <chrono>
#include <fstream>
#include <limits>
#include <grove/text/TextMetricsWire.h>  // decode render:font:metrics (avances de la police reelle)
#include <nlohmann/json.hpp>

// Forward declarations for hit testing functions in UIContext.cpp
namespace grove {
    UIWidget* hitTest(UIWidget* widget, float x, float y);
    void updateHoverState(UIWidget* widget, UIContext& ctx, UIWidget* hovered);
    UIWidget* dispatchMouseButton(UIWidget* widget, UIContext& ctx, int button, bool pressed);
}

namespace grove {

namespace {
// QUOI : traduit un SDL_Scancode (valeur brute publiée par InputModule sur
//   input:keyboard:key) vers le code d'édition attendu par UITextInput::onKeyInput
//   (dialecte "JS-like" : Backspace=8, Delete=127, Enter=13, Left=37, Right=39,
//   Home=36, End=35).
// POURQUOI : InputModule est volontairement découplé de l'UI — il publie le scancode
//   SDL tel quel ; UITextInput parle un autre dialecte. Sans cette table, les touches
//   d'édition étaient inertes (#5-suite) : seul le texte imprimable, via le topic
//   input:keyboard:text, fonctionnait.
// COMMENT : table figée des SDL_Scancode (cf. SDL_scancode.h — l'UIModule n'inclut pas
//   les en-têtes SDL, d'où les littéraux commentés). Renvoie 0 si la touche n'a pas de
//   sémantique d'édition : l'appelant n'émet alors aucun keyPressed et laisse le chemin
//   texte gérer les caractères imprimables.
int sdlScancodeToEditKey(int scancode) {
    switch (scancode) {
        case 42: return 8;    // SDL_SCANCODE_BACKSPACE -> Backspace
        case 76: return 127;  // SDL_SCANCODE_DELETE    -> Delete
        case 40: return 13;   // SDL_SCANCODE_RETURN    -> Enter
        case 88: return 13;   // SDL_SCANCODE_KP_ENTER  -> Enter (pavé num.)
        case 80: return 37;   // SDL_SCANCODE_LEFT       -> flèche gauche
        case 79: return 39;   // SDL_SCANCODE_RIGHT      -> flèche droite
        case 74: return 36;   // SDL_SCANCODE_HOME      -> Home
        case 77: return 35;   // SDL_SCANCODE_END       -> End
        // Haut/Bas n'avaient aucun sens tant que la saisie etait monoligne ; le textarea les utilise
        // pour changer de ligne en conservant la colonne.
        case 82: return 38;   // SDL_SCANCODE_UP        -> fleche haut
        case 81: return 40;   // SDL_SCANCODE_DOWN      -> fleche bas
        default: return 0;    // pas une touche d'édition
    }
}
} // namespace

UIModule::UIModule() = default;
UIModule::~UIModule() = default;

void UIModule::setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) {
    m_io = io;

    // Setup logger
    m_logger = spdlog::get("UIModule");
    if (!m_logger) {
        m_logger = spdlog::stdout_color_mt("UIModule");
    }

    m_logger->info("Initializing UIModule");

    // Initialize subsystems
    m_context = std::make_unique<UIContext>();
    m_tree = std::make_unique<UITree>();
    m_renderer = std::make_unique<UIRenderer>(io);
    m_tooltipManager = std::make_unique<UITooltipManager>();

    // Read screen size from config
    m_context->screenWidth = static_cast<float>(config.getInt("windowWidth", 1280));
    m_context->screenHeight = static_cast<float>(config.getInt("windowHeight", 720));

    // Set base UI layer
    int baseLayer = config.getInt("baseLayer", 1000);
    m_renderer->setBaseLayer(baseLayer);

    // Load layout if specified
    std::string layoutFile = config.getString("layoutFile", "");
    if (!layoutFile.empty()) {
        if (loadLayout(layoutFile)) {
            m_logger->info("Loaded layout from: {}", layoutFile);
        } else {
            m_logger->error("Failed to load layout: {}", layoutFile);
        }
    }

    // Check for inline layout data (const_cast safe for read-only operations)
    auto& mutableConfig = const_cast<IDataNode&>(config);
    if (auto* layoutData = mutableConfig.getChildReadOnly("layout")) {
        if (loadLayoutData(*layoutData)) {
            m_logger->info("Loaded inline layout data");
        }
    }

    // Subscribe to input topics with callbacks
    if (m_io) {
        m_io->subscribe("input:mouse:move", [this](const Message& msg) {
            m_context->mouseX = static_cast<float>(msg.data->getDouble("x", 0.0));
            m_context->mouseY = static_cast<float>(msg.data->getDouble("y", 0.0));
        });

        m_io->subscribe("input:mouse:button", [this](const Message& msg) { onInputMouseButton(msg); });

        m_io->subscribe("input:mouse:wheel", [this](const Message& msg) {
            m_context->mouseWheelDelta = static_cast<float>(msg.data->getDouble("delta", 0.0));
        });

        m_io->subscribe("input:keyboard", [this](const Message& msg) { onInputKeyboard(msg); });

        m_io->subscribe("input:keyboard:text", [this](const Message& msg) { onInputKeyboardText(msg); });
        m_io->subscribe("input:keyboard:key", [this](const Message& msg) { onInputKeyboardKey(msg); });

        m_io->subscribe("render:font:metrics", [this](const Message& msg) { onRenderFontMetrics(msg); });

        m_io->subscribe("input:clipboard:text", [this](const Message& msg) { onInputClipboardText(msg); });

        m_io->subscribe("ui:load", [this](const Message& msg) { onUiLoad(msg); });

        m_io->subscribe("ui:resize", [this](const Message& msg) { onUiResize(msg); });

        m_io->subscribe("ui:drawer:toggle", [this](const Message& msg) { onUiDrawerToggle(msg); });
        m_io->subscribe("ui:drawer:set", [this](const Message& msg) { onUiDrawerSet(msg); });

        m_io->subscribe("ui:modal:open", [this](const Message& msg) { onUiModalOpen(msg); });
        m_io->subscribe("ui:modal:close", [this](const Message& msg) { onUiModalClose(msg); });

        m_io->subscribe("ui:set_visible", [this](const Message& msg) { onUiSetVisible(msg); });

        m_io->subscribe("ui:set_position", [this](const Message& msg) { onUiSetPosition(msg); });

        m_io->subscribe("ui:radial:set_items", [this](const Message& msg) { onUiRadialSetItems(msg); });

        m_io->subscribe("ui:list:set_items", [this](const Message& msg) { onUiListSetItems(msg); });

        m_io->subscribe("ui:list:set_groups", [this](const Message& msg) { onUiListSetGroups(msg); });

        m_io->subscribe("ui:list:set_tree", [this](const Message& msg) { onUiListSetTree(msg); });

        m_io->subscribe("ui:data", [this](const Message& msg) { onUiData(msg); });

        m_io->subscribe("ui:data:set", [this](const Message& msg) { onUiDataSet(msg); });

        m_io->subscribe("ui:data:merge", [this](const Message& msg) { onUiDataMerge(msg); });

        m_io->subscribe("ui:list:select", [this](const Message& msg) { onUiListSelect(msg); });

        m_io->subscribe("ui:set_text", [this](const Message& msg) { onUiSetText(msg); });
    }

    m_logger->info("UIModule initialized");
}

void UIModule::onInputMouseButton(const Message& msg) {
        bool pressed = msg.data->getBool("pressed", false);
        m_context->mouseButton = msg.data->getInt("button", 0);   // 0 = left, 1 = right
        if (pressed && !m_context->mouseDown) {
            m_context->mousePressed = true;
        }
        if (!pressed && m_context->mouseDown) {
            m_context->mouseReleased = true;
        }
        m_context->mouseDown = pressed;
}

// Legacy single-topic keyboard path (kept for the visual showcases, which still
// publish "input:keyboard" {keyCode, char}).
void UIModule::onInputKeyboard(const Message& msg) {
        m_context->keyPressed = true;
        m_context->keyCode = msg.data->getInt("keyCode", 0);
        m_context->keyChar = static_cast<char>(msg.data->getInt("char", 0));
}

// FIX #5: the real InputModule publishes input:keyboard:text {text} (typed
// characters) and input:keyboard:key {scancode, pressed, ...} (special keys) —
// NOT "input:keyboard". UIModule subscribed only the legacy topic, so keyboard
// input from the real pipeline never reached the UI. Subscribe the real topics.
void UIModule::onInputKeyboardText(const Message& msg) {
        std::string text = msg.data->getString("text", "");
        if (!text.empty()) {
            m_context->keyPressed = true;
            m_context->keyCode = 0;
            // FIX #5/C2 : on transmet la chaîne ENTIÈRE (commit IME / coller / UTF-8
            // multi-octets), pas seulement le 1er octet. keyChar garde le 1er octet
            // pour la compat (reset du blink, chemin legacy).
            m_context->keyText = text;
            m_context->keyChar = static_cast<char>(static_cast<unsigned char>(text[0]));
        }
}

void UIModule::onInputKeyboardKey(const Message& msg) {
        if (msg.data->getBool("pressed", true)) {
            // FIX #5-suite : on ne traite ici que les touches d'ÉDITION (backspace,
            // entrée, suppr, flèches, home/end), traduites du scancode SDL vers le
            // dialecte UITextInput. Les caractères imprimables arrivent par
            // input:keyboard:text (ci-dessus) — on NE lève PAS keyPressed pour eux,
            // sinon le scancode brut écraserait keyChar (déjà posé par :text) avec
            // un no-op et le caractère serait perdu.
            // Les modificateurs voyagent DÉJÀ dans ce payload (InputConverter.cpp) ; on les
            // relaie enfin. Maj distingue « déplacer le curseur » de « étendre la sélection »,
            // Ctrl porte les raccourcis d'édition.
            const bool shift = msg.data->getBool("shift", false);
            const bool ctrl  = msg.data->getBool("ctrl", false);
            const int scancode = msg.data->getInt("scancode", 0);

            int editKey = sdlScancodeToEditKey(scancode);

            // QUOI : avec Ctrl enfoncé, une LETTRE devient elle aussi un événement d'édition.
            // POURQUOI : SDL n'émet pas de SDL_TEXTINPUT pour une touche modifiée par Ctrl — un
            //   Ctrl+A n'arrivait donc par AUCUN des deux chemins (ni :text, ni :key, puisque 'A'
            //   n'est pas une touche d'édition). Les raccourcis étaient structurellement
            //   inatteignables, indépendamment du `bool ctrl = false` du site d'appel.
            // COMMENT : SDL_SCANCODE_A..Z sont contigus (4..29) — on les ramène en minuscules,
            //   dialecte attendu par onKeyInput. Sans Ctrl, on ne touche à rien : les caractères
            //   imprimables continuent d'arriver par input:keyboard:text (sinon le scancode brut
            //   écraserait keyChar et le caractère serait perdu — cf. FIX #5-suite).
            int shortcutChar = 0;
            if (ctrl && scancode >= 4 && scancode <= 29) {
                shortcutChar = 'a' + (scancode - 4);
            }

            if (editKey != 0 || shortcutChar != 0) {
                m_context->keyPressed = true;
                m_context->keyCode = editKey;  // 0 pour un raccourci pur (Ctrl+lettre)
                m_context->keyChar = static_cast<char>(shortcutChar);
                m_context->keyShift = shift;
                m_context->keyCtrl = ctrl;
                m_context->keyAlt = msg.data->getBool("alt", false);
            }
        }
}

// QUOI : réception de la table d'avances de la police, poussée par le renderer.
// POURQUOI : tout ce qui touche au curseur d'un champ de saisie (position, surlignage de
//   sélection, clic -> index) est un calcul de largeur de texte. L'UIModule ne connaît pas la
//   police ; le renderer la POUSSE plutôt que de répondre à des requêtes, parce que placer un
//   curseur doit se faire dans la frame même du clic — un aller-retour serait asynchrone.
// COMMENT : décodage de la chaîne dense (IIO ne transporte que le JSON propre du nœud). Une
//   charge utile illisible rend une table vide, donc on retombe sur le repli monospace :
//   dégradé mais cohérent, jamais un curseur placé au hasard.
void UIModule::onRenderFontMetrics(const Message& msg) {
        if (!msg.data || !m_context) return;
        text::MetricsWire wire;
        wire.baseSize = static_cast<float>(msg.data->getDouble("baseSize", 8.0));
        wire.lineHeight = static_cast<float>(msg.data->getDouble("lineHeight", 8.0));
        wire.firstCodepoint = static_cast<uint32_t>(msg.data->getInt("firstCodepoint", 32));
        wire.advances = msg.data->getString("advances", "");

        text::Metrics decoded = text::decodeDense(wire);
        if (decoded.empty()) {
            m_logger->warn("render:font:metrics illisible — on garde la mesure monospace");
            return;
        }
        m_context->fontMetrics = std::move(decoded);
        ++m_context->fontMetricsEpoch;   // invalide les mises en page dependantes de la police
        m_logger->info("Metriques de police recues: base={}px, {} glyphes",
                       m_context->fontMetrics.baseSize, m_context->fontMetrics.advances.size());
}

// QUOI : réponse du service presse-papiers à une demande de collage.
// POURQUOI : l'UIModule est délibérément SDL-free, or le presse-papiers EST une ressource
//   SDL. Il ne peut donc pas le lire lui-même : il demande (`input:clipboard:get`) et le
//   propriétaire de SDL — InputModule — répond ici. Conséquence assumée : le collage a UNE
//   FRAME DE LATENCE. C'est le prix du découplage, invisible à l'œil humain, et préférable à
//   faire rentrer SDL dans l'UIModule pour économiser 16 ms.
// COMMENT : on met le texte de côté ; `updateUI` l'insère dans le champ focalisé au prochain
//   passage. On ne l'insère pas ici : ce handler s'exécute pendant le drain IIO, hors de la
//   phase de mise à jour, et muter l'arbre de widgets à ce moment-là violerait le même
//   invariant que les autres handlers (cf. handleWindowInteraction).
void UIModule::onInputClipboardText(const Message& msg) {
        if (!msg.data) return;
        m_pendingPaste = msg.data->getString("text", "");
        m_hasPendingPaste = true;
}

void UIModule::onUiLoad(const Message& msg) {
        std::string layoutPath = msg.data->getString("path", "");
        if (!layoutPath.empty()) {
            loadLayout(layoutPath);
        }
}

// Viewport resize → reflow the UI (UI framework slice 1.1).
// QUOI : met à jour la taille d'écran connue puis relayoute tout l'arbre depuis la racine.
// POURQUOI : aucun signal de resize n'existait — screenWidth/Height étaient figés au config.
//   Le HOST (qui possède la fenêtre SDL) publie ui:resize {width,height} sur un
//   SDL_WINDOWEVENT_RESIZED ; l'UIModule reste découplé de SDL et ne fait que consommer.
// COMMENT : on n'écrase une dimension que si elle est fournie/positive (payload partiel
//   toléré), puis relayoutRoot() résout le % de la racine contre le nouveau viewport.
void UIModule::onUiResize(const Message& msg) {
        const float w = static_cast<float>(msg.data->getDouble("width", 0.0));
        const float h = static_cast<float>(msg.data->getDouble("height", 0.0));
        if (w > 0.0f) m_context->screenWidth = w;
        if (h > 0.0f) m_context->screenHeight = h;
        relayoutRoot();
}

// Edge drawer open/close (slice 5b). toggle flips it; set forces a state. The drawer
// animates the slide itself; closing purges its entries when fully off-screen.
void UIModule::onUiDrawerToggle(const Message& msg) {
        if (!m_root) return;
        if (UIWidget* w = m_root->findById(msg.data->getString("id", ""))) {
            if (w->getType() == "drawer") {
                UIDrawer* d = static_cast<UIDrawer*>(w);
                d->setOpen(!d->isOpen());
            }
        }
}

void UIModule::onUiDrawerSet(const Message& msg) {
        if (!m_root) return;
        const bool open = msg.data->getBool("open", true);
        if (UIWidget* w = m_root->findById(msg.data->getString("id", ""))) {
            if (w->getType() == "drawer") {
                static_cast<UIDrawer*>(w)->setOpen(open);
            }
        }
}

// Modal dialog open/close (slice 5a). Open raises it to the front (on top of everything);
// close hides it + purges its entries + notifies the game.
void UIModule::onUiModalOpen(const Message& msg) {
        if (!m_root) return;
        if (UIWidget* w = m_root->findById(msg.data->getString("id", ""))) {
            if (w->getType() == "modal") { w->visible = true; w->bringToFront(); }
        }
}

void UIModule::onUiModalClose(const Message& msg) {
        if (!m_root) return;
        if (UIWidget* w = m_root->findById(msg.data->getString("id", ""))) {
            if (w->getType() == "modal" && w->visible) {
                w->visible = false;
                if (m_renderer) w->releaseRenderEntries(*m_renderer);
                auto ev = std::make_unique<JsonDataNode>("closed");
                ev->setString("id", w->id);
                m_io->publish("ui:modal:closed", std::move(ev));
            }
        }
}

void UIModule::onUiSetVisible(const Message& msg) {
        std::string widgetId = msg.data->getString("id", "");
        bool visible = msg.data->getBool("visible", true);
        if (m_root) {
            if (UIWidget* widget = m_root->findById(widgetId)) {
                const bool wasVisible = widget->visible;
                widget->visible = visible;
                // Hiding it: purge its retained render entries so they don't linger ("ghost rects").
                // A hidden widget stops calling render(), so without this its last :add entries stay
                // on screen. Re-showing re-registers + re-publishes on the next render().
                if (wasVisible && !visible && m_renderer) {
                    widget->releaseRenderEntries(*m_renderer);
                } else if (!wasVisible && visible) {
                    // Revealing a widget that was hidden at LAYOUT time: the layout skips invisible
                    // children (they get no size/position), so a %-sized / anchored widget (e.g. a
                    // centered 62%-wide window) would otherwise show at (0,0) size 0. Re-run the root
                    // layout now so its %/anchor resolve before it renders.
                    relayoutRoot();
                }
            }
        }
}

// Reposition a widget at runtime (e.g. pop the action wheel centered on the cursor). For the
// radial, x/y are its CENTRE; for rect widgets, the top-left. markGeometryDirty -> re-render.
void UIModule::onUiSetPosition(const Message& msg) {
        std::string widgetId = msg.data->getString("id", "");
        if (m_root) {
            if (UIWidget* widget = m_root->findById(widgetId)) {
                widget->x = static_cast<float>(msg.data->getDouble("x", widget->x));
                widget->y = static_cast<float>(msg.data->getDouble("y", widget->y));
                // Optional SIZE — omitted keys keep the current width/height (so an x/y-only move is
                // unchanged). Lets a host animate a widget's size at runtime (e.g. show a 9-slice frame
                // staying continuous as the box grows/shrinks) without a layout reload.
                widget->width  = static_cast<float>(msg.data->getDouble("width",  widget->width));
                widget->height = static_cast<float>(msg.data->getDouble("height", widget->height));
                // absX/absY (what render() uses) are computed at LOAD, not per frame — recompute
                // them from the parent chain now, or the widget would render at its OLD position.
                widget->computeAbsolutePosition();
                widget->markGeometryDirty();
            }
        }
}

// Reconfigure a radial wheel's item COUNT at runtime (demo: prove the pie cuts into any N). The
// geometry is general — sector = 2*pi/N + arbitrary wedge angles — so 2..8 (or more) all tile.
// Generates `count` generic items; releaseRenderEntries() resets so render() re-registers with
// the new count. (A game sets real items via the layout JSON; an items[] payload is a follow-on.)
void UIModule::onUiRadialSetItems(const Message& msg) {
        if (!msg.data || !m_root) return;
        const std::string id = msg.data->getString("id", "");
        const int count = msg.data->getInt("count", 0);
        UIWidget* w = m_root->findById(id);
        if (w && w->getType() == "radial" && count > 0) {
            UIRadial* radial = static_cast<UIRadial*>(w);
            radial->items.clear();
            for (int i = 0; i < count; ++i) {
                RadialItem item;
                item.action = "wheel:opt" + std::to_string(i);
                item.text   = std::to_string(i + 1);
                radial->items.push_back(std::move(item));
            }
            if (m_renderer) radial->releaseRenderEntries(*m_renderer);   // re-register with the new N
        }
}

// Repopulate a ship list at runtime: ui:list:set_items {id, items:[{id,label,subtitle?,icon?}]}.
// The game pushes its current fleet; the list rebuilds its rows. No pool release needed — the list
// is VIRTUALIZED, so its recycled row-slots are simply remapped (rewritten or hidden) on the next
// render; setItems just swaps the data + resets scroll/selection.
void UIModule::onUiListSetItems(const Message& msg) {
        if (!msg.data || !m_root) return;
        UIWidget* w = m_root->findById(msg.data->getString("id", ""));
        if (w && w->getType() == "list") {
            static_cast<UIList*>(w)->setItems(UIList::parseItems(*msg.data));
        }
}

// Repopulate a list as GROUPED warship wings at runtime: ui:list:set_groups {id, groups:[...]}.
// Same virtualized recycle as set_items — no pool release. (json-backed payload, see UI_TOPICS.)
void UIModule::onUiListSetGroups(const Message& msg) {
        if (!msg.data || !m_root) return;
        UIWidget* w = m_root->findById(msg.data->getString("id", ""));
        if (w && w->getType() == "list") {
            static_cast<UIList*>(w)->setGroups(UIList::parseGroups(*msg.data));
        }
}

// Repopulate a list as an N-LEVEL TREE at runtime: ui:list:set_tree {id, nodes:[{...,children?}]}
// (slice 5d). Same virtualized recycle as set_items/set_groups — no pool release.
void UIModule::onUiListSetTree(const Message& msg) {
        if (!msg.data || !m_root) return;
        UIWidget* w = m_root->findById(msg.data->getString("id", ""));
        if (w && w->getType() == "list") {
            static_cast<UIList*>(w)->setTree(UIList::parseTree(*msg.data));
        }
}

// JSON-UI data context: ui:data {<the model>}. The game pushes its view-model; the whole payload
// BECOMES the root data context, and every {{path}} binding re-resolves against it (the inbound
// half of the data-driven loop — no imperative set_text needed). Reactivity = re-resolve on push.
void UIModule::onUiData(const Message& msg) {
        if (!msg.data) return;
        if (auto* jn = dynamic_cast<const JsonDataNode*>(msg.data.get())) {
            const auto& incoming = jn->getJsonData();
            // Modele IDENTIQUE -> il n'y a rien a faire, et surtout rien a defaire.
            //
            // POURQUOI ici, en plus de la garde par repeteur : republier le meme modele est le
            // geste normal d'un HUD (il pousse son etat chaque frame, qu'il ait bouge ou non).
            // Sortir ici epargne aussi la RE-RESOLUTION de tous les bindings -- chacun resolvant
            // son chemin TROIS fois (interpolate + resolveNumber + resolveBool) dont deux
            // resultats jetes, cf. P2 de l'audit. La garde par repeteur seule laisserait ce
            // travail-la se faire pour rien.
            if (incoming == m_uiData) return;
            m_uiData = incoming;
        }
        ++m_dataVersion;
        refreshDataDriven();
}

// Partial update — set ONE deep path: ui:data:set {path, value}. The game updates a single field
// (e.g. a ship's hp) without re-sending the whole model; only the bindings re-resolve.
//
// NB : pas de comparaison avant/apres ici, contrairement a `ui:data`. Detecter un patch sans
// effet exigerait de COPIER tout le modele pour le comparer ensuite -- un cout nouveau a
// chaque appel, pour attraper un cas qui n'existe pas : un `set`/`merge` est par construction
// l'intention de changer quelque chose. Le cas pathologique mesure etait la republication du
// modele ENTIER, qu'un HUD fait par frame ; c'est celui-la qui est garde.
void UIModule::onUiDataSet(const Message& msg) {
        if (!msg.data) return;
        const std::string path = msg.data->getString("path", "");
        if (path.empty()) return;
        if (auto* jn = dynamic_cast<const JsonDataNode*>(msg.data.get())) {
            const auto& j = jn->getJsonData();
            if (j.contains("value")) {
                uibind::setAtPath(m_uiData, path, j["value"]);
                ++m_dataVersion;
                refreshDataDriven();
            }
        }
}

// Partial update — deep MERGE a patch object: ui:data:merge {<partial>}. RFC 7386 semantics
// (provided keys override deeply; a null value deletes a key). Update many fields without a full push.
void UIModule::onUiDataMerge(const Message& msg) {
        if (!msg.data) return;
        if (auto* jn = dynamic_cast<const JsonDataNode*>(msg.data.get())) {
            m_uiData.merge_patch(jn->getJsonData());
            ++m_dataVersion;
            refreshDataDriven();
        }
}

// Programmatic selection: ui:list:select {id, index} (e.g. pre-select a ship). Sets state only —
// it does NOT re-emit ui:list:selected (that topic is the USER's click, to avoid feedback loops).
void UIModule::onUiListSelect(const Message& msg) {
        if (!msg.data || !m_root) return;
        UIWidget* w = m_root->findById(msg.data->getString("id", ""));
        if (w && w->getType() == "list") {
            static_cast<UIList*>(w)->setSelectedIndex(msg.data->getInt("index", -1));
        }
}

void UIModule::onUiSetText(const Message& msg) {
        // Timestamp on receive
        auto now = std::chrono::high_resolution_clock::now();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

        std::string widgetId = msg.data->getString("id", "");
        std::string text = msg.data->getString("text", "");

        // Extract original timestamp if present
        double t0 = msg.data->getDouble("_timestamp_publish", 0);
        if (t0 > 0) {
            double latency = (micros - t0) / 1000.0; // Convert to milliseconds
            m_logger->info("⏱️ [T3] UIModule received ui:set_text at {} µs (latency from T0: {:.2f} ms)", micros, latency);
        } else {
            m_logger->info("⏱️ [T3] UIModule received ui:set_text at {} µs", micros);
        }

        if (m_root) {
            if (UIWidget* widget = m_root->findById(widgetId)) {
                // Only labels support text updates
                if (widget->getType() == "label") {
                    UILabel* label = static_cast<UILabel*>(widget);
                    label->text = text;
                    m_logger->info("Updated text for label '{}': '{}'", widgetId, text);
                } else {
                    m_logger->warn("Widget '{}' is not a label, cannot set text", widgetId);
                }
            }
        }
}


void UIModule::process(const IDataNode& input) {
    float deltaTime = static_cast<float>(input.getDouble("deltaTime", 0.016));

    // Begin new frame
    m_context->beginFrame();
    m_renderer->beginFrame();

    // Process input messages from IIO
    processInput();

    // Update UI logic
    updateUI(deltaTime);

    // Render UI
    renderUI();

    m_frameCount++;
}

void UIModule::processInput() {
    if (!m_io) return;

    // Pull and dispatch all pending messages (callbacks invoked automatically)
    while (m_io->hasMessages() > 0) {
        m_io->pullAndDispatch();
    }
}

void UIModule::publishCaptureState(UIWidget* hovered) {
    if (!m_io) return;

    // A press that lands on an interactive/absorbing widget GRABS the pointer until release — capture then
    // persists even if the cursor leaves the widget mid-drag (scrollbar / window / slider / content drag).
    if (m_context->mousePressed && hovered) m_pointerGrabbed = true;
    if (m_context->mouseReleased)           m_pointerGrabbed = false;

    // Mouse captured = the pointer is over a UI widget (hitTest absorbs there — buttons, windows, lists,
    // modals, drawers, tabs, the radial; NOT bare panels/labels) OR an active grab is in progress.
    const int captureMouse    = (hovered != nullptr || m_pointerGrabbed) ? 1 : 0;
    // Keyboard captured = a widget has focus (e.g. a text input is eating keystrokes).
    const int captureKeyboard = m_context->focusedWidgetId.empty() ? 0 : 1;

    // Publish only on change (retained-style) so the game keeps a simple latched flag. The game checks it
    // before acting on input:mouse:* / input:keyboard:* for the world (camera, world clicks, shortcuts).
    if (captureMouse != m_lastCaptureMouse || captureKeyboard != m_lastCaptureKeyboard) {
        m_lastCaptureMouse = captureMouse;
        m_lastCaptureKeyboard = captureKeyboard;
        auto ev = std::make_unique<JsonDataNode>("capture");
        ev->setBool("mouse", captureMouse != 0);
        ev->setBool("keyboard", captureKeyboard != 0);
        m_io->publish("ui:capture", std::move(ev));
    }
}

// A widget's data SCOPE: the root scope, or — if it carries a scopePath (a repeater item, e.g. "fleet.0")
// — the item's data with the root as parent (so `{{name}}` is the item's, `{{$root.x}}` climbs out). The
// returned Scope's `item` out-param must outlive the returned pointer (it backs the non-root case).
static const uibind::Scope* scopeFor(UIWidget* w, const uibind::Scope& root, uibind::Scope& item) {
    if (w->scopePath.empty()) return &root;
    if (const uibind::json* d = uibind::resolvePath(root, w->scopePath)) {
        item = uibind::Scope{ d, &root };
        return &item;
    }
    return &root;   // stale/missing scope path -> fall back to root (don't crash)
}

// Recurse the tree applying each widget's data-bindings against ITS scope (root, or its repeater item).
// Each binding is resolved three ways — string (text), number (value/x/...), bool (visible) — and the
// widget picks the form it needs in applyBoundProp.
static void resolveWidgetBindings(UIWidget* w, const uibind::Scope& root, UIRenderer* renderer) {
    uibind::Scope item;
    const uibind::Scope* scope = scopeFor(w, root, item);

    // Conditional `if`: a FALSE condition hides the widget + purges its retained entries (release publishes
    // render:*:remove -> no ghost), and skips resolving/recursing the hidden subtree.
    if (!w->ifBinding.empty()) {
        const bool show = uibind::resolveBool(*scope, w->ifBinding);
        if (!show) {
            if (w->visible && renderer) w->releaseRenderEntries(*renderer);
            w->visible = false;
            return;
        }
        w->visible = true;
    }

    for (const auto& b : w->bindings) {
        w->applyBoundProp(b.first,
                          uibind::interpolate(*scope, b.second),
                          uibind::resolveNumber(*scope, b.second),
                          uibind::resolveBool(*scope, b.second));
    }
    for (auto& child : w->children) resolveWidgetBindings(child.get(), root, renderer);
}

void UIModule::resolveAllBindings() {
    if (!m_root) return;
    uibind::Scope root{ &m_uiData, nullptr };
    resolveWidgetBindings(m_root.get(), root, m_renderer.get());
}

// Set the same scopePath on a whole template instance (all its widgets share one item scope).
static void setScopePathRecursive(UIWidget* w, const std::string& path) {
    w->scopePath = path;
    for (auto& c : w->children) setScopePathRecursive(c.get(), path);
}

// Collect the ROOT-scope repeater hosts (scopePath empty). A `list` with repeat+template is SKIPPED here —
// it virtualizes its own template (only the visible rows) in updateTemplateLists. Nested repeaters (inside
// an item scope) are a deferred follow-on. Collected up-front so expansion doesn't disturb the walk.
static void collectRepeaters(UIWidget* w, std::vector<UIWidget*>& out) {
    if (!w->repeatPath.empty() && w->scopePath.empty() && w->getType() != "list") out.push_back(w);
    for (auto& c : w->children) collectRepeaters(c.get(), out);
}

// Collect the VIRTUALIZED template lists (a `list` with repeat+template at the root scope).
static void collectTemplateLists(UIWidget* w, std::vector<UIWidget*>& out) {
    if (w->getType() == "list" && !w->repeatPath.empty() && !w->repeatTemplateJson.empty() && w->scopePath.empty())
        out.push_back(w);
    for (auto& c : w->children) collectTemplateLists(c.get(), out);
}

void UIModule::expandRepeaters() {
    if (!m_root || !m_tree) return;
    uibind::Scope root{ &m_uiData, nullptr };

    std::vector<UIWidget*> hosts;
    collectRepeaters(m_root.get(), hosts);

    for (UIWidget* host : hosts) {
        const uibind::json* arr = uibind::resolvePath(root, host->repeatPath);

        // GARDE D'INACTIVITE. Ce que ce repeteur devrait afficher, compare a ce qu'il affiche deja.
        //
        // POURQUOI : sans elle, chaque poussee de donnees purgeait et reconstruisait TOUTES les
        //   instances, meme identiques -- 180 `:remove` + 180 `:add` et 15,8 ms sur 30 lignes, pour
        //   un budget de frame de 16,6 ms a 60 fps. Un HUD qui rafraichit ses donnees par frame ne
        //   tenait donc pas la frame a lui seul, et le trafic retombait sur le chemin IIO+JSON deja
        //   documente comme le mur de debit du moteur.
        //
        // COMMENT : comparaison EXACTE du tableau serialise. Ce n'est PAS une garde par
        //   `m_dataVersion` : celle-ci s'incremente a chaque poussee, identique ou non, donc elle
        //   n'aurait rien filtre ici. La garde par version existe deja pour les listes virtualisees
        //   (`windowDirty`) -- c'est le repeteur qui n'en avait aucune.
        //
        // ⚠️ Ne saute QUE la destruction/reconstruction. La re-resolution des bindings tourne juste
        //   apres (`resolveAllBindings`) sur les instances conservees, donc les valeurs affichees
        //   restent fraiches : un HUD qui pousse {hp, fleet} voit `hp` bouger sans que `fleet` soit
        //   refait. C'est precisement le cas que cette garde sert.
        const std::string snapshot = (arr && arr->is_array()) ? arr->dump() : std::string();
        if (snapshot == host->repeatDataSnapshot) {
            continue;   // rien n'a bouge pour CE repeteur
        }
        host->repeatDataSnapshot = snapshot;

        // Purge + drop the previous instances (release publishes render:*:remove -> no ghosts).
        if (m_renderer) for (auto& c : host->children) c->releaseRenderEntries(*m_renderer);
        host->children.clear();

        if (!arr || !arr->is_array()) continue;

        // Le gabarit est parse UNE FOIS par hote, plus une fois PAR ELEMENT : il ne change jamais a
        // l'execution, donc N-1 parses JSON etaient du travail pur perdu. Chaque instance recoit une
        // COPIE du json (bien moins cher qu'un reparse depuis le texte) -- parseWidget fabrique des
        // widgets neufs, mais on ne partage pas le noeud source par prudence.
        const uibind::json templateJson = uibind::json::parse(host->repeatTemplateJson);

        for (size_t i = 0; i < arr->size(); ++i) {
            JsonDataNode tnode("tpl", templateJson);
            auto inst = m_tree->parseWidget(tnode);
            if (!inst) continue;
            setScopePathRecursive(inst.get(), host->repeatPath + "." + std::to_string(i));
            inst->y = static_cast<float>(i) * inst->height;   // vertical stack by index (step 4)
            host->addChild(std::move(inst));
        }
    }
    m_root->computeAbsolutePosition();
}

void UIModule::refreshDataDriven() {
    expandRepeaters();      // (re)build instances against the current data
    resolveAllBindings();   // resolve every binding (incl. the new instances) against its scope
    // Bound positional props (x/y/width/height — e.g. a ship "part" placed from data) only change the
    // widget's relative coords; recompute absolute positions so they actually take effect on screen.
    if (m_root) m_root->computeAbsolutePosition();
}

void UIModule::updateTemplateLists() {
    if (!m_root || !m_tree) return;
    uibind::Scope root{ &m_uiData, nullptr };

    std::vector<UIWidget*> lists;
    collectTemplateLists(m_root.get(), lists);

    for (UIWidget* w : lists) {
        UIList* list = static_cast<UIList*>(w);
        const float rh = list->rowHeight;
        if (rh <= 0.0f) continue;

        // PERF idle-gate: skip the re-window + re-resolve unless an input changed (scroll / data / geometry).
        if (!list->windowDirty(m_dataVersion)) continue;
        ++m_templateWindowCount;

        // The bound data array + its count (drives the scroll range via setTemplateRowCount).
        const uibind::json* arr = uibind::resolvePath(root, list->repeatPath);
        const int dataCount = (arr && arr->is_array()) ? static_cast<int>(arr->size()) : 0;
        list->setTemplateRowCount(dataCount);

        // The visible window: only this many template instances ever exist (VIRTUALIZATION).
        const int firstVisible = std::max(0, static_cast<int>(list->scrollOffsetY / rh));
        const int fit = static_cast<int>(std::ceil(list->height / rh)) + 1;   // viewport rows + 1 buffer

        // Grow the instance pool (list children) to `fit` — re-parsed from the template, grow-only.
        while (static_cast<int>(list->children.size()) < fit) {
            uibind::json tj = uibind::json::parse(list->repeatTemplateJson);
            JsonDataNode tnode("tpl", tj);
            auto inst = m_tree->parseWidget(tnode);
            if (!inst) break;
            list->addChild(std::move(inst));
        }

        // Map each pool slot to the item now scrolled into it: set its scope + y, resolve; hide the rest.
        const int poolSize = static_cast<int>(list->children.size());
        for (int s = 0; s < poolSize; ++s) {
            UIWidget* inst = list->children[s].get();
            const int itemIndex = firstVisible + s;
            if (s < fit && itemIndex < dataCount) {
                inst->y = static_cast<float>(itemIndex) * rh - list->scrollOffsetY;
                inst->visible = true;
                setScopePathRecursive(inst, list->repeatPath + "." + std::to_string(itemIndex));
                inst->computeAbsolutePosition();
                resolveWidgetBindings(inst, root, m_renderer.get());
            } else {
                if (inst->visible && m_renderer) inst->releaseRenderEntries(*m_renderer);
                inst->visible = false;
            }
        }
    }
}

void UIModule::fireWidgetEvent(UIWidget* w, const std::string& signal) {
    if (!w || !m_io) return;
    auto it = w->eventBindings.find(signal);
    if (it == w->eventBindings.end()) return;

    // Resolve the args against the widget's scope (a repeater row carries its item) and publish. The
    // OUTBOUND half — view -> game, context-carrying, the symmetry of binding-in.
    uibind::Scope root{ &m_uiData, nullptr };
    uibind::Scope item;
    const uibind::Scope* scope = scopeFor(w, root, item);
    auto payload = std::make_unique<JsonDataNode>("event");
    for (const auto& arg : it->second.args) {
        payload->setString(arg.first, uibind::interpolate(*scope, arg.second));
    }
    m_io->publish(it->second.eventName, std::move(payload));
}

void UIModule::updateUI(float deltaTime) {
    if (!m_root) return;

    // Store previous hover state
    std::string prevHoveredId = m_context->hoveredWidgetId;

    // Perform hit testing to update hover state
    UIWidget* hoveredWidget = hitTest(m_root.get(), m_context->mouseX, m_context->mouseY);
    if (hoveredWidget && !hoveredWidget->id.empty()) {
        m_context->hoveredWidgetId = hoveredWidget->id;
    } else {
        m_context->hoveredWidgetId.clear();
    }

    // Update hover state (calls onMouseEnter/onMouseLeave). Pointer-based: only the single widget under the
    // cursor hovers (id-less repeater instances would otherwise all match an empty id).
    updateHoverState(m_root.get(), *m_context, hoveredWidget);

    // Publish hover event if changed
    if (m_context->hoveredWidgetId != prevHoveredId && m_io) {
        auto hoverEvent = std::make_unique<JsonDataNode>("hover");
        hoverEvent->setString("widgetId", m_context->hoveredWidgetId);
        hoverEvent->setBool("enter", !m_context->hoveredWidgetId.empty());
        m_io->publish("ui:hover", std::move(hoverEvent));
    }

    // Anti-click-through: tell the game whether the UI is consuming the pointer/keyboard this frame, so it
    // can skip world input (camera pan/zoom, world clicks) underneath the UI or during a UI drag.
    publishCaptureState(hoveredWidget);

    // Handle mouse wheel for scroll panels AND lists (both scroll their content on the wheel).
    if (m_context->mouseWheelDelta != 0.0f && hoveredWidget) {
        // Walk up from the hovered widget to the first scrollable ancestor (scrollpanel or list).
        // On remonte jusqu'au premier widget qui se declare hote de defilement. Enumerer
        // `scrollpanel` et `list` ici obligeait a revenir editer ce fichier pour tout futur widget
        // defilant ; le virtuel `handleMouseWheel` pose la question a la place.
        for (UIWidget* w = hoveredWidget; w; w = w->parent) {
            if (w->handleMouseWheel(m_context->mouseWheelDelta)) break;
        }
    }

    // Handle mouse button events
    if (m_context->mousePressed || m_context->mouseReleased) {
        UIWidget* clickedWidget = dispatchMouseButton(
            m_root.get(), *m_context,
            m_context->mouseButton, // 0 = left, 1 = right
            m_context->mousePressed
        );

        if (clickedWidget && m_io) {
            // Publish click event
            auto clickEvent = std::make_unique<JsonDataNode>("click");
            clickEvent->setString("widgetId", clickedWidget->id);
            clickEvent->setDouble("x", m_context->mouseX);
            clickEvent->setDouble("y", m_context->mouseY);
            m_io->publish("ui:click", std::move(clickEvent));

            // Declarative events: publish the widget's bound event with {{}}-resolved args, on the real click
            // (release). Right button fires "rightClick", left fires "click" — so a widget can do both (e.g. a
            // fleet icon: left-click selects, right-click opens the inspector).
            if (m_context->mouseReleased) fireWidgetEvent(clickedWidget, m_context->mouseButton == 1 ? "rightClick" : "click");

            // Publish type-specific events
            std::string widgetType = clickedWidget->getType();

            m_logger->info("🖱️ Widget clicked: id='{}', type='{}', mousePressed={}",
                clickedWidget->id, widgetType, m_context->mousePressed);

            // Handle focus for text inputs
            // ---------------- FOCUS CLAVIER ----------------
            //
            // QUOI     : un clic donne le focus au widget cliquable au clavier, et le retire au
            //            precedent.
            // POURQUOI : ce bloc etait ecrit DEUX FOIS -- une branche `textinput`, une branche
            //            `textarea` -- pour un corps identique au type pres. Un troisieme widget
            //            focusable aurait exige une troisieme copie.
            // COMMENT  : `acceptsFocus()` remplace l'enumeration des types, `loseFocus()`/
            //            `gainFocus()` sont appeles a travers l'interface. La publication reste ici :
            //            un widget n'a pas d'IIO (cf. UI_ARCHITECTURE.md).
            //
            // ⚠️ CORRECTION DE COMPORTEMENT au passage. L'ancienne branche `textinput` ne retirait le
            //    focus au precedent QUE s'il etait lui aussi un `textinput` -- elle ignorait le cas
            //    `textarea`, que la branche jumelle traitait pourtant. Cliquer d'une zone multiligne
            //    vers un champ laissait donc la zone focalisee en interne (curseur et bordure de
            //    focus toujours dessines), alors que les touches partaient ailleurs. Unifier le
            //    corps supprime l'asymetrie : c'etait un defaut de la duplication elle-meme.
            if (clickedWidget->acceptsFocus() && m_context->mousePressed) {
                if (!m_context->focusedWidgetId.empty() &&
                    m_context->focusedWidgetId != clickedWidget->id) {
                    if (UIWidget* prev = m_root->findById(m_context->focusedWidgetId)) {
                        prev->loseFocus();
                    }
                    auto lost = std::make_unique<JsonDataNode>("focus_lost");
                    lost->setString("widgetId", m_context->focusedWidgetId);
                    m_io->publish("ui:focus_lost", std::move(lost));
                }

                clickedWidget->gainFocus();
                m_context->setFocus(clickedWidget->id);

                auto gained = std::make_unique<JsonDataNode>("focus_gained");
                gained->setString("widgetId", clickedWidget->id);
                m_io->publish("ui:focus_gained", std::move(gained));

                m_logger->info("Widget '{}' gained keyboard focus", clickedWidget->id);
            }
            else if (widgetType == "button") {
                // Publish action event if button has onClick
                UIButton* btn = static_cast<UIButton*>(clickedWidget);
                if (!btn->onClick.empty() && m_context->mouseReleased) {
                    auto actionEvent = std::make_unique<JsonDataNode>("action");
                    actionEvent->setString("action", btn->onClick);
                    actionEvent->setString("widgetId", btn->id);
                    m_io->publish("ui:action", std::move(actionEvent));
                    m_logger->info("Button '{}' clicked, action: {}", btn->id, btn->onClick);
                }
            }
            else if (widgetType == "slider") {
                // QUOI : on n'émet PAS ui:value_changed ici — on marque seulement le
                //   début du drag sur le front "press".
                // POURQUOI : émettre uniquement dans cette branche (fronts press/release)
                //   ratait toutes les valeurs intermédiaires du drag, et même la valeur
                //   finale quand le release tombait hors containsPoint (audit H2).
                //   L'émission est désormais centralisée après le pass update() (voir
                //   plus bas), donc elle couvre aussi chaque frame de drag-move.
                // COMMENT : on retient l'id ; m_lastDragValue = NaN force l'émission de
                //   la valeur initiale au premier passage post-update.
                if (m_context->mousePressed) {
                    UISlider* slider = static_cast<UISlider*>(clickedWidget);
                    m_draggingSliderId = slider->id;
                    m_lastDragValue = std::numeric_limits<float>::quiet_NaN();
                }
            }
            else if (widgetType == "checkbox") {
                // Publish value_changed event for checkbox
                UICheckbox* checkbox = static_cast<UICheckbox*>(clickedWidget);
                if (m_context->mouseReleased) {  // Only on click release
                    auto valueEvent = std::make_unique<JsonDataNode>("value");
                    valueEvent->setString("widgetId", checkbox->id);
                    valueEvent->setBool("checked", checkbox->checked);
                    m_io->publish("ui:value_changed", std::move(valueEvent));

                    // Publish onChange action if specified
                    if (!checkbox->onChange.empty()) {
                        auto actionEvent = std::make_unique<JsonDataNode>("action");
                        actionEvent->setString("action", checkbox->onChange);
                        actionEvent->setString("widgetId", checkbox->id);
                        actionEvent->setBool("checked", checkbox->checked);
                        m_io->publish("ui:action", std::move(actionEvent));
                    }

                    m_logger->info("Checkbox '{}' toggled to {}", checkbox->id, checkbox->checked);
                }
            }
            else if (widgetType == "radial") {
                // Roue d'action : un release sur la roue CONFIRME le segment sous le
                // pointeur (publie ui:action + l'index), ou ANNULE si relâché dans la
                // dead-zone centrale (selectedAction() == "").
                // POURQUOI on émet sur ui:action (topic existant) : les jeux consomment
                //   déjà ce topic pour les boutons -> zéro nouveau contrat à câbler.
                // AUTO-CLOSE : on se cache à la sélection (la roue est un menu modal). C'est
                //   sûr maintenant que releaseRenderEntries() purge les entrées retained du
                //   widget caché (plus de rects fantômes — l'ancienne limitation est levée).
                UIRadial* radial = static_cast<UIRadial*>(clickedWidget);
                if (m_context->mouseReleased) {
                    std::string action = radial->selectedAction();
                    if (!action.empty()) {
                        auto actionEvent = std::make_unique<JsonDataNode>("action");
                        actionEvent->setString("action", action);
                        actionEvent->setString("widgetId", radial->id);
                        actionEvent->setInt("index", radial->selectedIndex());
                        m_io->publish("ui:action", std::move(actionEvent));
                        m_logger->info("Radial '{}' selected '{}' (index {})",
                                       radial->id, action, radial->selectedIndex());
                    }
                    // Close the wheel on ANY release (selection OR dead-zone cancel) and purge its
                    // retained entries so nothing lingers. A modal action-wheel closes after one pick.
                    radial->visible = false;
                    if (m_renderer) radial->releaseRenderEntries(*m_renderer);
                }
            }
            else if (widgetType == "tabs") {
                // A press in the tab bar switches the active page + notifies the game.
                UITabs* tabs = static_cast<UITabs*>(clickedWidget);
                if (m_context->mousePressed && tabs->pointInTabBar(m_context->mouseX, m_context->mouseY)) {
                    int idx = tabs->tabAt(m_context->mouseX);
                    if (idx >= 0 && idx != tabs->activeIndex()) {
                        tabs->setActiveIndex(idx);
                        auto tabEvent = std::make_unique<JsonDataNode>("tab");
                        tabEvent->setString("widgetId", tabs->id);
                        tabEvent->setInt("index", idx);
                        m_io->publish("ui:tab:changed", std::move(tabEvent));
                    }
                }
            }
            else if (widgetType == "modal") {
                // Click on the dim, outside the dialog -> close the modal (common modal UX).
                UIModal* modal = static_cast<UIModal*>(clickedWidget);
                if (m_context->mousePressed && !modal->pointInDialog(m_context->mouseX, m_context->mouseY)) {
                    modal->visible = false;
                    if (m_renderer) modal->releaseRenderEntries(*m_renderer);
                    auto closeEvent = std::make_unique<JsonDataNode>("closed");
                    closeEvent->setString("id", modal->id);
                    m_io->publish("ui:modal:closed", std::move(closeEvent));
                }
            }
            else if (widgetType == "list") {
                // A press resolves the row under the cursor (scroll-aware). A HEADER row toggles its
                // group's collapse (ui:list:group:toggled); an ITEM row selects it (ui:list:selected,
                // carrying its groupId). rowAt returns -1 on empty gaps/out-of-range -> nothing happens.
                UIList* list = static_cast<UIList*>(clickedWidget);
                // Select/toggle on RELEASE, and only if the press didn't turn into a scroll-drag (the list
                // owns scrolling: scrollbar-thumb drag + content drag-to-scroll, driven in its update()).
                if (m_context->mouseReleased && !list->suppressClick()) {
                    int row = list->rowAt(m_context->mouseY);
                    const ListRow* r = (row >= 0) ? list->rowPtr(row) : nullptr;
                    if (r && r->isHeader) {
                        // COPY the group id BEFORE toggleGroup() — it calls rebuildRows() which clears
                        // m_rows, so `r` (a pointer INTO m_rows) dangles right after. (Use-after-free
                        // otherwise: the event would carry a freed/empty groupId.)
                        const std::string gid = r->groupId;
                        bool collapsed = list->toggleGroup(gid);
                        auto ev = std::make_unique<JsonDataNode>("toggled");
                        ev->setString("id", list->id);
                        ev->setString("groupId", gid);
                        ev->setBool("collapsed", collapsed);
                        m_io->publish("ui:list:group:toggled", std::move(ev));
                    } else if (r) {
                        // Snapshot the row's fields before mutating (setSelectedIndex doesn't rebuild today,
                        // but copying keeps this robust if it ever does).
                        const std::string gid = r->groupId, iid = r->itemId;
                        const int itemIdx = r->itemIndex;
                        list->setSelectedIndex(row);
                        auto sel = std::make_unique<JsonDataNode>("selected");
                        sel->setString("id", list->id);
                        sel->setString("groupId", gid);     // "" for a flat (ungrouped) list
                        sel->setInt("index", itemIdx);      // index WITHIN the group (flat: global)
                        sel->setString("itemId", iid);
                        m_io->publish("ui:list:selected", std::move(sel));
                    }
                }
            }
        }
    }

    // COLLAGE EN ATTENTE — la réponse du presse-papiers est arrivée pendant le drain IIO ; on
    // l'applique ici, dans la phase de mise à jour, au champ focalisé. Passe par le même chemin
    // d'insertion que la frappe (`insertFilteredText`), donc il hérite gratuitement du remplacement
    // de la sélection, du filtre du champ et de la limite de longueur : coller ne peut pas introduire
    // ce que taper interdirait.
    if (m_hasPendingPaste) {
        const std::string pasted = m_pendingPaste;
        m_hasPendingPaste = false;
        m_pendingPaste.clear();

        if (!pasted.empty() && !m_context->focusedWidgetId.empty()) {
            if (UIWidget* w = m_root->findById(m_context->focusedWidgetId)) {
                // Le collage ne demande rien de specifique a un type : n'importe quel widget de
                // saisie sait inserer une chaine (meme contrat que le routage clavier ci-dessous).
                bool pastedOk = false;
                std::string widgetId, newText;
                if (UITextEditWidget* edit = dynamic_cast<UITextEditWidget*>(w)) {
                    pastedOk = edit->insertFilteredText(pasted);
                    widgetId = edit->id;
                    newText  = edit->text();
                }
                if (pastedOk) {
                    auto ev = std::make_unique<JsonDataNode>("text_changed");
                    ev->setString("widgetId", widgetId);
                    ev->setString("text", newText);
                    m_io->publish("ui:text_changed", std::move(ev));
                }
            }
        }
    }

    // Handle keyboard input for focused widget
    if (m_context->keyPressed && !m_context->focusedWidgetId.empty()) {
        if (UIWidget* focusedWidget = m_root->findById(m_context->focusedWidgetId)) {
            // ---------------- SAISIE CLAVIER ----------------
            //
            // QUOI     : raccourcis presse-papiers, insertion, touches d'edition, puis publication
            //            de `ui:text_changed` / `ui:text_submit` / `ui:action`.
            // POURQUOI : ce corps etait ecrit DEUX FOIS -- une branche `textarea`, une branche
            //            `textinput` -- pour une centaine de lignes quasi identiques. La seule
            //            divergence reelle est le declencheur de SOUMISSION : Entree pour le champ
            //            monoligne, Ctrl+Entree pour la zone (ou Entree insere un saut de ligne).
            // COMMENT  : `UITextEditWidget` porte l'API commune ; on caste UNE fois vers ce contrat
            //            au lieu de deux fois vers des types concrets, et `submitsOn()` porte la
            //            divergence. Ajouter un 3e widget editable ne touche plus ce fichier.
            //
            // Le PRESSE-PAPIERS reste ici, et non dans le widget, parce que c'est le module qui
            // possede l'IIO -- meme partage des roles que partout ailleurs (cf. UI_ARCHITECTURE.md).
            //
            // NB : un raccourci consomme ne fait PAS sortir de updateUI -- la passe de mise a jour
            // des widgets doit se derouler normalement. Un `return` ici gelerait l'UI pendant la
            // frame du Ctrl+C.
            if (UITextEditWidget* edit = dynamic_cast<UITextEditWidget*>(focusedWidget)) {
                bool shortcutConsumed = false;
                if (m_context->keyCtrl) {
                    const char shortcut = m_context->keyChar;
                    if (shortcut == 'c' || shortcut == 'x') {
                        // Rien de selectionne = rien a copier. Sans ce garde, un Ctrl+C par reflexe
                        // ecraserait le presse-papiers systeme avec du vide, detruisant ce que
                        // l'utilisateur y avait mis depuis une autre application.
                        const std::string sel = edit->selectedText();
                        if (!sel.empty()) {
                            auto d = std::make_unique<JsonDataNode>("clip");
                            d->setString("text", sel);
                            m_io->publish("input:clipboard:set", std::move(d));

                            if (shortcut == 'x' && edit->deleteSelection()) {
                                auto ev = std::make_unique<JsonDataNode>("text_changed");
                                ev->setString("widgetId", edit->id);
                                ev->setString("text", edit->text());
                                m_io->publish("ui:text_changed", std::move(ev));
                            }
                        }
                        shortcutConsumed = true;   // pas de frappe derriere un raccourci
                    } else if (shortcut == 'v') {
                        // On DEMANDE le presse-papiers ; l'insertion se fera a la reception.
                        m_io->publish("input:clipboard:get", std::make_unique<JsonDataNode>("clip"));
                        shortcutConsumed = true;
                    }
                }

                // SOUMISSION. Le declencheur ET le moment appartiennent au widget :
                //   - `submitsOn` dit QUELLE touche soumet (Entree vs Ctrl+Entree) ;
                //   - `swallowsSubmitKey` dit si elle est AVALEE (avant la frappe) ou si elle
                //     traverse le widget et soumet APRES.
                // Les deux flux different reellement -- les forcer dans un seul ordre changerait le
                // comportement de l'un des deux widgets (cf. UITextEditWidget).
                const bool submitKey = edit->submitsOn(m_context->keyCode, m_context->keyCtrl);
                auto publishSubmit = [&] {
                    auto submitEvent = std::make_unique<JsonDataNode>("text_submit");
                    submitEvent->setString("widgetId", edit->id);
                    submitEvent->setString("text", edit->text());
                    m_io->publish("ui:text_submit", std::move(submitEvent));

                    if (!edit->submitAction().empty()) {
                        auto actionEvent = std::make_unique<JsonDataNode>("action");
                        actionEvent->setString("action", edit->submitAction());
                        actionEvent->setString("widgetId", edit->id);
                        actionEvent->setString("text", edit->text());
                        m_io->publish("ui:action", std::move(actionEvent));
                    }
                    m_logger->info("TextEdit '{}' submitted: '{}'", edit->id, edit->text());
                };

                // Cas AVALE (zone multiligne) : on soumet et la touche n'atteint jamais le widget.
                bool swallowed = false;
                if (!shortcutConsumed && submitKey && edit->swallowsSubmitKey()) {
                    publishSubmit();
                    swallowed = true;
                }

                bool handled = false;
                if (shortcutConsumed || swallowed) {
                    // Raccourci deja traite : on ne le fait PAS redescendre dans le widget, sinon un
                    // Ctrl+C ajouterait un 'c' au texte.
                } else if (!m_context->keyText.empty()) {
                    // Chemin saisie texte (commit IME / collage / UTF-8) : on insere la chaine
                    // ENTIERE, pas un seul caractere.
                    handled = edit->insertFilteredText(m_context->keyText);
                } else {
                    // Chemin touches d'edition / legacy mono-caractere.
                    // keyChar est un `char` SIGNE : un octet > 127 (accent Latin-1) deviendrait
                    // negatif et se sign-extendrait en un code-point enorme. On passe par
                    // `unsigned char` d'abord pour garder la valeur 0..255 (bugprone-signed-char-misuse).
                    const uint32_t character =
                        static_cast<uint32_t>(static_cast<unsigned char>(m_context->keyChar));
                    handled = edit->onKeyInput(m_context->keyCode, character,
                                               m_context->keyCtrl, m_context->keyShift);
                }

                if (handled) {
                    auto ev = std::make_unique<JsonDataNode>("text_changed");
                    ev->setString("widgetId", edit->id);
                    ev->setString("text", edit->text());
                    m_io->publish("ui:text_changed", std::move(ev));

                    // Cas NON avale (champ monoligne) : la touche a traverse le widget, la
                    // soumission suit la frappe -- donc `text_changed` PUIS `text_submit`.
                    if (submitKey && !edit->swallowsSubmitKey()) publishSubmit();
                }
            }
        }
    }

    // In-app window interaction (raise / drag / close) — BEFORE the child-update pass, because a
    // raise reorders root->children and must not mutate the vector mid-iteration. Runs after the
    // click dispatch above, so a content-button click is delivered (and a close-click is absorbed
    // by the still-visible window) before we hide/reorder.
    handleWindowInteraction();

    // Update all widgets
    m_root->update(*m_context, deltaTime);

    // Virtualized template lists: now that scroll is current (the list updated above), window each list's
    // pool to the visible items + bind them. Runs every frame (cheap — O(visible)); positions the rows for
    // this frame's render and the next frame's hit-test.
    updateTemplateLists();

    // --- Live slider value emission (audit H2) ---
    // QUOI : si un slider est en cours de drag, émettre ui:value_changed (+ l'action
    //   onChange éventuelle) à chaque frame où sa valeur a changé.
    // POURQUOI : la valeur est modifiée dans UISlider::update() (drag-move), hors de la
    //   branche dispatch souris qui ne tourne qu'aux fronts press/release. Sans ce bloc,
    //   un drag ne produit aucun event entre le grab et le release → pas de feedback live.
    // COMMENT : on relit le slider par id (robuste à un reload de layout) ; on compare à
    //   la dernière valeur publiée (NaN au grab ⇒ 1re émission garantie, NaN != tout) ;
    //   on publie, puis on libère le drag dès que la souris n'est plus enfoncée.
    if (!m_draggingSliderId.empty() && m_io) {
        UIWidget* w = m_root->findById(m_draggingSliderId);
        if (w && w->getType() == "slider") {
            UISlider* slider = static_cast<UISlider*>(w);
            float v = slider->getValue();
            // NaN-safe : au grab m_lastDragValue vaut NaN, donc (v == NaN) est faux et
            // on émet la valeur initiale ; ensuite on n'émet que sur changement réel.
            if (!(v == m_lastDragValue)) {
                auto valueEvent = std::make_unique<JsonDataNode>("value");
                valueEvent->setString("widgetId", slider->id);
                valueEvent->setDouble("value", v);
                valueEvent->setDouble("min", slider->minValue);
                valueEvent->setDouble("max", slider->maxValue);
                m_io->publish("ui:value_changed", std::move(valueEvent));

                // Action sémantique optionnelle (onChange) — suit la valeur en live.
                if (!slider->onChange.empty()) {
                    auto actionEvent = std::make_unique<JsonDataNode>("action");
                    actionEvent->setString("action", slider->onChange);
                    actionEvent->setString("widgetId", slider->id);
                    actionEvent->setDouble("value", v);
                    m_io->publish("ui:action", std::move(actionEvent));
                }
                m_lastDragValue = v;
            }
        } else {
            // Le slider a disparu (reload / changement de layout) → on arrête le suivi.
            m_draggingSliderId.clear();
        }
        // Fin du drag : la souris a été relâchée (mouseDown repasse à false au release).
        if (!m_context->mouseDown) {
            m_draggingSliderId.clear();
        }
    }

    // Update tooltips
    if (m_tooltipManager) {
        m_tooltipManager->update(hoveredWidget, *m_context, deltaTime);
    }
}

void UIModule::handleWindowInteraction() {
    if (!m_root || !m_context) return;

    // 1. Continue / end an in-progress title-bar drag (move the window to follow the cursor).
    if (!m_draggingWindowId.empty()) {
        if (m_context->mouseDown) {
            if (UIWidget* w = m_root->findById(m_draggingWindowId)) {
                // New top-left (abs) = cursor - grab offset; store as parent-relative x/y.
                const float parentAbsX = w->parent ? w->parent->absX : 0.0f;
                const float parentAbsY = w->parent ? w->parent->absY : 0.0f;
                w->x = (m_context->mouseX - m_dragOffsetX) - parentAbsX;
                w->y = (m_context->mouseY - m_dragOffsetY) - parentAbsY;
                w->computeAbsolutePosition();  // window.update() re-derives content children next
            }
        } else {
            m_draggingWindowId.clear();  // released
        }
    }

    // 1b. Continue / end an in-progress corner resize (grow the window to the cursor, min-clamped).
    if (!m_resizingWindowId.empty()) {
        if (m_context->mouseDown) {
            if (UIWidget* w = m_root->findById(m_resizingWindowId)) {
                if (w->getType() == "window") {
                    UIWindow* win = static_cast<UIWindow*>(w);
                    const float newW = m_context->mouseX - win->absX;
                    const float newH = m_context->mouseY - win->absY;
                    win->width  = (newW > win->minWidth)  ? newW : win->minWidth;
                    win->height = (newH > win->minHeight) ? newH : win->minHeight;
                }
            }
        } else {
            m_resizingWindowId.clear();
        }
    }

    // 2. Only a fresh press starts a raise / drag / close / resize.
    if (!m_context->mousePressed) return;

    // Resolve the TOPMOST top-level window under the cursor (reverse = front-to-back).
    UIWindow* win = nullptr;
    for (auto it = m_root->children.rbegin(); it != m_root->children.rend(); ++it) {
        if ((*it)->visible && (*it)->getType() == "window") {
            UIWindow* w = static_cast<UIWindow*>(it->get());
            if (w->pointInWindow(m_context->mouseX, m_context->mouseY)) { win = w; break; }
        }
    }
    if (!win) return;

    // Raise it (z-order). Safe here: we run before the child-update pass, so reordering
    // root->children doesn't invalidate an in-progress iteration.
    win->bringToFront();

    if (win->pointInCloseButton(m_context->mouseX, m_context->mouseY)) {
        // Close: hide + purge retained entries (no ghost rects) + notify the game.
        win->visible = false;
        if (m_renderer) win->releaseRenderEntries(*m_renderer);
        if (m_io) {
            auto ev = std::make_unique<JsonDataNode>("closed");
            ev->setString("id", win->id);
            m_io->publish("ui:window:closed", std::move(ev));
        }
    } else if (win->resizable && win->pointInResizeGrip(m_context->mouseX, m_context->mouseY)) {
        // Grab the bottom-right grip: the resize-continue block above grows the window each frame.
        m_resizingWindowId = win->id;
    } else if (win->draggable && win->pointInTitleBar(m_context->mouseX, m_context->mouseY)) {
        // Grab the title bar: remember the window + where on it we grabbed.
        m_draggingWindowId = win->id;
        m_dragOffsetX = m_context->mouseX - win->absX;
        m_dragOffsetY = m_context->mouseY - win->absY;
    }
}

void UIModule::renderUI() {
    if (m_root && m_root->visible) {
        m_root->render(*m_renderer);
    }

    // Render tooltips on top of everything
    if (m_tooltipManager && m_tooltipManager->isVisible()) {
        m_tooltipManager->render(*m_renderer, m_context->screenWidth, m_context->screenHeight);
    }
}

bool UIModule::loadLayout(const std::string& layoutPath) {
    std::ifstream file(layoutPath);
    if (!file.is_open()) {
        m_logger->error("Cannot open layout file: {}", layoutPath);
        return false;
    }

    try {
        nlohmann::json jsonData;
        file >> jsonData;

        // Convert to JsonDataNode
        auto layoutNode = std::make_unique<JsonDataNode>("layout", jsonData);
        return loadLayoutData(*layoutNode);
    }
    catch (const std::exception& e) {
        m_logger->error("Failed to parse layout JSON: {}", e.what());
        return false;
    }
}

bool UIModule::loadLayoutData(const IDataNode& layoutData) {
    m_root = m_tree->loadFromJson(layoutData);
    if (m_root) {
        // Run a full layout pass NOW (not just computeAbsolutePosition): resolves the root's
        // relative size against the config viewport and positions the whole tree immediately,
        // so a freshly-loaded UI is correct before the first frame (and % sizing is honored).
        relayoutRoot();
        refreshDataDriven();    // expand repeaters + resolve {{}} props against the (possibly empty) context
        m_logger->info("Layout loaded: root id='{}', type='{}'",
                       m_root->id, m_root->getType());
        return true;
    }
    return false;
}

void UIModule::relayoutRoot() {
    if (!m_root || !m_context) return;

    // The root tracks the viewport: a root with widthPercent/heightPercent > 0 fills that
    // fraction of the window (1.0 = full screen). Without percent, the root keeps its explicit
    // JSON size. Child percents are resolved deeper, inside UILayout, against their own parent.
    if (m_root->widthPercent  > 0.0f) m_root->width  = m_root->widthPercent  * m_context->screenWidth;
    if (m_root->heightPercent > 0.0f) m_root->height = m_root->heightPercent * m_context->screenHeight;

    // measure (bottom-up) then layout (top-down) against the root's resolved size, then derive
    // absolute positions used by both render and hit-test. Mirrors UIPanel::update's per-frame
    // pass, but seeded from the viewport — so it also handles an Absolute-mode root.
    UILayout::measure(m_root.get());
    UILayout::layout(m_root.get(), m_root->width, m_root->height);
    m_root->computeAbsolutePosition();
}

void UIModule::shutdown() {
    m_logger->info("UIModule shutting down, {} frames processed", m_frameCount);

    m_root.reset();
    m_tree.reset();
    m_renderer.reset();
    m_context.reset();
}

std::unique_ptr<IDataNode> UIModule::getState() {
    auto state = std::make_unique<JsonDataNode>("state");
    state->setInt("frameCount", static_cast<int>(m_frameCount));
    return state;
}

void UIModule::setState(const IDataNode& state) {
    m_frameCount = static_cast<uint64_t>(state.getInt("frameCount", 0));
    m_logger->info("State restored: frameCount={}", m_frameCount);
}

const IDataNode& UIModule::getConfiguration() {
    if (!m_configCache) {
        m_configCache = std::make_unique<JsonDataNode>("config");
        m_configCache->setDouble("screenWidth", m_context ? m_context->screenWidth : 1280.0);
        m_configCache->setDouble("screenHeight", m_context ? m_context->screenHeight : 720.0);
    }
    return *m_configCache;
}

std::unique_ptr<IDataNode> UIModule::getHealthStatus() {
    auto health = std::make_unique<JsonDataNode>("health");
    health->setString("status", "running");
    health->setInt("frameCount", static_cast<int>(m_frameCount));
    health->setBool("hasRoot", m_root != nullptr);
    // Perf introspection: how many times a virtualized template list was actually re-windowed (idle frames
    // skip it via the windowDirty gate). Lets a test prove the gate works without IIO-observable effects.
    health->setInt("templateWindowOps", static_cast<int>(m_templateWindowCount));
    return health;
}

} // namespace grove

// ============================================================================
// C Export (required for dlopen/LoadLibrary)
// Skip when building as static library to avoid multiple definition errors
// ============================================================================

#ifndef GROVE_MODULE_STATIC

#ifdef _WIN32
#define GROVE_MODULE_EXPORT __declspec(dllexport)
#else
#define GROVE_MODULE_EXPORT
#endif

extern "C" {

GROVE_MODULE_EXPORT grove::IModule* createModule() {
    return new grove::UIModule();
}

GROVE_MODULE_EXPORT void destroyModule(grove::IModule* module) {
    delete module;
}

}

#endif // GROVE_MODULE_STATIC
