/**
 * GroveEngine — INTERACTIVE vessel screen demo (slices 2+).
 *
 * QUOI : la fenêtre OS charge demo_vessel_screen.json — un drawer de FLOTTE caché hors-screen à gauche,
 *        rempli de VIGNETTES de vaisseau (repeater data-driven, une par ship). On l'ouvre/ferme (bouton
 *        "Flotte" en haut-droite OU touche F) ; il slide. Cliquer une vignette émet vessel:open{id}.
 *
 * POURQUOI : prouver "à la main" le menu vaisseau caché-hors-screen + ses maquettes (IT_047 le verrouille
 *            headless). Slices suivantes : vignettes = mini-maquettes (3), clic vignette -> inspector (4).
 *
 * Lancer depuis la RACINE :  ./build/tests/test_vessel_screen_demo
 * Contrôles : "Flotte" (ou F) ouvre/ferme le drawer · clic une vignette = vessel:open · etire = resize · ESC.
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <memory>
#include <string>
#include <cstdio>

#include "BgfxRendererModule.h"
#include "UIModule.h"
#include "RHI/RHIDevice.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <nlohmann/json.hpp>

using namespace grove;
using nlohmann::json;

class VesselScreenDemo {
public:
    bool init(SDL_Window* window, int w, int h) {
        m_w = w; m_h = h;
        SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(window, &wmi);

        m_rIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_uIOPtr = IntraIOManager::getInstance().createInstance("ui_module");
        m_gIOPtr = IntraIOManager::getInstance().createInstance("game");
        m_rIO = m_rIOPtr.get(); m_uIO = m_uIOPtr.get(); m_gIO = m_gIOPtr.get();

        m_renderer = std::make_unique<BgfxRendererModule>();
        {
            JsonDataNode c("config");
            c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setBool("vsync", true);
            c.setString("texture1", "assets/textures/ship/cockpit.png");
            c.setString("texture2", "assets/textures/ship/reactor.png");
            c.setString("texture3", "assets/textures/ship/engine.png");
            c.setString("texture4", "assets/textures/ship/gun.png");
            m_renderer->setConfiguration(c, m_rIO, nullptr);
        }
        m_uiModule = std::make_unique<UIModule>();
        {
            JsonDataNode c("config");
            c.setString("layoutFile", "assets/ui/demo_vessel_screen.json");
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setInt("baseLayer", 1000);
            m_uiModule->setConfiguration(c, m_uIO, nullptr);
        }

        // The HUD "Flotte" button emits vessel:drawer -> toggle the drawer. A vignette click emits vessel:open.
        m_gIO->subscribe("vessel:drawer", [this](const Message&){ toggleDrawer(); });
        m_gIO->subscribe("vessel:open",   [this](const Message& m){
            std::cout << "vessel:open " << m.data->getString("id","") << " (slice 4: ouvrira l'inspector)\n";
        });

        pushFleet();
        std::cout << "=== Vessel screen — bouton 'Flotte' (ou F) ouvre le drawer, clic une vignette, ESC quitte ===\n";
        return true;
    }

    void handleSDLEvent(SDL_Event& e) {
        if (e.type == SDL_WINDOWEVENT &&
            (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) {
            m_w = e.window.data1; m_h = e.window.data2;
            if (auto* dev = m_renderer->getDevice()) dev->reset(static_cast<uint16_t>(m_w), static_cast<uint16_t>(m_h));
            publishCamera();
            { auto d=std::make_unique<JsonDataNode>("d"); d->setInt("width",m_w); d->setInt("height",m_h); m_gIO->publish("ui:resize", std::move(d)); }
        } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_f) {
            toggleDrawer();                                  // F toggles (coordinate-free, handy for capture)
        } else if (e.type == SDL_MOUSEMOTION) {
            auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",e.motion.x); d->setDouble("y",e.motion.y);
            m_gIO->publish("input:mouse:move", std::move(d));
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            auto d=std::make_unique<JsonDataNode>("d");
            d->setInt("button", e.button.button - 1); d->setBool("pressed", e.type == SDL_MOUSEBUTTONDOWN);
            d->setDouble("x", e.button.x); d->setDouble("y", e.button.y);
            m_gIO->publish("input:mouse:button", std::move(d));
        }
    }

    void frame(float dt) {
        while (m_gIO->hasMessages() > 0) m_gIO->pullAndDispatch();
        publishCamera();
        JsonDataNode input("input"); input.setDouble("deltaTime", dt);
        m_uiModule->process(input);
        m_renderer->process(input);
    }

    void shutdown() {
        m_uiModule->shutdown(); m_renderer->shutdown();
        for (const char* n : {"renderer","ui_module","game"}) IntraIOManager::getInstance().removeInstance(n);
    }

private:
    void publishCamera() {
        auto cam=std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
        cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",m_w); cam->setInt("viewportH",m_h);
        m_gIO->publish("render:camera", std::move(cam));
    }
    void toggleDrawer() {
        auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","fleetDrawer");
        m_gIO->publish("ui:drawer:toggle", std::move(d));
    }

    // QUOI : la flotte — chaque ship une vignette avec sa position de slot (host-calculée, comme les parts).
    void pushFleet() {
        static const char* kNames[8] = {"S.S. Aurora","S.S. Borealis","S.S. Cygnus","S.S. Draco",
                                        "S.S. Equinox","S.S. Falcon","S.S. Gemini","S.S. Helios"};
        json fleet = json::array();
        for (int i = 0; i < 8; ++i)
            fleet.push_back({ {"id", "ship" + std::to_string(i)}, {"name", kNames[i]},
                              {"vx", 14}, {"vy", 8 + i*120}, {"vw", 272}, {"vh", 110} });
        m_gIO->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"fleet", fleet} }));
    }

    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::unique_ptr<UIModule> m_uiModule;
    std::shared_ptr<IntraIO> m_rIOPtr, m_uIOPtr, m_gIOPtr;
    IIO* m_rIO=nullptr; IIO* m_uIO=nullptr; IIO* m_gIO=nullptr;
    int m_w=1280, m_h=720;
};

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return 1; }
    const int W = 1280, H = 720;
    SDL_Window* window = SDL_CreateWindow("GroveEngine — Vessel Screen (fleet drawer)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; SDL_Quit(); return 1; }

    VesselScreenDemo demo;
    if (!demo.init(window, W, H)) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

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
