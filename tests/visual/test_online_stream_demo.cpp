/**
 * GroveEngine — INTERACTIVE demo: stream textures from an ONLINE image bank (picsum.photos) through the
 * asset system, live in a real window. Proves the streaming/async/budget pipeline works with REMOTE sources
 * (real network latency → genuinely gradual fill, unlike local PNGs that pop in within a frame).
 *
 * QUOI : une grille de sprites référencés par assetId dont le "path" est une URL. Un décodeur async maison
 *        (CurlAsyncDecoder) télécharge chaque image sur un pool de worker threads (curl → fichier temp →
 *        decodeRgba), et le pumpAsync() du module uploade chaque image dès qu'elle arrive — donc ça
 *        s'affiche AUSSI VITE QU'ON TÉLÉCHARGE (N téléchargements en parallèle, upload à la frame suivante).
 *        Placeholder magenta tant que pas arrivé. HUD : loaded X/N + temps de la vague.
 *
 * POURQUOI : tester le streaming depuis une banque en ligne (demandé). L'interface IAsyncDecoder du moteur
 *            est swappable par design → la source (fichiers locaux / atlas / HTTP) se change sans toucher le
 *            cœur. Le HTTP/TLS reste CÔTÉ DÉMO (shell vers curl) — aucune dépendance ajoutée au moteur.
 *
 * Lancer depuis la RACINE du projet :  ./build/tests/test_online_stream_demo
 * Contrôles : SPACE/R = recharge (re-télécharge) · étirer = resize · ESC / fermer = quitter.
 * Réseau requis (picsum.photos). Échec de download d'une image -> elle reste en placeholder (pas de crash).
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include "BgfxRendererModule.h"
#include "Assets/AssetManager.h"
#include "Resources/ResourceCache.h"
#include "Resources/TextureLoader.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <cstdlib>
#include <cstdio>

using namespace grove;

// ---------------------------------------------------------------------------------------------------------
// CurlAsyncDecoder — a demo-side IAsyncDecoder that DOWNLOADS each asset's URL on a worker-thread pool, then
// decodes it (TextureLoader::decodeRgba). The engine's pumpAsync() uploads the result on the render thread.
// HTTP/TLS is handled by shelling to `curl` (present on Win10+/git) so no HTTP dependency touches the engine.
// ---------------------------------------------------------------------------------------------------------
class CurlAsyncDecoder : public assets::IAsyncDecoder {
public:
    explicit CurlAsyncDecoder(int workers = 8) {
        if (workers < 1) workers = 1;
        m_tmp = std::filesystem::temp_directory_path();
        for (int i = 0; i < workers; ++i) m_threads.emplace_back([this] { workerLoop(); });
    }
    ~CurlAsyncDecoder() override {
        { std::lock_guard<std::mutex> lk(m_mx); m_stop = true; }
        m_cv.notify_all();
        for (auto& t : m_threads) if (t.joinable()) t.join();
    }

    void request(const std::string& id, const std::string& url) override {
        { std::lock_guard<std::mutex> lk(m_mx); m_jobs.push_back({id, url}); }
        ++m_pending;
        m_cv.notify_one();
    }
    void poll(std::vector<assets::DecodedImage>& out) override {
        std::lock_guard<std::mutex> lk(m_doneMx);
        for (auto& d : m_done) out.push_back(std::move(d));
        m_done.clear();
    }
    size_t pending() const override { return m_pending.load(); }

private:
    struct Job { std::string id, url; };

    void workerLoop() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(m_mx);
                m_cv.wait(lk, [this] { return m_stop || !m_jobs.empty(); });
                if (m_stop && m_jobs.empty()) return;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            assets::DecodedImage d;
            d.id = job.id;
            // Download to a unique temp file, then decode it. Both are the slow part — off the render thread.
            const unsigned n = m_counter++;
            const std::filesystem::path tmp = m_tmp / ("grove_online_" + std::to_string(n) + ".img");
            const std::string cmd = "curl -s -L --max-time 25 -o \"" + tmp.string() + "\" \"" + job.url + "\"";
            const int rc = std::system(cmd.c_str());
            if (rc == 0) d.ok = TextureLoader::decodeRgba(tmp.string(), d.pixels, d.w, d.h);
            std::error_code ec; std::filesystem::remove(tmp, ec);
            { std::lock_guard<std::mutex> lk(m_doneMx); m_done.push_back(std::move(d)); }
            --m_pending;
        }
    }

    std::vector<std::thread> m_threads;
    std::mutex m_mx; std::condition_variable m_cv; std::deque<Job> m_jobs; bool m_stop = false;
    std::mutex m_doneMx; std::vector<assets::DecodedImage> m_done;
    std::atomic<size_t> m_pending{0};
    std::atomic<unsigned> m_counter{0};
    std::filesystem::path m_tmp;
};

// ---------------------------------------------------------------------------------------------------------

class OnlineStreamDemo {
public:
    bool init(SDL_Window* window, int w, int h) {
        m_w = w; m_h = h;
        SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(window, &wmi);

        m_rIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_gIOPtr = IntraIOManager::getInstance().createInstance("game");
        m_rIO = m_rIOPtr.get(); m_gIO = m_gIOPtr.get();

        m_renderer = std::make_unique<BgfxRendererModule>();
        {
            JsonDataNode c("config");
            c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
            c.setInt("windowWidth", m_w); c.setInt("windowHeight", m_h); c.setBool("vsync", true);
            m_renderer->setConfiguration(c, m_rIO, nullptr);
        }
        rhi::IRHIDevice* dev = m_renderer->getDevice();
        if (!dev) { std::cerr << "no device\n"; return false; }
        ResourceCache* cache = m_renderer->getResourceCache();
        m_am = m_renderer->getAssetManager();
        if (!cache || !m_am) { std::cerr << "no asset system\n"; return false; }

        // Magenta placeholder = "still downloading".
        {
            std::vector<uint8_t> mag(8 * 8 * 4);
            for (size_t i = 0; i < 8 * 8; ++i) { mag[i*4]=255; mag[i*4+1]=0; mag[i*4+2]=255; mag[i*4+3]=255; }
            rhi::TextureDesc d; d.width = 8; d.height = 8; d.format = rhi::TextureDesc::RGBA8; d.mipLevels = 1;
            d.data = mag.data(); d.dataSize = static_cast<uint32_t>(mag.size());
            m_am->setPlaceholder(cache->registerTexture(dev->createTexture(d)));
        }

        // Plug the HTTP decoder into the asset system (the engine's pumpAsync drains it each frame).
        m_decoder = std::make_unique<CurlAsyncDecoder>(/*download threads*/ 8);
        m_am->setAsyncDecoder(m_decoder.get());

        // Register N assets whose "path" is a picsum URL (seeded -> deterministic distinct images).
        for (int i = 0; i < N; ++i) {
            char id[24]; std::snprintf(id, sizeof id, "online/%02d", i);
            m_ids.push_back(id);
            const std::string url = "https://picsum.photos/seed/grove" + std::to_string(i) + "/160/160";
            m_am->registerAsset(id, url, /*priority*/ 0);
        }

        std::cout << "=== GroveEngine — ONLINE image streaming (picsum.photos). SPACE/R = reload, ESC = quit ===\n";
        return true;
    }

    void handleSDLEvent(SDL_Event& e) {
        if (e.type == SDL_WINDOWEVENT &&
            (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) {
            m_w = e.window.data1; m_h = e.window.data2;
            if (auto* dev = m_renderer->getDevice()) dev->reset(static_cast<uint16_t>(m_w), static_cast<uint16_t>(m_h));
        } else if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_SPACE || e.key.keysym.sym == SDLK_r)) {
            for (const auto& id : m_ids) m_am->unload(id);   // drop all -> they re-stream (re-download)
            m_waveTime = 0.0f; m_waveDone = false;
        }
    }

    void frame(float dt) {
        const size_t res = m_am->residentCount();
        if (res < static_cast<size_t>(N)) m_waveTime += dt; else m_waveDone = true;

        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",m_w); cam->setInt("viewportH",m_h);
          m_gIO->publish("render:camera", std::move(cam)); }

        const int marginX = 30, top = 64, cell = std::max(48, (m_w - 2*marginX) / COLS), gap = 12;
        const int sprite = cell - gap;
        for (int i = 0; i < N; ++i) {
            const int col = i % COLS, row = i / COLS;
            auto s = std::make_unique<JsonDataNode>("d");
            s->setDouble("cx", marginX + col * cell); s->setDouble("cy", top + row * cell);
            s->setDouble("scaleX", sprite); s->setDouble("scaleY", sprite);
            s->setString("asset", m_ids[i]); s->setInt("layer", 1000);
            m_gIO->publish("render:sprite", std::move(s));
        }

        { char buf[160];
          std::snprintf(buf, sizeof buf, "Online streaming (picsum.photos)  -  loaded %zu/%d  in %.1fs%s   [SPACE] reload",
                        res, N, m_waveTime, m_waveDone ? " (done)" : "");
          auto t = std::make_unique<JsonDataNode>("d");
          t->setString("text", buf); t->setDouble("x", 30); t->setDouble("y", 26); t->setInt("fontSize", 20);
          t->setInt("layer", 2000); t->setString("space", "screen");
          m_gIO->publish("render:text", std::move(t)); }

        JsonDataNode input("input"); input.setDouble("deltaTime", dt);
        m_renderer->process(input);   // pumpAsync() uploads whatever just finished downloading
    }

    void shutdown() {
        m_am->setAsyncDecoder(nullptr);   // stop draining before the decoder is destroyed
        m_decoder.reset();                // joins the download threads
        m_renderer->shutdown();
        for (const char* n : {"renderer","game"}) IntraIOManager::getInstance().removeInstance(n);
    }

private:
    static const int COLS = 6;
    static const int N = 30;   // 6 x 5
    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::unique_ptr<CurlAsyncDecoder> m_decoder;
    std::shared_ptr<IntraIO> m_rIOPtr, m_gIOPtr;
    IIO* m_rIO = nullptr; IIO* m_gIO = nullptr;
    assets::AssetManager* m_am = nullptr;
    std::vector<std::string> m_ids;
    float m_waveTime = 0.0f; bool m_waveDone = false;
    int m_w = 900, m_h = 640;
};

int main(int, char**) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n"; return 1; }
    const int W = 900, H = 640;
    SDL_Window* window = SDL_CreateWindow("GroveEngine - Online Image Streaming (live)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; SDL_Quit(); return 1; }

    OnlineStreamDemo demo;
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
