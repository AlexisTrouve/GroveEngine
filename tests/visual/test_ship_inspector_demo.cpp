/**
 * GroveEngine — INTERACTIVE ship-inspector demo (SMART RESIZE).
 *
 * QUOI : une vraie fenêtre redimensionnable qui charge le MEME JSON responsive que la suite E2E
 *        (assets/ui/demo_ship_inspector.json) + les 4 sprites de pièces (SVG -> PNG). On clique une
 *        pièce de la maquette -> le panneau info se met à jour en live ; on étire la fenêtre -> tout
 *        reflow (la fenêtre % grandit, le blueprint flex s'élargit, le panneau info 220px suit le bord).
 *
 * POURQUOI : prouver "à la main" ce que IT_044 (sprites-as-UI + parts cliquables) et IT_045 (reflow sur
 *            ui:resize) verrouillent headless. La démo n'a pas d'assertions — la preuve, c'est l'E2E ;
 *            ici c'est pour VOIR et cliquer.
 *
 * COMMENT : on construit BgfxRendererModule + UIModule en statique (pour atteindre getDevice()->reset()
 *           au resize, impossible via ModuleLoader). Boucle SDL :
 *             - SIZE_CHANGED -> device->reset(w,h) (backbuffer bgfx) + render:camera (viewport) + ui:resize
 *             - souris -> input:mouse:* ; ship:part (clic d'une pièce) -> ui:data:merge {selectedPart,noPart}
 *
 * Lancer depuis la RACINE du projet :  ./build/tests/test_ship_inspector_demo
 * Contrôles : étirer un coin = resize live · clic sur une pièce = info · ESC quitte.
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

class ShipInspectorDemo {
public:
    // QUOI : câble renderer (avec les 4 textures de pièces) + UIModule (JSON responsive) et pousse le vaisseau.
    bool init(SDL_Window* window, int w, int h) {
        m_w = w; m_h = h;
        SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(window, &wmi);

        m_rendererIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_uiIOPtr       = IntraIOManager::getInstance().createInstance("ui_module");
        m_gameIOPtr     = IntraIOManager::getInstance().createInstance("game");
        m_rendererIO = m_rendererIOPtr.get(); m_uiIO = m_uiIOPtr.get(); m_gameIO = m_gameIOPtr.get();

        // Renderer : on enregistre les sprites SVG->PNG sur les ids de texture 1..4 (le template lie {{tex}}).
        m_renderer = std::make_unique<BgfxRendererModule>();
        {
            JsonDataNode c("config");
            c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setBool("vsync", true);
            c.setString("texture1", "assets/textures/ship/cockpit.png");
            c.setString("texture2", "assets/textures/ship/reactor.png");
            c.setString("texture3", "assets/textures/ship/engine.png");
            c.setString("texture4", "assets/textures/ship/gun.png");
            m_renderer->setConfiguration(c, m_rendererIO, nullptr);
        }

        // UIModule : la fenêtre + la maquette + le panneau info sont entièrement décrits en JSON.
        m_uiModule = std::make_unique<UIModule>();
        {
            JsonDataNode c("config");
            c.setString("layoutFile", "assets/ui/demo_ship_inspector.json");   // lancer depuis la racine
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setInt("baseLayer", 1000);
            m_uiModule->setConfiguration(c, m_uiIO, nullptr);
        }

        // "Le jeu" : un clic sur une pièce émet ship:part {id,label,stat} -> on relaie en selectedPart (reactif).
        m_gameIO->subscribe("ship:part", [this](const Message& m){
            json patch = { {"noPart", false},
                           {"selectedPart", {{"label", m.data->getString("label","")},
                                             {"stat",  m.data->getString("stat","")}}} };
            pushMerge(std::move(patch));
        });

        pushShip();
        pushResources();
        std::cout << "=== Ship inspector — etire un coin pour resize, clic une piece, clic 'Ressources' pour deplier (scroll), ESC quitte ===\n";
        return true;
    }

    // QUOI : route les events SDL vers l'UI. POURQUOI : le resize doit reset le backbuffer + relayouter l'UI.
    void handleSDLEvent(SDL_Event& e) {
        if (e.type == SDL_WINDOWEVENT &&
            (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) {
            // COMMENT : 1) backbuffer bgfx à la nouvelle taille, 2) viewport caméra, 3) ui:resize -> root % relayouté.
            m_w = e.window.data1; m_h = e.window.data2;
            if (auto* dev = m_renderer->getDevice())
                dev->reset(static_cast<uint16_t>(m_w), static_cast<uint16_t>(m_h));
            publishCamera();
            { auto d = std::make_unique<JsonDataNode>("d"); d->setInt("width", m_w); d->setInt("height", m_h);
              m_gameIO->publish("ui:resize", std::move(d)); }
        } else if (e.type == SDL_MOUSEMOTION) {
            auto d = std::make_unique<JsonDataNode>("d");
            d->setDouble("x", static_cast<double>(e.motion.x)); d->setDouble("y", static_cast<double>(e.motion.y));
            m_gameIO->publish("input:mouse:move", std::move(d));
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            auto d = std::make_unique<JsonDataNode>("d");
            d->setInt("button", e.button.button - 1);
            d->setBool("pressed", e.type == SDL_MOUSEBUTTONDOWN);
            d->setDouble("x", static_cast<double>(e.button.x)); d->setDouble("y", static_cast<double>(e.button.y));
            m_gameIO->publish("input:mouse:button", std::move(d));
        }
    }

    void frame(float dt) {
        while (m_gameIO->hasMessages() > 0) m_gameIO->pullAndDispatch();   // drainer ship:part -> merge
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
    // QUOI : la caméra = plein cadre, viewport à la taille courante (suit le resize chaque frame).
    void publishCamera() {
        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
        cam->setInt("viewportX",0); cam->setInt("viewportY",0);
        cam->setInt("viewportW", m_w); cam->setInt("viewportH", m_h);
        m_gameIO->publish("render:camera", std::move(cam));
    }
    void pushMerge(json patch) { m_gameIO->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", std::move(patch))); }

    // QUOI : le vaisseau — pièces en BLOCS couleur (tex 0) + SPRITES (tex 1..4 issus des SVG->PNG).
    void pushShip() {
        auto part = [](const char* id,int x,int y,int w,int h,const char* col,int tex,const char* lbl,const char* st){
            return json{ {"id",id},{"x",x},{"y",y},{"w",w},{"h",h},{"color",col},{"tex",tex},{"label",lbl},{"stat",st} };
        };
        json parts = json::array({
            part("cockpit",160,10,80,80,  "0xFFFFFFFF",1,"Cockpit","PV 120  Equipage 2"),
            part("hullA",  140,90,120,44, "0x3a4a63FF",0,"Coque avant","PV 80"),
            part("gunL",   66,100,64,64,  "0xFFFFFFFF",4,"Canon babord","Degats 14  Portee 600"),
            part("gunR",   270,100,64,64, "0xFFFFFFFF",4,"Canon tribord","Degats 14  Portee 600"),
            part("hullM",  130,134,140,60,"0x46587aFF",0,"Coque centrale","PV 140"),
            part("reactor",160,190,80,80, "0xFFFFFFFF",2,"Reacteur","Energie +60  PV 90"),
            part("wingL",  40,196,70,44,  "0x2e3c54FF",0,"Aile babord","PV 50"),
            part("wingR",  290,196,70,44, "0x2e3c54FF",0,"Aile tribord","PV 50"),
            part("hullB",  140,272,120,44,"0x3a4a63FF",0,"Coque arriere","PV 80"),
            part("engL",   108,312,70,92, "0xFFFFFFFF",3,"Moteur babord","Poussee +35"),
            part("engR",   222,312,70,92, "0xFFFFFFFF",3,"Moteur tribord","Poussee +35")
        });
        // noPart:true au départ -> le hint "Clique une piece" s'affiche tant qu'on n'a rien sélectionné.
        m_gameIO->publish("ui:data", std::make_unique<JsonDataNode>("d", json{
            {"ship", {{"name","S.S. Aurora"},{"parts",parts}}},
            {"noPart", true},
            {"selectedPart", {{"label","-"},{"stat",""}}}
        }));
    }

    // QUOI : les 50 stocks de ressources -> un groupe UNIQUE replié ("menu folded"). POURQUOI : déplier ->
    //   50 lignes > hauteur de la liste -> scrollbar + molette (le "ça active le scroll" du spec). COMMENT :
    //   on pousse un groupe collapsed:true sur la liste "resources" ; le clic du header (câblé par UIModule)
    //   le déplie et émet ui:list:group:toggled.
    void pushResources() {
        static const char* kNames[50] = {
            "Fer","Cuivre","Or","Argent","Titane","Aluminium","Nickel","Cobalt","Lithium","Uranium",
            "Platine","Tungstene","Silicium","Carbone","Hydrogene","Helium","Oxygene","Azote","Methane","Ammoniac",
            "Glace","Eau","Deuterium","Tritium","Antimatiere","Plasma","Cristaux","Quartz","Diamant","Graphene",
            "Polymere","Composite","Acier","Bronze","Circuits","Processeurs","Capteurs","Alliage","Ceramique","Isotopes",
            "Catalyseur","Solvant","Carburant","Oxydant","Munitions","Vivres","Medicaments","Semences","Pieces","Outils"
        };
        json items = json::array();
        for (int i = 0; i < 50; ++i) {
            const int stock = (i * 37 + 12) % 980 + 7;          // varied, deterministic stock count
            char id[16]; std::snprintf(id, sizeof id, "r%02d", i);
            items.push_back({ {"id", id}, {"label", kNames[i]}, {"subtitle", "x" + std::to_string(stock)} });
        }
        json groups = json::array({ { {"id","stock"}, {"label","Ressources (50)"}, {"collapsed", true}, {"items", items} } });
        m_gameIO->publish("ui:list:set_groups", std::make_unique<JsonDataNode>("d", json{ {"id","resources"}, {"groups", groups} }));
    }

    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::unique_ptr<UIModule> m_uiModule;
    std::shared_ptr<IntraIO> m_rendererIOPtr, m_uiIOPtr, m_gameIOPtr;
    IIO* m_rendererIO = nullptr; IIO* m_uiIO = nullptr; IIO* m_gameIO = nullptr;
    int m_w = 1100, m_h = 720;
};

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return 1; }
    const int W = 1100, H = 720;
    SDL_Window* window = SDL_CreateWindow("GroveEngine — Ship Inspector (smart resize)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; SDL_Quit(); return 1; }

    ShipInspectorDemo demo;
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
