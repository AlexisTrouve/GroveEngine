/**
 * GroveEngine — FULL UI demo: "Fleet Command"
 *
 * One screen exercising EVERYTHING built into the UI framework + the JSON data-binding engine:
 *  - layout / HUD (anchored bar), panels, labels, buttons, progressbars
 *  - a VIRTUALIZED, TEMPLATED, data-bound fleet list (sidebar) with per-row events + scrollbar
 *  - an in-app WINDOW (drag / resize / close), TABS (sections), a DRAWER, a MODAL, the action RADIAL
 *  - DATA BINDING ({{credits}}, {{selected.name}}, {{hp}} ...), declarative EVENTS, `if` conditionals
 *  - REACTIVITY: the whole UI is described in JSON and driven by ui:data / :merge pushes from the "game"
 *
 * The UI is loaded from assets/ui/demo_fleet_command.json (the SAME file IT_043 verifies headlessly).
 * This program is the DEMO (no assertions); the verification is the E2E suite.
 *
 * Run from the PROJECT ROOT:  ./build/tests/test_ui_full_demo
 * Controls: mouse to interact · "Tour +" advances turn (+ live hull wear) · right-click = action wheel · ESC quits.
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <memory>
#include <string>

#include "../helpers/WindowIcon.h"
#include "BgfxRendererModule.h"
#include "UIModule.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp>

using namespace grove;
using nlohmann::json;

static constexpr int WIN_W = 1280, WIN_H = 720;

class FleetCommandDemo {
public:
    bool init(SDL_Window* window) {
        SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(window, &wmi);
        m_logger = spdlog::stdout_color_mt("FleetDemo");
        spdlog::set_level(spdlog::level::info);

        m_rendererIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_uiIOPtr       = IntraIOManager::getInstance().createInstance("ui_module");
        m_gameIOPtr     = IntraIOManager::getInstance().createInstance("game");
        m_rendererIO = m_rendererIOPtr.get(); m_uiIO = m_uiIOPtr.get(); m_gameIO = m_gameIOPtr.get();

        // Renderer.
        m_renderer = std::make_unique<BgfxRendererModule>();
        {
            JsonDataNode config("config");
            config.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
            config.setInt("windowWidth", WIN_W); config.setInt("windowHeight", WIN_H);
            config.setBool("vsync", true);
            m_renderer->setConfiguration(config, m_rendererIO, nullptr);
        }

        // UIModule — load the demo layout from JSON file (the whole UI is data-described).
        m_uiModule = std::make_unique<UIModule>();
        {
            JsonDataNode config("config");
            config.setString("layoutFile", "assets/ui/demo_fleet_command.json");   // run from project root
            config.setInt("windowWidth", WIN_W); config.setInt("windowHeight", WIN_H);
            config.setInt("baseLayer", 1000);
            m_uiModule->setConfiguration(config, m_uiIO, nullptr);
        }

        // The "game": react to the UI's declarative events.
        m_gameIO->subscribe("fleet:select",   [this](const Message& m){ onSelect(m.data->getString("id","")); });
        m_gameIO->subscribe("turn:advance",   [this](const Message&)  { onTurnAdvance(); });
        m_gameIO->subscribe("fleet:scuttle",  [this](const Message& m){ onScuttle(m.data->getString("id","")); });
        m_gameIO->subscribe("demo:drawer",    [this](const Message& m){ relay("ui:drawer:toggle", m.data->getString("id","")); });
        m_gameIO->subscribe("demo:modalOpen", [this](const Message& m){ relay("ui:modal:open", m.data->getString("id","")); });
        m_gameIO->subscribe("demo:modalClose",[this](const Message& m){ relay("ui:modal:close", m.data->getString("id","")); });
        m_gameIO->subscribe("ui:action",      [this](const Message& m){ m_logger->info("action: {}", m.data->getString("action","")); });

        buildModel();
        pushFull();
        m_logger->info("=== Fleet Command demo — mouse to interact, Tour + for live data, right-click = wheel, ESC quits ===");
        return true;
    }

    void handleSDLEvent(SDL_Event& e) {
        if (e.type == SDL_MOUSEMOTION) {
            m_mx = static_cast<float>(e.motion.x); m_my = static_cast<float>(e.motion.y);
            auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("x", m_mx); d->setDouble("y", m_my);
            m_gameIO->publish("input:mouse:move", std::move(d));
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            auto d = std::make_unique<JsonDataNode>("d");
            d->setInt("button", e.button.button - 1);
            d->setBool("pressed", e.type == SDL_MOUSEBUTTONDOWN);
            d->setDouble("x", static_cast<double>(e.button.x)); d->setDouble("y", static_cast<double>(e.button.y));
            m_gameIO->publish("input:mouse:button", std::move(d));
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) openWheel(m_mx, m_my);
        } else if (e.type == SDL_MOUSEWHEEL) {
            auto d = std::make_unique<JsonDataNode>("d"); d->setDouble("delta", static_cast<double>(e.wheel.y));
            m_gameIO->publish("input:mouse:wheel", std::move(d));
        }
    }

    void frame(float dt) {
        // Drain the game's event queue (callbacks above react to UI events).
        while (m_gameIO->hasMessages() > 0) m_gameIO->pullAndDispatch();

        // Live data: every ~1.5s the hulls wear down a touch -> the bound HP bars (sidebar + detail)
        // animate, and the virtualized list re-binds the visible rows. Pure reactivity.
        m_churn += dt;
        if (m_churn >= 1.5f) {
            m_churn = 0.0f;
            for (auto& s : m_model["fleet"]) {
                double hp = s.value("hp", 1.0) - 0.04; s["hp"] = hp < 0.05 ? 0.05 : hp;
            }
            syncSelectedHp();
            pushMerge(json{ {"fleet", m_model["fleet"]}, {"selected", m_model["selected"]} });
        }

        // Camera (full window) + step the modules.
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",WIN_W); cam->setInt("viewportH",WIN_H);
          m_gameIO->publish("render:camera", std::move(cam)); }
        JsonDataNode input("input"); input.setDouble("deltaTime", dt); input.setInt("frameCount", ++m_frames);
        m_uiModule->process(input);
        m_renderer->process(input);
    }

    void shutdown() {
        m_uiModule->shutdown(); m_renderer->shutdown();
        for (const char* n : {"renderer","ui_module","game"}) IntraIOManager::getInstance().removeInstance(n);
    }

private:
    void buildModel() {
        const char* names[] = {"Aurora","Borealis","Cygnus","Draco","Equinox","Falcon","Gemini","Helios",
                               "Icarus","Juno","Kestrel","Lyra","Mistral","Nova","Orion","Pavo"};
        const char* cls[]   = {"Frigate","Hauler","Scout","Cruiser","Corvette"};
        json fleet = json::array();
        for (int i = 0; i < 16; ++i)
            fleet.push_back({ {"id","s"+std::to_string(i)}, {"name",names[i]}, {"cls",cls[i%5]},
                              {"hp", 0.4 + 0.04*(i%15)} });
        m_model = { {"credits",1240},{"turn",1},{"fleetCount",(int)fleet.size()},
                    {"hasSelection",false},{"noSelection",true},
                    {"selected",{{"id",""},{"name","-"},{"cls","-"},{"hp",0}}},{"fleet",fleet} };
    }
    void pushFull()  { m_gameIO->publish("ui:data", std::make_unique<JsonDataNode>("d", m_model)); }
    void pushMerge(json patch) { m_gameIO->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", std::move(patch))); }
    void relay(const std::string& topic, const std::string& id) {
        auto d = std::make_unique<JsonDataNode>("d"); d->setString("id", id); m_gameIO->publish(topic, std::move(d));
    }
    void onSelect(const std::string& id) {
        for (auto& s : m_model["fleet"]) if (s.value("id","") == id) {
            m_model["selected"] = s;
            m_model["hasSelection"] = true; m_model["noSelection"] = false;
            pushMerge(json{ {"selected",s},{"hasSelection",true},{"noSelection",false} });
            m_logger->info("selected {}", s.value("name",""));
            return;
        }
    }
    void onTurnAdvance() {
        int t = m_model.value("turn",1) + 1; m_model["turn"] = t;
        for (auto& s : m_model["fleet"]) { double hp = s.value("hp",1.0)-0.06; s["hp"] = hp<0.05?0.05:hp; }
        syncSelectedHp();
        pushMerge(json{ {"turn",t},{"fleet",m_model["fleet"]},{"selected",m_model["selected"]} });
    }
    void onScuttle(const std::string& id) {
        json kept = json::array();
        for (auto& s : m_model["fleet"]) if (s.value("id","") != id) kept.push_back(s);
        m_model["fleet"] = kept; m_model["fleetCount"] = (int)kept.size();
        m_model["hasSelection"] = false; m_model["noSelection"] = true;
        m_model["selected"] = {{"id",""},{"name","-"},{"cls","-"},{"hp",0}};
        pushFull();
        relay("ui:modal:close", "scuttleModal");
    }
    void syncSelectedHp() {
        const std::string id = m_model["selected"].value("id","");
        if (id.empty()) return;
        for (auto& s : m_model["fleet"]) if (s.value("id","") == id) { m_model["selected"] = s; return; }
    }
    void openWheel(float x, float y) {
        { auto p = std::make_unique<JsonDataNode>("d"); p->setString("id","wheel"); p->setDouble("x",x); p->setDouble("y",y);
          m_gameIO->publish("ui:set_position", std::move(p)); }
        { auto v = std::make_unique<JsonDataNode>("d"); v->setString("id","wheel"); v->setBool("visible",true);
          m_gameIO->publish("ui:set_visible", std::move(v)); }
    }

    std::shared_ptr<spdlog::logger> m_logger;
    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::unique_ptr<UIModule> m_uiModule;
    std::shared_ptr<IntraIO> m_rendererIOPtr, m_uiIOPtr, m_gameIOPtr;
    IIO* m_rendererIO = nullptr; IIO* m_uiIO = nullptr; IIO* m_gameIO = nullptr;
    json m_model;
    float m_mx = 640, m_my = 360, m_churn = 0.0f;
    int m_frames = 0;
};

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return 1; }
    SDL_Window* window = SDL_CreateWindow("GroveEngine — Fleet Command (full UI demo)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; SDL_Quit(); return 1; }
    grove::test::setWindowIconGrove(window);

    FleetCommandDemo demo;
    if (!demo.init(window)) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    bool running = true; Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) running = false;
            demo.handleSDLEvent(e);
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last) / SDL_GetPerformanceFrequency(); last = now;
        demo.frame(dt);
        SDL_Delay(1);
    }
    demo.shutdown();
    SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
