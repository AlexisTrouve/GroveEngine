#pragma once

/**
 * tests/visual/MapViewHud — the resources/core HUD for the map-viewer, as ONE object shared by the live
 * window (test_mapview_viewer) and the E2E (test_mapview_viewer_e2e) so both drive identical code.
 *
 * WHAT : hosts InputModule + UIModule on the viewer's engine, loads the HUD layout, and wires its declarative
 *        events to the map: a CATEGORY button repopulates the left resource list (data-driven from the world's
 *        res_* schema); a RESOURCE row swaps the map to that resource's density heatmap lens; "Élévation"
 *        returns to terrain. The planet CORE side-car (core.json, non-spatial) is pushed to the right panel.
 *
 * WHY  : the HUD widgets render through the UIModule's RETAINED path, which now lands in the screen-fixed HUD
 *        bucket (SceneCollector retained-HUD fix) — so the HUD stays put while the map pans/zooms. This object
 *        is the wiring that proves it end to end; the E2E injects real clicks through feedAndPump().
 *
 * NOTE : Input + UI are registered AFTER the renderer. UI widgets are RETAINED, so a one-frame lag on a change
 *        is invisible; this keeps the hosting simple (no re-ordering of the existing renderer registration).
 *        Only compiled when GROVE_MAPVIEW_HUD is defined (i.e. UIModule + InputModule are in the build).
 */

#ifdef GROVE_MAPVIEW_HUD

#include <SDL.h>

#include "UIModule.h"
#include "InputModule.h"
#include "MapViewViewerApp.h"
#include "MapViewDemoScene.h"

#include <grove/DebugEngine.h>
#include <grove/IModuleSystem.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include "grove/mapview/Field.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace grove {
namespace mvdemo {

class MapViewHud {
public:
    MapViewHud(DebugEngine& engine, IIO* gio, ViewerApp& app,
               const std::vector<mapview::FieldDecl>& schema, int w, int h)
        : gio_(gio), app_(app) {
        // Host Input + UI on the engine. Registration order = engine.step() process order: INPUT first (it
        // publishes input:*), then UI (consumes input:* SAME frame), then the renderer (registered earlier by
        // the caller — the UI is retained so it draws a frame late, invisible). registerStaticModule wires each
        // module's setConfiguration(config, routed-io, scheduler).
        {
            auto inOwned = std::make_unique<InputModule>();
            input_ = inOwned.get();
            engine.registerStaticModule("input", std::move(inOwned), ModuleSystemType::SEQUENTIAL,
                                        std::make_unique<JsonDataNode>("config"));
        }
        {
            auto uiCfg = std::make_unique<JsonDataNode>("config");
            // Resolve the layout across cwds: the live viewer runs from the project ROOT, but ctest runs the
            // E2E from build/tests (so "../../assets" reaches the source). Pick the first path that exists.
            uiCfg->setString("layoutFile", resolveLayout());
            uiCfg->setInt("windowWidth", w); uiCfg->setInt("windowHeight", h); uiCfg->setInt("baseLayer", 1000);
            engine.registerStaticModule("ui", std::make_unique<UIModule>(), ModuleSystemType::SEQUENTIAL, std::move(uiCfg));
        }

        buildCategories(schema);

        // The HUD's declarative events are published on their OWN topic names (fireWidgetEvent publishes on
        // the layout's "event" value, args as string fields) — plus the built-in ui:list:selected / ui:capture.
        gio_->subscribe("ui:capture",       [this](const Message& m) { mouseCaptured_ = m.data->getBool("mouse", false); });
        gio_->subscribe("cat:select",       [this](const Message& m) { onCategory(m); });
        gio_->subscribe("lens:terrain",     [this](const Message&)   { app_.useTerrainLens(); activeLens_ = "terrain"; ++actionCount_; });
        gio_->subscribe("ui:list:selected", [this](const Message& m) { onResourceSelected(m); });
        gio_->subscribe("core:pick",        [this](const Message& m) { lastMaterial_ = m.data->getString("material", ""); });
    }

    // Drain SDL: feed each event to InputModule (so it reaches the UI) AND the viewer's camera handler
    // (skipping mouse events while the UI holds capture — the pointer is over a widget). Then drain gIO so this
    // object's subscriptions fire. The SAME loop the live window and the E2E run (E2E injects via SDL_PushEvent).
    void feedAndPump() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (input_) input_->feedEvent(&e);
            // Anti-click-through: gate the viewer's PAN-START (a left button-down) when the pointer is over a UI
            // widget (ui:capture mouse=true). Wheel/motion/up pass through — the wheel test zooms even over the
            // map, and gating only the pan-start is enough to stop a HUD click from grabbing the camera.
            if (!(e.type == SDL_MOUSEBUTTONDOWN && mouseCaptured_)) app_.handleEvent(e);
        }
        drainBus();
    }

    void drainBus() { while (gio_ && gio_->hasMessages() > 0) gio_->pullAndDispatch(); }

    // Push the planet-core side-car (non-spatial planet state) to the core panel (bound {{core.*}}).
    void setCore(const nlohmann::json& core) {
        auto d = std::make_unique<JsonDataNode>("d", nlohmann::json{ {"core", core} });
        gio_->publish("ui:data:merge", std::move(d));
    }

    // A representative MOCK core (stands in for Theomen's core.json side-car until it ships). fractionPct is
    // precomputed so the composition rows show a % without an expression language in the binding.
    void setMockCore() {
        setCore(nlohmann::json{
            {"temperature_c", 5200}, {"max_capacity", 1000000000}, {"total_mass", 742000000},
            {"fill_ratio", 0.74},
            {"composition", nlohmann::json::array({
                {{"material", "iron_ore"},   {"quantity", 408000000}, {"fraction", 0.55}, {"fractionPct", 55}},
                {{"material", "nickel"},     {"quantity", 133000000}, {"fraction", 0.18}, {"fractionPct", 18}},
                {{"material", "silica"},     {"quantity", 89000000},  {"fraction", 0.12}, {"fractionPct", 12}},
                {{"material", "sulfur"},     {"quantity", 59000000},  {"fraction", 0.08}, {"fractionPct", 8}},
                {{"material", "uranium"},    {"quantity", 52000000},  {"fraction", 0.07}, {"fractionPct", 7}},
            })}});
    }

    // --- E2E introspection ---
    int actionCount() const { return actionCount_; }
    const std::string& activeLens() const { return activeLens_; }
    const std::string& lastCategory() const { return lastCategory_; }
    int listItemCount() const { return lastListCount_; }
    size_t categoryCount() const { return catToFields_.size(); }
    const std::string& lastMaterial() const { return lastMaterial_; }

