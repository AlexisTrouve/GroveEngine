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
#include <algorithm>

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

        // "Flotte" toggles the panel. LEFT-click a ship = select it (highlight); RIGHT-click = open the
        // inspector. A group label selects the whole group. Clicking a PART inside the inspector relays into
        // the info panel (reactive selectedPart).
        m_gIO->subscribe("vessel:drawer", [this](const Message&){ toggleFleet(); });
        m_gIO->subscribe("vessel:open",   [this](const Message& m){ openInspector(m.data->getString("name","")); });
        m_gIO->subscribe("vessel:select", [this](const Message& m){ m_selId = m.data->getString("id",""); m_selGroup.clear(); repushSlots(); });
        m_gIO->subscribe("vessel:selectGroup", [this](const Message& m){ m_selGroup = m.data->getString("group",""); m_selId.clear(); repushSlots(); });
        m_gIO->subscribe("ship:part",     [this](const Message& m){
            json patch = { {"noPart", false}, {"selectedPart", {{"label", m.data->getString("label","")},
                                                                {"stat",  m.data->getString("stat","")}}} };
            m_gIO->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", std::move(patch)));
        });

        pushFleet();
        std::cout << "=== Vessel screen — bouton 'Flotte' (ou F) ouvre le menu (slide), clic une icone, ESC quitte ===\n";
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
            toggleFleet();                                   // F toggles (coordinate-free, handy for capture)
        } else if (e.type == SDL_MOUSEMOTION) {
            auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",e.motion.x); d->setDouble("y",e.motion.y);
            m_gIO->publish("input:mouse:move", std::move(d));
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            auto d=std::make_unique<JsonDataNode>("d");
            d->setInt("button", e.button.button - 1); d->setBool("pressed", e.type == SDL_MOUSEBUTTONDOWN);
            d->setDouble("x", e.button.x); d->setDouble("y", e.button.y);
            m_gIO->publish("input:mouse:button", std::move(d));
        } else if (e.type == SDL_MOUSEWHEEL) {
            // Forward the wheel so the hovered scrollpanel (e.g. the inspector's inventory grid) scrolls.
            auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("delta", static_cast<double>(e.wheel.y));
            m_gIO->publish("input:mouse:wheel", std::move(d));
        }
    }

    void frame(float dt) {
        while (m_gIO->hasMessages() > 0) m_gIO->pullAndDispatch();
        animateFleetSlide(dt);
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
    // QUOI : ouvre/ferme le menu flotte. POURQUOI : "caché hors-écran -> slide depuis la gauche". COMMENT :
    //   on bascule la cible m_fleetOpen ; à l'ouverture on rend le panneau visible tout de suite (il glisse
    //   depuis le bord gauche) ; animateFleetSlide lerpe sa position chaque frame et le cache à la fermeture.
    void toggleFleet() {
        m_fleetOpen = !m_fleetOpen;
        if (m_fleetOpen && !m_fleetShown) setFleetVisible(true);
    }
    void setFleetVisible(bool v) {
        m_fleetShown = v;
        auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","fleetPanel"); d->setBool("visible", v);
        m_gIO->publish("ui:set_visible", std::move(d));
    }
    // Slide the top-left panel between off-screen-left (closed) and its docked x (open), host-driven.
    void animateFleetSlide(float dt) {
        const float kOpenX = 12.0f, kClosedX = -260.0f;   // panel width 252 -> fully off the left edge
        const float target = m_fleetOpen ? kOpenX : kClosedX;
        m_fleetX += (target - m_fleetX) * std::min(1.0f, dt * 12.0f);
        if (m_fleetShown) {
            auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","fleetPanel");
            d->setDouble("x", m_fleetX); d->setDouble("y", 52.0);
            m_gIO->publish("ui:set_position", std::move(d));
        }
        if (!m_fleetOpen && m_fleetShown && m_fleetX < kClosedX + 3.0f) setFleetVisible(false);  // fully closed
    }

    // QUOI : la flotte = 3 groupes de contrôle EMPILÉS verticalement ; chaque groupe = un label + une rangée
    //   d'ICÔNES horizontale (pas de noms). COMMENT : `groups` -> les labels empilés (ly = ligne du groupe) ;
    //   `slots` -> la grille d'icônes (ix horizontal dans la rangée, iy = ligne du groupe + 20). Deux repeaters
    //   à plat (positions host-calculées), pas d'imbrication.
    void pushFleet() {
        const int sizes[3] = {5, 4, 3};
        const char* groupNames[3] = {"Alpha", "Bravo", "Reserve"};
        const char* shipNames[12] = {"Aurora","Borealis","Cygnus","Draco","Equinox","Falcon",
                                     "Gemini","Helios","Icarus","Juno","Kestrel","Lyra"};
        json groups = json::array();
        m_fleet = json::array();
        int idx = 0;
        for (int g = 0; g < 3; ++g) {
            groups.push_back({ {"name", groupNames[g]}, {"ly", g * 64} });
            for (int k = 0; k < sizes[g]; ++k) {
                m_fleet.push_back({ {"id","ship"+std::to_string(idx)}, {"name",shipNames[idx]}, {"group",groupNames[g]},
                                    {"ix", k*46}, {"iy", g*64+20}, {"icon", 1+(idx%4)} });
                ++idx;
            }
        }
        m_gIO->publish("ui:data", std::make_unique<JsonDataNode>("d", json{ {"groups", groups}, {"slots", slotsWithBorders()} }));
    }

    // QUOI : reconstruit les slots avec, par vaisseau, une bordure de REPOS = highlight si sélectionné (par id
    //   OU par groupe). POURQUOI : la sélection est data-driven — on re-pousse les slots, le repeater re-bind
    //   borderColor sur chaque icône. Highlight = doré ; repos = bord normal.
    json slotsWithBorders() {
        json slots = json::array();
        for (const auto& s : m_fleet) {
            const bool sel = (s.value("id","") == m_selId) ||
                             (!m_selGroup.empty() && s.value("group","") == m_selGroup);
            json slot = s;
            slot["border"] = sel ? "0xffd166FF" : "0x2a3650FF";
            slots.push_back(std::move(slot));
        }
        return slots;
    }
    void repushSlots() {
        m_gIO->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", json{ {"slots", slotsWithBorders()} }));
    }

    // QUOI : ouvre l'inspector sur le vaisseau cliqué. POURQUOI : feature C ("clic -> window avec la maquette
    //   en gros"). COMMENT : pousse le blueprint (pièces) + nom, les 50 ressources (repliées), puis révèle la
    //   fenêtre. Le clic d'une pièce -> ship:part -> merge selectedPart (déjà câblé en init).
    void openInspector(const std::string& name) {
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
        // MERGE (not replace) so the fleet data (groups/slots) survives — ui:data would clobber the tree.
        m_gIO->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", json{
            {"ship", {{"name", name.empty() ? std::string("Vaisseau") : name}, {"parts", parts}}},
            {"noPart", true}, {"selectedPart", {{"label","-"},{"stat",""}}} }));
        pushResources();
        { auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","inspector"); d->setBool("visible", true);
          m_gIO->publish("ui:set_visible", std::move(d)); }
    }

    // The inspector's 50-resource INVENTORY GRID (icon + count cells, 4 columns, host-computed grid
    // positions so the scrollpanel can scroll them). Re-pushed (merged) on each open.
    void pushResources() {
        static const char* kNames[50] = {
            "Fer","Cuivre","Or","Argent","Titane","Aluminium","Nickel","Cobalt","Lithium","Uranium",
            "Platine","Tungstene","Silicium","Carbone","Hydrogene","Helium","Oxygene","Azote","Methane","Ammoniac",
            "Glace","Eau","Deuterium","Tritium","Antimatiere","Plasma","Cristaux","Quartz","Diamant","Graphene",
            "Polymere","Composite","Acier","Bronze","Circuits","Processeurs","Capteurs","Alliage","Ceramique","Isotopes",
            "Catalyseur","Solvant","Carburant","Oxydant","Munitions","Vivres","Medicaments","Semences","Pieces","Outils"
        };
        json inv = json::array();
        for (int i = 0; i < 50; ++i) {
            const int stock = (i * 37 + 12) % 980 + 7;
            char id[16]; std::snprintf(id, sizeof id, "r%02d", i);
            inv.push_back({ {"id", id}, {"name", kNames[i]}, {"icon", 1+(i%4)}, {"count", stock},
                            {"cx", (i%4)*54 + 2}, {"cy", (i/4)*54 + 2} });
        }
        m_gIO->publish("ui:data:merge", std::make_unique<JsonDataNode>("d", json{ {"inventory", inv} }));
    }

    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::unique_ptr<UIModule> m_uiModule;
    std::shared_ptr<IntraIO> m_rIOPtr, m_uIOPtr, m_gIOPtr;
    IIO* m_rIO=nullptr; IIO* m_uIO=nullptr; IIO* m_gIO=nullptr;
    int m_w=1280, m_h=720;
    bool m_fleetOpen=false, m_fleetShown=false;   // fleet-menu slide state
    float m_fleetX=-260.0f;                        // current slide x (off-screen left = closed)
    json m_fleet;                                  // base fleet model (re-pushed with selection borders)
    std::string m_selId, m_selGroup;               // current selection (by ship id or by group)
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
