/**
 * GroveEngine — INTERACTIVE demo of the ASYNC ASSET STREAMING system (phase 3), live in a real window.
 *
 * QUOI : une vraie fenêtre redimensionnable qui rend une grille de sprites référencés par STRING assetId,
 *        avec assetAsyncLoad ON. Chaque cellule démarre en placeholder MAGENTA (texture pas encore résidente,
 *        décodage en vol sur le worker thread) puis POP-IN vers la vraie texture une fois que le worker a
 *        décodé et que pumpAsync() (dans process()) a uploadé. Une "vague de rechargement" (auto toutes les
 *        ~2s, ou SPACE) décharge tout -> on REVOIT le streaming magenta->vaisseaux en boucle. HUD : resident X/N.
 *
 * POURQUOI : montrer le moteur qui tourne POUR DE VRAI avec le système d'assets — pas une capture headless.
 *            La preuve "ça marche" reste l'E2E (AssetAsyncModuleGpu) ; ici c'est pour VOIR tourner.
 *
 * Lancer depuis la RACINE du projet :  ./build/tests/test_async_stream_demo
 * Contrôles : SPACE = relance une vague de streaming · R = idem · ESC / fermer = quitter · étirer = resize.
 */

#define SDL_MAIN_HANDLED   // we provide our own main() (no SDL2main / WinMain hijack)
#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>

#include "BgfxRendererModule.h"
#include "Assets/AssetManager.h"
#include "Resources/ResourceCache.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

using namespace grove;

class AsyncStreamDemo {
public:
    bool init(SDL_Window* window, int w, int h) {
        m_w = w; m_h = h;
        SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(window, &wmi);

        m_rIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_gIOPtr = IntraIOManager::getInstance().createInstance("game");
        m_rIO = m_rIOPtr.get(); m_gIO = m_gIOPtr.get();

        // Renderer with the asset system + ASYNC load ON (1 decode thread so the streaming stays visible).
        m_renderer = std::make_unique<BgfxRendererModule>();
        {
            JsonDataNode c("config");
            c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setBool("vsync", true);
            c.setBool("assetAsyncLoad", true); c.setInt("assetDecodeThreads", 1);
            m_renderer->setConfiguration(c, m_rIO, nullptr);
        }
        rhi::IRHIDevice* dev = m_renderer->getDevice();
        if (!dev) { std::cerr << "no device\n"; return false; }
        ResourceCache* cache = m_renderer->getResourceCache();
        m_am = m_renderer->getAssetManager();
        if (!cache || !m_am) { std::cerr << "no asset system\n"; return false; }

        // Bright magenta placeholder -> a not-yet-streamed cell is obviously "loading".
        {
            std::vector<uint8_t> mag(8 * 8 * 4);
            for (size_t i = 0; i < 8 * 8; ++i) { mag[i*4]=255; mag[i*4+1]=0; mag[i*4+2]=255; mag[i*4+3]=255; }
            rhi::TextureDesc d; d.width = 8; d.height = 8; d.format = rhi::TextureDesc::RGBA8; d.mipLevels = 1;
            d.data = mag.data(); d.dataSize = static_cast<uint32_t>(mag.size());
            rhi::TextureHandle hnd = dev->createTexture(d);
            m_am->setPlaceholder(cache->registerTexture(hnd));
        }

        // N distinct asset ids (each a separate stream) cycling the 4 real ship PNGs.
        const char* files[4] = { "assets/textures/ship/cockpit.png", "assets/textures/ship/reactor.png",
                                 "assets/textures/ship/engine.png",  "assets/textures/ship/gun.png" };
        for (int i = 0; i < N; ++i) {
            char id[16]; std::snprintf(id, sizeof id, "ship/%02d", i);
            m_ids.push_back(id);
            m_am->registerAsset(id, files[i % 4], /*priority*/ 0);
        }

        std::cout << "=== GroveEngine — ASYNC ASSET STREAMING (live). SPACE/R = vague de streaming, ESC = quitte ===\n";
        return true;
    }

    void handleSDLEvent(SDL_Event& e) {
        if (e.type == SDL_WINDOWEVENT &&
            (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) {
            m_w = e.window.data1; m_h = e.window.data2;
            if (auto* dev = m_renderer->getDevice()) dev->reset(static_cast<uint16_t>(m_w), static_cast<uint16_t>(m_h));
        } else if (e.type == SDL_KEYDOWN &&
                   (e.key.keysym.sym == SDLK_SPACE || e.key.keysym.sym == SDLK_r)) {
            reloadWave();   // unload everything -> the grid re-streams from magenta
        }
    }

    void frame(float dt) {
        // Auto reload wave every ~2s so there's continuous visible streaming activity.
        m_timer += dt;
        if (m_timer >= 2.0f) { m_timer = 0.0f; reloadWave(); }

        // Camera (full frame, follows resize).
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",m_w); cam->setInt("viewportH",m_h);
          m_gIO->publish("render:camera", std::move(cam)); }

        // Republish the grid each frame (sprites are ephemeral). Cells span the current window size.
        const int marginX = 40, top = 70, cell = std::max(40, (m_w - 2*marginX) / COLS), gap = 14;
        const int sprite = cell - gap;
        for (int i = 0; i < N; ++i) {
            const int col = i % COLS, row = i / COLS;
            auto s = std::make_unique<JsonDataNode>("d");
            s->setDouble("x", marginX + col * cell); s->setDouble("y", top + row * cell);
            s->setDouble("scaleX", sprite); s->setDouble("scaleY", sprite);
            s->setString("asset", m_ids[i]); s->setInt("layer", 1000);
            m_gIO->publish("render:sprite", std::move(s));
        }

        // HUD (screen space): live resident count so the streaming is legible as a number too.
        { const size_t res = m_am->residentCount();
          auto t = std::make_unique<JsonDataNode>("d");
          t->setString("text", "GroveEngine - async asset streaming   resident " + std::to_string(res) + "/" +
                               std::to_string(N) + "   [SPACE] reload wave   [ESC] quit");
          t->setDouble("x", 40); t->setDouble("y", 28); t->setInt("fontSize", 22);
          t->setInt("layer", 2000); t->setString("space", "screen");
          m_gIO->publish("render:text", std::move(t)); }

        JsonDataNode input("input"); input.setDouble("deltaTime", dt);
        m_renderer->process(input);   // process() runs pumpAsync() before collect -> uploads finished decodes
    }

    void shutdown() {
        m_renderer->shutdown();
        for (const char* n : {"renderer","game"}) IntraIOManager::getInstance().removeInstance(n);
    }

private:
    // Unload every asset -> next frame's resolve re-requests each off-thread -> the whole grid re-streams.
    void reloadWave() { for (const auto& id : m_ids) m_am->unload(id); }

    static const int COLS = 8;
    static const int N = 48;   // 8 x 6
    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::shared_ptr<IntraIO> m_rIOPtr, m_gIOPtr;
    IIO* m_rIO = nullptr; IIO* m_gIO = nullptr;
    assets::AssetManager* m_am = nullptr;
    std::vector<std::string> m_ids;
    float m_timer = 0.0f;
    int m_w = 900, m_h = 640;
};

int main(int, char**) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return 1; }
    const int W = 900, H = 640;
    SDL_Window* window = SDL_CreateWindow("GroveEngine - Async Asset Streaming (live)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; SDL_Quit(); return 1; }

    AsyncStreamDemo demo;
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