private:
    // Find the HUD layout regardless of cwd (project root for the live viewer, build/tests for ctest).
    static std::string resolveLayout() {
        for (const char* p : { "assets/ui/mapview_hud.json",
                               "../../assets/ui/mapview_hud.json",
                               "../assets/ui/mapview_hud.json" }) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) return p;
        }
        return "assets/ui/mapview_hud.json";   // fallback (UIModule logs a load failure if truly absent)
    }

    // Discover the resource density fields (res_*) from the world's schema and bucket each into a category.
    void buildCategories(const std::vector<mapview::FieldDecl>& schema) {
        for (const auto& f : schema) {
            if (f.name.rfind("res_", 0) != 0) continue;   // only resource-density fields
            catToFields_[categoryOf(f.name)].push_back(f.name);
        }
    }

    // The ONLY hardcoded piece: res_<type> -> category, by keyword. Unmatched -> "Autres". Resource DISCOVERY
    // stays data-driven (schema); only this classification is a table.
    static std::string categoryOf(const std::string& field) {
        struct KV { const char* kw; const char* cat; };
        static const KV table[] = {
            {"coal","Combustibles"},{"oil","Combustibles"},{"gas","Combustibles"},{"hydrogen","Combustibles"},
            {"iron","Métaux"},{"copper","Métaux"},{"nickel","Métaux"},{"aluminum","Métaux"},{"titanium","Métaux"},{"lead","Métaux"},{"tin","Métaux"},{"zinc","Métaux"},
            {"gold","Précieux"},{"silver","Précieux"},{"platinum","Précieux"},{"gem","Précieux"},{"diamond","Précieux"},
            {"uranium","Radioactifs"},{"thorium","Radioactifs"},{"plutonium","Radioactifs"},{"radium","Radioactifs"},
            {"ice","Glaces&volatils"},{"water","Glaces&volatils"},{"methane","Glaces&volatils"},{"ammonia","Glaces&volatils"},{"co2","Glaces&volatils"},{"nitrogen","Glaces&volatils"},
            {"silica","Minéraux"},{"quartz","Minéraux"},{"sulfur","Minéraux"},{"salt","Minéraux"},{"clay","Minéraux"},{"phosphate","Minéraux"},{"carbonate","Minéraux"},
        };
        for (const auto& kv : table) if (field.find(kv.kw) != std::string::npos) return kv.cat;
        return "Autres";
    }

    // A category button was clicked -> fill the left list with that category's resources (data-driven).
    void onCategory(const Message& m) {
        const std::string cat = m.data->getString("id", "");
        lastCategory_ = cat; ++actionCount_;
        nlohmann::json items = nlohmann::json::array();
        auto it = catToFields_.find(cat);
        if (it != catToFields_.end()) {
            for (const auto& field : it->second)
                items.push_back({ {"id", field}, {"label", prettyLabel(field)} });
        }
        lastListCount_ = static_cast<int>(items.size());
        { auto d = std::make_unique<JsonDataNode>("d", nlohmann::json{ {"id", "resList"}, {"items", items} });
          gio_->publish("ui:list:set_items", std::move(d)); }
        { auto d = std::make_unique<JsonDataNode>("d", nlohmann::json{ {"catTitle", cat} });
          gio_->publish("ui:data:merge", std::move(d)); }
        // Open the left sub-menu drawer (starts closed so it doesn't capture the pointer over the map).
        { auto d = std::make_unique<JsonDataNode>("d", nlohmann::json{ {"id", "resDrawer"}, {"open", true} });
          gio_->publish("ui:drawer:set", std::move(d)); }
    }

    // A resource row was clicked -> swap the map to that resource's density heatmap.
    void onResourceSelected(const Message& m) {
        if (m.data->getString("id", "") != "resList") return;   // only the resource list drives the lens
        const std::string field = m.data->getString("itemId", "");
        if (field.empty()) return;
        app_.setLens(mvdemo::makeResourceLens(field, app_.hillshade()));
        activeLens_ = "resource:" + field;
        ++actionCount_;
    }

    static std::string prettyLabel(const std::string& field) {
        std::string s = field.rfind("res_", 0) == 0 ? field.substr(4) : field;   // drop the res_ prefix
        for (char& c : s) if (c == '_') c = ' ';
        if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
        return s;
    }

    IIO*         gio_;
    ViewerApp&   app_;
    InputModule* input_ = nullptr;
    bool         mouseCaptured_ = false;
    std::map<std::string, std::vector<std::string>> catToFields_;   // category -> res_* fields

    // E2E-observable state.
    int         actionCount_ = 0;
    int         lastListCount_ = 0;
    std::string activeLens_ = "terrain";
    std::string lastCategory_;
    std::string lastMaterial_;
};

} // namespace mvdemo
} // namespace grove

#endif  // GROVE_MAPVIEW_HUD
