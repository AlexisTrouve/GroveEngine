/**
 * GroveEngine — 9-slice ELABORATE-ART demo (in-engine).
 *
 * QUOI : charge quatre cadres PNG 128x128 (dessines en SVG, rasterises) comme ASSETS moteur, et les affiche
 *        via le 9-slice sur des boutons (verre) et des fenetres (HUD sci-fi / ornemental / parchemin). C'est le
 *        vrai pipeline : asset PNG streame -> render:nineslice -> SpritePass. Fenetres draggables/resizables.
 *
 * POURQUOI : prouver EN MOTEUR (pas un compositeur externe) que le 9-slice rend de l'art elabore avec des
 *        coins nets a toute taille — la galerie SVG->PNG montrait la meme decoupe hors moteur ; ceci est le
 *        rendu GPU reel, capturable par --shot.
 *
 * Lancer depuis la RACINE :  ./build/tests/test_nineslice_art_demo   [--shot out.png]
 * Controles : survole/clique les boutons, drag la barre de titre / le coin d'une fenetre, ESC quitte.
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "BgfxRendererModule.h"
#include "UIModule.h"
#include "RHI/RHIDevice.h"
#include "PngCapture.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

using namespace grove;

class NineSliceArtDemo {
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
            m_renderer->setConfiguration(c, m_rIO, nullptr);
        }
        if (!m_renderer->getDevice()) { std::cerr << "no GPU device\n"; return false; }

        // 1. Register the four frame PNGs as streamed assets, then preload them, BEFORE the UI renders — so
        //    the first render:nineslice:add resolves each `frame.asset` to a resident texture (an id resolved
        //    at collect time would latch texId 0 = white if not yet loaded).
        registerFrame("frame_tech",      "assets/textures/ui/frame_tech.png");
        registerFrame("frame_ornate",    "assets/textures/ui/frame_ornate.png");
        registerFrame("frame_glossy",    "assets/textures/ui/frame_glossy.png");
        registerFrame("frame_parchment", "assets/textures/ui/frame_parchment.png");
        registerFrame("frame_standard",  "assets/textures/ui/frame_standard.png");
        { auto d = std::make_unique<JsonDataNode>("d"); d->setString("group", "frames");
          m_gIO->publish("asset:preload", std::move(d)); }
        for (int i = 0; i < 3; ++i) { publishCamera(); JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); m_renderer->process(in); }

        // 2. Bring up the UI (its first nineslice:add now resolves the resident frame textures).
        m_uiModule = std::make_unique<UIModule>();
        {
            JsonDataNode c("config");
            c.setString("layoutFile", "assets/ui/demo_nineslice_art.json");
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setInt("baseLayer", 1000);
            m_uiModule->setConfiguration(c, m_uIO, nullptr);
        }
        std::cout << "=== 9-slice art demo : survole/clique, drag/etire les fenetres, ESC quitte ===\n";
        return true;
    }

    void handleSDLEvent(SDL_Event& e) {
        if (e.type == SDL_WINDOWEVENT &&
            (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) {
            m_w = e.window.data1; m_h = e.window.data2;
            if (auto* dev = m_renderer->getDevice()) dev->reset(static_cast<uint16_t>(m_w), static_cast<uint16_t>(m_h));
            { auto d=std::make_unique<JsonDataNode>("d"); d->setInt("width",m_w); d->setInt("height",m_h); m_gIO->publish("ui:resize", std::move(d)); }
        } else if (e.type == SDL_MOUSEMOTION) {
            auto d=std::make_unique<JsonDataNode>("d"); d->setDouble("x",e.motion.x); d->setDouble("y",e.motion.y);
            m_gIO->publish("input:mouse:move", std::move(d));
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            const int b = (e.button.button == SDL_BUTTON_RIGHT) ? 1 : (e.button.button == SDL_BUTTON_MIDDLE ? 2 : 0);
            auto d=std::make_unique<JsonDataNode>("d");
            d->setInt("button", b); d->setBool("pressed", e.type == SDL_MOUSEBUTTONDOWN);
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

    bool captureShot(const std::string& path) {
        rhi::IRHIDevice* dev = m_renderer->getDevice();
        rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(m_w), static_cast<uint16_t>(m_h));
        dev->setViewFramebuffer(0, fb); dev->setViewFramebuffer(1, fb);
        for (int i = 0; i < 6; ++i) frame(0.016f);
        std::vector<uint8_t> rgba(static_cast<size_t>(m_w) * m_h * 4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) return false;
        // Smoke: count cyan-ish (tech frame) + gold-ish (ornate frame) pixels; ~0 both = frames didn't resolve.
        int cyan = 0, gold = 0;
        for (size_t i = 0; i < static_cast<size_t>(m_w) * m_h; ++i) {
            const int r = rgba[i*4], g = rgba[i*4+1], b = rgba[i*4+2];
            if (b > 150 && g > 150 && r < 130) ++cyan;
            else if (r > 150 && g > 130 && b < 120) ++gold;
        }
        std::cout << "shot: cyan(tech)=" << cyan << " gold(ornate)=" << gold << " px\n";
        return grove::mvdemo::writeRgbaAsPng(path, m_w, m_h, rgba);
    }

    void shutdown() {
        if (m_uiModule) m_uiModule->shutdown();
        m_renderer->shutdown();
        for (const char* n : {"renderer","ui_module","game"}) IntraIOManager::getInstance().removeInstance(n);
    }

private:
    void publishCamera() {
        auto cam=std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
        cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",m_w); cam->setInt("viewportH",m_h);
        m_gIO->publish("render:camera", std::move(cam));
    }
    void registerFrame(const char* id, const char* path) {
        auto d = std::make_unique<JsonDataNode>("d");
        d->setString("id", id); d->setString("path", path); d->setString("group", "frames"); d->setInt("priority", 10);
        m_gIO->publish("asset:register", std::move(d));
    }

    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::unique_ptr<UIModule> m_uiModule;
    std::shared_ptr<IntraIO> m_rIOPtr, m_uIOPtr, m_gIOPtr;
    IIO* m_rIO=nullptr; IIO* m_uIO=nullptr; IIO* m_gIO=nullptr;
    int m_w=1280, m_h=720;
};

int main(int argc, char** argv) {
    std::string shotPath; bool shot = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--shot") { shot = true; if (i + 1 < argc && argv[i+1][0] != '-') shotPath = argv[++i]; }
    }
    if (shot && shotPath.empty()) shotPath = "nineslice_art.png";

    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return 1; }
    const int W = 1280, H = 720;
    SDL_Window* window = SDL_CreateWindow("GroveEngine — 9-slice elaborate art",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
        shot ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE));
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; SDL_Quit(); return 1; }

    NineSliceArtDemo demo;
    if (!demo.init(window, W, H)) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    if (shot) {
        const bool ok = demo.captureShot(shotPath);
        std::cout << (ok ? "wrote " : "FAILED to write ") << shotPath << "\n";
        demo.shutdown(); SDL_DestroyWindow(window); SDL_Quit();
        return ok ? 0 : 1;
    }

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
