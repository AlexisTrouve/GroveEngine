#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/JsonDataNode.h>
#include <memory>
#include <spdlog/logger.h>

namespace grove {

class UIContext;
class UITree;
class UIRenderer;
class UIWidget;
class UITooltipManager;

/**
 * @brief UI Module - Declarative UI system with JSON configuration
 *
 * Provides a retained-mode UI system with:
 * - JSON-based layout definition
 * - Widget hierarchy (Panel, Label, Button, etc.)
 * - Rendering via IIO topics (render:sprite, render:text)
 * - Input handling via IIO (input:mouse, input:keyboard)
 */
class UIModule : public IModule {
public:
    UIModule();
    ~UIModule() override;

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "UIModule"; }
    bool isIdle() const override { return true; }

private:
    IIO* m_io = nullptr;
    std::shared_ptr<spdlog::logger> m_logger;

    // UI subsystems
    std::unique_ptr<UIContext> m_context;
    std::unique_ptr<UITree> m_tree;
    std::unique_ptr<UIRenderer> m_renderer;
    std::unique_ptr<UITooltipManager> m_tooltipManager;
    std::unique_ptr<UIWidget> m_root;

    // Configuration cache
    std::unique_ptr<JsonDataNode> m_configCache;

    // Stats
    uint64_t m_frameCount = 0;

    // --- Live slider drag tracking (audit H2) ---
    // QUOI : id du slider actuellement "saisi" (drag en cours) + dernière valeur
    //   qu'on a publiée pour lui.
    // POURQUOI : avant, ui:value_changed n'était émis que dans la branche dispatch
    //   souris (fronts press/release). Pendant un drag (souris tenue + déplacée, sans
    //   front bouton), UISlider::update() modifie la valeur SANS qu'aucun event ne
    //   parte → aucun feedback live (volume, luminosité…). Cf. audit H2. Pire : le
    //   release tombait souvent hors containsPoint (bord droit, strict <) → même la
    //   valeur finale était perdue.
    // COMMENT : on retient l'id (pas un pointeur brut — survit à un reload de layout) ;
    //   au grab on met m_lastDragValue = NaN pour forcer l'émission de la valeur
    //   initiale ; chaque frame, après update(), on republie si la valeur a changé.
    std::string m_draggingSliderId;
    float m_lastDragValue = 0.0f;

    // --- In-app window interaction (slice 3b-2) ---
    // QUOI : id de la fenêtre actuellement glissée par son titlebar + offset de saisie.
    // POURQUOI : drag/raise/close se résolvent de façon CENTRALISÉE (la fenêtre topmost sous le
    //   curseur), pas par fenêtre — sinon deux fenêtres qui se chevauchent se disputent le drag.
    //   Le raise (bringToFront) mute root->children : on le fait AVANT la passe d'update enfants.
    std::string m_draggingWindowId;
    float m_dragOffsetX = 0.0f, m_dragOffsetY = 0.0f;
    std::string m_resizingWindowId;   // window being resized by its bottom-right grip

    // Input-capture ("WantCaptureMouse") — so the game does NOT also act on input the UI consumed (a click
    // on a widget, or a drag that started on the UI, must not click-through to the world/camera behind it).
    // m_pointerGrabbed = a press landed on the UI and the button is still held (capture persists off-widget
    // for the whole drag). Published as ui:capture {mouse, keyboard} on change. -1 = nothing published yet.
    bool m_pointerGrabbed = false;
    int  m_lastCaptureMouse = -1;
    int  m_lastCaptureKeyboard = -1;
    void publishCaptureState(UIWidget* hovered);

    // JSON-UI data context (the "model" the game pushes via ui:data). Data-bindings ({{path}} props) and
    // declarative events resolve against it. resolveAllBindings re-applies every widget binding (called at
    // load + on ui:data); fireWidgetEvent publishes a widget's declarative event with {{}}-resolved args.
    nlohmann::json m_uiData = nlohmann::json::object();
    uint64_t m_dataVersion = 0;          // bumped on every data push — drives the template-list idle gate
    uint64_t m_templateWindowCount = 0;  // perf introspection (reported in getHealthStatus): #re-windows
    void resolveAllBindings();
    void expandRepeaters();        // (re)instantiate every NON-list repeater's template per data-array element
    void updateTemplateLists();    // window+bind the VIRTUALIZED template lists (only visible rows) each frame
    void refreshDataDriven();      // expandRepeaters() + resolveAllBindings() — call on load + each data push
    void fireWidgetEvent(UIWidget* w, const std::string& signal);

    // Load layout from file path
    bool loadLayout(const std::string& layoutPath);

    // Load layout from inline JSON data
    bool loadLayoutData(const IDataNode& layoutData);

    // Process input from IIO
    void processInput();

    // Update UI state
    void updateUI(float deltaTime);

    // In-app window interaction (slice 3b-2): raise-on-click + title-bar drag + close button. Runs
    // once per frame on the topmost window under the cursor, before the child-update pass.
    void handleWindowInteraction();

    // Render UI
    void renderUI();

    // Re-layout the whole tree against the current viewport (screenWidth/Height) and recompute
    // absolute positions. QUOI : résout d'abord le sizing relatif de la RACINE (widthPercent/
    //   heightPercent → fraction du viewport, 1.0 = plein écran), puis measure+layout+absPos.
    // POURQUOI : c'est le point d'entrée du reflow — appelé au chargement (layout initial correct,
    //   % résolu tout de suite) ET à chaque ui:resize. Sans lui, la racine garde sa taille JSON et
    //   rien ne se réagence quand la fenêtre change. (Les panels relayoutent déjà par frame dans
    //   UIPanel::update, mais sur LEUR taille — ils ne savent pas suivre le viewport.)
    void relayoutRoot();
};

} // namespace grove
