// ============================================================================
//  GROS PUTAIN DE SAUVAGE — BgfxRenderer rendering throughput benchmark
// ============================================================================
//
// WHAT: opens a real D3D11 window and HAMMERS the BgfxRenderer with a growing
//   flood of primitives — textured sprites (atlas/streaming), particles, text
//   glyphs, and a monster tilemap — until the frame time melts. For each regime
//   it ramps the count, measures steady-state frame time + the bgfx GPU/CPU
//   split, finds the 60fps and 30fps ceilings, and names the bottleneck. Then a
//   GRAND FINALE throws everything at once and reports the meltdown FPS.
//
// WHY: "how fast is our rendering, really?" can only be answered by a test that
//   actually submits the load to the GPU and reads the clock (doctrine: a perf
//   claim without a run is an affirmation, not a fact). bgfx is forced
//   single-threaded here (BGFX_CONFIG_MULTITHREADED=0), so the CPU submit time
//   tells us whether the wall is the GPU (fill-rate/overdraw) or OUR loop
//   (SceneCollector build + per-frame sort + draw-call submit). That GPU-vs-CPU
//   verdict is the actionable output.
//
// HONESTY / SAFETY (grounded in the renderer's real limits, read from the code):
//   - The sprite pass batches by (layer, textureId) and a single batch's
//     instance buffer is capped: transient alloc fails past bgfx's transient
//     budget and falls back to a 10000-instance dynamic buffer
//     (MAX_SPRITES_PER_BATCH). A SINGLE batch > 10000 would overflow that
//     fallback → crash. So we deliberately SPREAD every regime across
//     NUM_ASSETS textures × SPRITE_LAYERS layers: at 200k sprites that's
//     ~6.25k per (texture,layer) group — comfortably under 10k, so no batch
//     ever overflows and the run never crashes on its own load.
//   - We read bgfx::getStats()->numPrims as a sanity check: triangles drawn
//     should track 2× sprites submitted; a gross divergence is flagged, not
//     hidden.
//   - spdlog is silenced (level::off): the sprite pass logs per-batch at info
//     level, which would both flood the console AND skew the timing.
//   - Sprites are scattered INSIDE the viewport (the world pass culls
//     off-screen sprites — off-screen load would measure nothing). Heavy
//     overlap at high counts = real overdraw, which is the point.
//
// RUN: from build/ (asset paths are ../assets/...):  ./tests/benchmark_render_savage
//      ESC bails early. Not a ctest — it's a wall-clock measurement (windowed,
//      GPU, machine/driver dependent), like benchmark_pool_vs_threaded.
// ============================================================================

#define SDL_MAIN_HANDLED   // we provide our own main() (no SDL2main / WinMain hijack)
#include <SDL.h>
#include <SDL_syswm.h>
#include <bgfx/bgfx.h>

#include "BgfxRendererModule.h"
#include "Assets/AssetManager.h"
#include "Frame/FramePacket.h"   // SpriteInstance (POD) for the bulk path

#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace grove;

namespace {

// ---- Tunables -------------------------------------------------------------
constexpr int      VIEW_W = 1280, VIEW_H = 720;
constexpr int      NUM_ASSETS    = 4;     // distinct ship textures sprites cycle through
constexpr int      SPRITE_LAYERS = 8;     // × NUM_ASSETS = 32 batches max — each well under 10k/batch
constexpr double   MELT_MS       = 100.0; // stop ramping once a frame costs this much (~10 FPS)
constexpr int      WARM_FRAMES    = 8;    // let bgfx/GPU settle + assets go resident before timing
constexpr int      MEAS_FRAMES    = 24;   // averaged window per ramp step
constexpr int      MAX_N          = 200000;

// The ramp ladder (counts). The ramp stops early when a step exceeds MELT_MS.
const std::vector<int> LADDER = {1000, 2500, 5000, 10000, 20000, 35000,
                                 50000, 75000, 100000, 150000, 200000};

// One measured ramp step.
struct Row {
    int      n;        // primitives submitted
    double   wallMs;   // true per-frame wall time (emit + collect + submit + frame)
    double   gpuMs;    // bgfx GPU timer (fill-rate / overdraw)
    double   cpuMs;    // bgfx CPU submit timer
    uint32_t draws;    // draw calls (batches)
    uint32_t prims;    // triangles submitted (sanity: ~2× sprites)
    bool     clamped;  // numPrims diverged from expected → renderer dropped work
};

// Pre-scattered, deterministic positions/angles reused by every regime, so we
// measure the RENDER cost, not RNG. Sized to MAX_N once.
struct Scatter {
    std::vector<float>    x, y, a;
    std::vector<uint32_t> color;
    Scatter() {
        std::mt19937 rng(1234567u);  // fixed seed → frames comparable across runs
        std::uniform_real_distribution<float> fx(0.0f, float(VIEW_W));
        std::uniform_real_distribution<float> fy(0.0f, float(VIEW_H));
        std::uniform_real_distribution<float> fa(0.0f, 6.2831853f);
        x.resize(MAX_N); y.resize(MAX_N); a.resize(MAX_N); color.resize(MAX_N);
        const uint32_t palette[6] = {0xFFFFFFFF, 0xFF6688FF, 0x66FF88FF,
                                     0x6688FFFF, 0xFFCC44FF, 0x44CCFFFF};
        for (int i = 0; i < MAX_N; ++i) {
            x[i] = fx(rng); y[i] = fy(rng); a[i] = fa(rng);
            color[i] = palette[i % 6];
        }
    }
};

class SavageBench {
public:
    bool init(SDL_Window* window) {
        SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version);
        SDL_GetWindowWMInfo(window, &wmi);

        m_rendererIO = IntraIOManager::getInstance().createInstance("renderer");
        m_gameIO     = IntraIOManager::getInstance().createInstance("game");

        m_renderer = std::make_unique<BgfxRendererModule>();
        JsonDataNode cfg("config");
        cfg.setDouble("nativeWindowHandle",
                      double(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        cfg.setInt("windowWidth",  VIEW_W);
        cfg.setInt("windowHeight", VIEW_H);
        cfg.setString("backend", "d3d11");
        cfg.setBool("vsync", false);            // CRITICAL: uncapped → real throughput, not 60Hz
        cfg.setInt("assetVramBudgetMB", 512);   // keep all ship textures resident (no thrash)
        m_renderer->setConfiguration(cfg, m_rendererIO.get(), nullptr);

        // Register the 4 real ship PNGs as streamed assets (atlas/streaming path Alexi picked).
        // Direct AssetManager API (same as test_async_stream_demo) — no IIO timing to babysit.
        assets::AssetManager* am = m_renderer->getAssetManager();
        if (!am) { std::printf("FATAL: no AssetManager\n"); return false; }
        const char* files[NUM_ASSETS] = {
            "../assets/textures/ship/cockpit.png", "../assets/textures/ship/reactor.png",
            "../assets/textures/ship/engine.png",  "../assets/textures/ship/gun.png"};
        for (int i = 0; i < NUM_ASSETS; ++i) {
            char id[16]; std::snprintf(id, sizeof id, "ship/%d", i);
            m_assetIds.push_back(id);
            am->registerAsset(id, files[i], /*priority*/ 10);
        }
        buildPodPool();
        return true;
    }

    // ---- per-frame plumbing -----------------------------------------------
    void sendCameraAndClear() {
        auto clr = std::make_unique<JsonDataNode>("clear");
        clr->setInt("color", 0x101018FF);
        m_gameIO->publish("render:clear", std::move(clr));

        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x", 0); cam->setDouble("y", 0); cam->setDouble("zoom", 1.0);
        cam->setInt("viewportX", 0); cam->setInt("viewportY", 0);
        cam->setInt("viewportW", VIEW_W); cam->setInt("viewportH", VIEW_H);
        m_gameIO->publish("render:camera", std::move(cam));
    }

    // Emit n textured sprites, spread across NUM_ASSETS × SPRITE_LAYERS so no
    // single (texture,layer) batch exceeds ~n/32 — stays under the 10k/batch wall.
    void emitSprites(int n) {
        for (int i = 0; i < n; ++i) {
            auto s = std::make_unique<JsonDataNode>("s");
            s->setDouble("x", m_sc.x[i]);  s->setDouble("y", m_sc.y[i]);
            s->setDouble("scaleX", 22.0);  s->setDouble("scaleY", 22.0);
            s->setDouble("rotation", m_sc.a[i]);
            s->setInt("color", int(m_sc.color[i]));
            s->setInt("layer", i % SPRITE_LAYERS);
            s->setString("asset", m_assetIds[i % NUM_ASSETS]);
            m_gameIO->publish("render:sprite", std::move(s));
        }
    }

    // Emit n additive particles (overdraw stress — the fill-rate killer).
    void emitParticles(int n) {
        for (int i = 0; i < n; ++i) {
            auto p = std::make_unique<JsonDataNode>("p");
            p->setDouble("x", m_sc.x[i]); p->setDouble("y", m_sc.y[i]);
            p->setDouble("vx", 0.0); p->setDouble("vy", 0.0);
            p->setDouble("size", 14.0);
            p->setDouble("life", 0.6);             // mid-life → visible, additive
            p->setInt("color", int(m_sc.color[i]));
            p->setInt("textureId", 0);
            m_gameIO->publish("render:particle", std::move(p));
        }
    }

    // Build the POD pool ONCE: MAX_N GPU-ready SpriteInstance (the bulk path hands these
    // straight to the renderer — no JSON). textureId is spread over 28 fake ids so the
    // sprite pass splits into ≤28 batches (each well under the 10k/batch fallback limit →
    // crash-safe even when delivery is uncapped). All bind the default texture; we measure
    // DELIVERY + draw cost, not texturing.
    void buildPodPool() {
        m_pool.resize(MAX_N);
        for (int i = 0; i < MAX_N; ++i) {
            SpriteInstance& s = m_pool[i];
            s.x = m_sc.x[i]; s.y = m_sc.y[i];
            s.scaleX = 22.0f; s.scaleY = 22.0f;
            s.rotation = m_sc.a[i];
            s.u0 = 0.0f; s.v0 = 0.0f; s.u1 = 1.0f; s.v1 = 1.0f;
            s.textureId = float(1 + (i % 28));   // 28 batches → each <8k at 200k → no overflow
            s.layer = float(i % SPRITE_LAYERS);
            s.padding0 = 0.0f;
            s.reserved[0] = s.reserved[1] = s.reserved[2] = s.reserved[3] = 0.0f;
            const uint32_t c = m_sc.color[i];
            s.r = ((c >> 24) & 0xFF) / 255.0f; s.g = ((c >> 16) & 0xFF) / 255.0f;
            s.b = ((c >> 8) & 0xFF) / 255.0f;  s.a = (c & 0xFF) / 255.0f;
        }
    }

    // THE FIX UNDER TEST: feed n GPU-ready instances straight to the renderer, bypassing IIO
    // and JSON entirely (the IIO's publish() hard-requires JsonDataNode and deep-copies it to
    // JSON, so a POD payload literally can't ride the bus — direct submission is the path).
    // Cost = one vector insert of n × 80 B, ~ns/sprite vs the ~10 µs/sprite JSON envelope.
    void emitSpriteBatchPOD(int n) {
        m_renderer->submitSpriteBatch(m_pool.data(), size_t(n));
    }

    // Emit n text strings (each the word below → n×wordLen glyph quads).
    void emitText(int n) {
        static const char* WORD = "SAVAGE";
        for (int i = 0; i < n; ++i) {
            auto t = std::make_unique<JsonDataNode>("t");
            t->setDouble("x", m_sc.x[i]); t->setDouble("y", m_sc.y[i]);
            t->setString("text", WORD);
            t->setInt("fontSize", 14);
            t->setInt("color", int(m_sc.color[i]));
            t->setInt("layer", 100 + (i % 4));
            m_gameIO->publish("render:text", std::move(t));
        }
    }

    // Add `chunks` retained tilemap chunks (256×256 = 65,536 tiles each), laid out
    // in a grid. Retained = uploaded once; we then measure the steady-state cost.
    void addTilemapChunks(int chunks) {
        constexpr int W = 256, H = 256;
        std::string tileData; tileData.reserve(size_t(W) * H * 2);
        for (int k = 0; k < W * H; ++k) { if (k) tileData += ','; tileData += ((k & 1) ? '1' : '3'); }
        const int side = int(std::ceil(std::sqrt(double(chunks))));
        for (int c = 0; c < chunks; ++c) {
            auto tm = std::make_unique<JsonDataNode>("tm");
            tm->setInt("id", 1000 + c);
            tm->setDouble("x", double((c % side) * W));
            tm->setDouble("y", double((c / side) * H));
            tm->setInt("width", W); tm->setInt("height", H);
            tm->setInt("tileW", 4); tm->setInt("tileH", 4);
            tm->setInt("textureId", 0);
            tm->setString("tileData", tileData);
            m_gameIO->publish("render:tilemap:add", std::move(tm));
        }
        m_tilemapChunks += chunks;
    }

    // Run the renderer for one frame; return the wall time in ms.
    double stepFrame(int frameIdx, double dt) {
        Uint64 t0 = SDL_GetPerformanceCounter();
        JsonDataNode in("input");
        in.setDouble("deltaTime", dt);
        in.setInt("frameCount", frameIdx);
        m_renderer->process(in);
        Uint64 t1 = SDL_GetPerformanceCounter();
        return double(t1 - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
    }

    // Read bgfx's GPU/CPU timers + counters for the frame just submitted.
    void readStats(double& gpuMs, double& cpuMs, uint32_t& draws, uint32_t& prims) {
        const bgfx::Stats* s = bgfx::getStats();
        gpuMs = s && s->gpuTimerFreq ? double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0 / double(s->gpuTimerFreq) : 0.0;
        cpuMs = s && s->cpuTimerFreq ? double(s->cpuTimeEnd - s->cpuTimeBegin) * 1000.0 / double(s->cpuTimerFreq) : 0.0;
        draws = s ? s->numDraw : 0;
        prims = s ? s->numPrims[bgfx::Topology::TriList] : 0;
    }

    // Measure ONE count: warm up, then average over MEAS_FRAMES. emit(n) is the regime.
    template <typename Emit>
    Row measure(int n, int expectTrisPerItem, Emit&& emit, bool& quit) {
        for (int f = 0; f < WARM_FRAMES && !quit; ++f) { pump(quit); sendCameraAndClear(); emit(n); stepFrame(f, 1.0 / 60.0); }
        double wall = 0, gpu = 0, cpu = 0; uint32_t draws = 0, prims = 0;
        for (int f = 0; f < MEAS_FRAMES && !quit; ++f) {
            pump(quit);
            sendCameraAndClear();
            emit(n);
            wall += stepFrame(WARM_FRAMES + f, 1.0 / 60.0);
            double g, c; uint32_t d, p; readStats(g, c, d, p);
            gpu += g; cpu += c; draws = d; prims = p;  // counters are per-frame, last is representative
        }
        const double inv = 1.0 / MEAS_FRAMES;
        // Sanity: a sprite/particle/glyph is 2 tris. If far fewer drew, work was dropped.
        const uint32_t expectTris = uint32_t(int64_t(n) * expectTrisPerItem);
        const bool clamped = expectTris > 0 && prims + prims / 10 < expectTris;  // >10% short
        return Row{n, wall * inv, gpu * inv, cpu * inv, draws, prims, clamped};
    }

    void pump(bool& quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) quit = true;
        }
    }

    int  tilemapChunks() const { return m_tilemapChunks; }
    void shutdown() { m_renderer->shutdown(); }

private:
    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::shared_ptr<IntraIO>            m_rendererIO, m_gameIO;
    std::vector<std::string>            m_assetIds;
    Scatter                             m_sc;
    std::vector<SpriteInstance>         m_pool;   // pre-built GPU-ready instances (POD path)
    int                                 m_tilemapChunks = 0;
};

// ---- report helpers -------------------------------------------------------

// Linear-interpolate the count at which wallMs crosses `targetMs` (the FPS ceiling).
int ceilingAt(const std::vector<Row>& rows, double targetMs) {
    for (size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].wallMs >= targetMs && rows[i - 1].wallMs < targetMs) {
            const double t = (targetMs - rows[i - 1].wallMs) / (rows[i].wallMs - rows[i - 1].wallMs);
            return int(rows[i - 1].n + t * (rows[i].n - rows[i - 1].n));
        }
    }
    return rows.empty() ? 0 : (rows.back().wallMs < targetMs ? -rows.back().n : 0); // -N = "still above target at N"
}

void printRegime(const char* name, const char* unit, const std::vector<Row>& rows) {
    std::printf("\n  ===== %s =====\n", name);
    std::printf("  %-9s %-9s %-7s %-9s %-9s %-7s %-9s %s\n",
                unit, "wall ms", "fps", "gpu ms", "cpu ms", "draws", "tris", "bottleneck");
    std::printf("  ----------------------------------------------------------------------------------\n");
    for (const Row& r : rows) {
        const double fps = r.wallMs > 0 ? 1000.0 / r.wallMs : 0;
        const char* bn = (r.gpuMs > r.cpuMs * 1.15) ? "GPU(fill)"
                       : (r.cpuMs > r.gpuMs * 1.15) ? "CPU(submit)" : "~tie";
        std::printf("  %-9d %-9.2f %-7.0f %-9.2f %-9.2f %-7u %-9u %s%s\n",
                    r.n, r.wallMs, fps, r.gpuMs, r.cpuMs, r.draws, r.prims, bn,
                    r.clamped ? "  <<DROPPED>>" : "");
    }
    const int c60 = ceilingAt(rows, 1000.0 / 60.0);
    const int c30 = ceilingAt(rows, 1000.0 / 30.0);
    auto fmt = [](int c) { return c < 0 ? std::string(">") + std::to_string(-c) : std::to_string(c); };
    std::printf("  ceiling @60fps: %-10s  @30fps: %-10s\n", fmt(c60).c_str(), fmt(c30).c_str());
}

} // namespace

int main(int, char**) {
    spdlog::set_level(spdlog::level::off);  // silence the per-batch render logs (flood + skew)
    std::setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered: last printed line survives a crash/abort

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { std::printf("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    SDL_Window* window = SDL_CreateWindow("GROS SAUVAGE — render benchmark",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, VIEW_W, VIEW_H, SDL_WINDOW_SHOWN);
    if (!window) { std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    auto bench = std::make_unique<SavageBench>();
    if (!bench->init(window)) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    std::printf("================================================================================\n");
    std::printf("  GROS PUTAIN DE SAUVAGE — BgfxRenderer throughput (D3D11, vsync OFF)\n");
    std::printf("  window %dx%d | wall ms = true per-frame cost | GPU vs CPU = where the wall is\n", VIEW_W, VIEW_H);
    std::printf("================================================================================\n");

    bool quit = false;

    // --- Regime 1: textured sprites (atlas/streaming) ---
    std::vector<Row> sprites;
    for (int n : LADDER) {
        if (quit) break;
        Row r = bench->measure(n, 2, [&](int c){ bench->emitSprites(c); }, quit);
        sprites.push_back(r);
        std::printf("  sprites   %-7d -> %.2f ms (%.0f fps)\n", r.n, r.wallMs, r.wallMs > 0 ? 1000.0 / r.wallMs : 0);
        if (r.wallMs > MELT_MS) { std::printf("  >>> MELTED at %d sprites <<<\n", n); break; }
    }

    // --- Regime 1b: SAME sprites via the POD bulk path (the fix). One message, zero JSON. ---
    std::vector<Row> spritesPod;
    for (int n : LADDER) {
        if (quit) break;
        Row r = bench->measure(n, 2, [&](int c){ bench->emitSpriteBatchPOD(c); }, quit);
        spritesPod.push_back(r);
        std::printf("  POD-batch %-7d -> %.2f ms (%.0f fps)\n", r.n, r.wallMs, r.wallMs > 0 ? 1000.0 / r.wallMs : 0);
        if (r.wallMs > MELT_MS) { std::printf("  >>> MELTED at %d POD sprites <<<\n", n); break; }
    }

    // --- Regime 2: particles (additive overdraw) ---
    std::vector<Row> particles;
    for (int n : LADDER) {
        if (quit) break;
        Row r = bench->measure(n, 2, [&](int c){ bench->emitParticles(c); }, quit);
        particles.push_back(r);
        std::printf("  particles %-7d -> %.2f ms (%.0f fps)\n", r.n, r.wallMs, r.wallMs > 0 ? 1000.0 / r.wallMs : 0);
        if (r.wallMs > MELT_MS) { std::printf("  >>> MELTED at %d particles <<<\n", n); break; }
    }

    // --- Regime 3: text glyphs (6 glyphs/string) ---
    std::vector<Row> text;
    for (int strings : {1000, 2500, 5000, 10000, 20000, 35000}) {
        if (quit) break;
        Row r = bench->measure(strings, 12 /*~6 glyphs × 2 tris*/, [&](int c){ bench->emitText(c); }, quit);
        text.push_back(r);
        std::printf("  text      %-7d strings (%d glyphs) -> %.2f ms (%.0f fps)\n",
                    r.n, r.n * 6, r.wallMs, r.wallMs > 0 ? 1000.0 / r.wallMs : 0);
        if (r.wallMs > MELT_MS) { std::printf("  >>> MELTED at %d strings <<<\n", strings); break; }
    }

    // --- Regime 4: monster tilemap (retained chunks, steady-state) ---
    std::vector<Row> tilemap;
    for (int chunks : {1, 4, 16, 36, 64}) {
        if (quit) break;
        bench->addTilemapChunks(chunks - bench->tilemapChunks());  // grow to `chunks` total
        Row r = bench->measure(0, 0, [&](int){ /* tilemap is retained, nothing to re-emit */ }, quit);
        r.n = bench->tilemapChunks();
        tilemap.push_back(r);
        std::printf("  tilemap   %-3d chunks (%d tiles) -> %.2f ms (%.0f fps)\n",
                    r.n, r.n * 65536, r.wallMs, r.wallMs > 0 ? 1000.0 / r.wallMs : 0);
        if (r.wallMs > MELT_MS) break;
    }

    // --- GRAND FINALE: everything at once. NIQUE. ---
    Row finale{};
    if (!quit) {
        const int S = 80000, P = 80000, T = 8000;  // sprites + particles + text strings (+ the resident tilemap)
        finale = bench->measure(S, 2, [&](int){
            bench->emitSprites(S);
            bench->emitParticles(P);
            bench->emitText(T);
        }, quit);
        finale.n = S + P + T * 6;
    }

    // ---- report ----
    printRegime("TEXTURED SPRITES — JSON per-sprite (the OLD path)", "sprites", sprites);
    printRegime("SPRITES — POD bulk batch (the FIX)",                "sprites", spritesPod);

    // Head-to-head the HONEST way: how far does each path hold 60fps? Comparing wall-time at a
    // fixed huge count is apples-to-oranges (the JSON path silently DROPS most sprites past its
    // cap, so its "fast" frame is fast because it isn't drawing them). The fair measure is the
    // sprite count each path sustains at 60fps — the ceiling.
    if (!sprites.empty() && !spritesPod.empty()) {
        const int jc = ceilingAt(sprites,    1000.0 / 60.0);
        const int pc = ceilingAt(spritesPod, 1000.0 / 60.0);
        const double ratio = (jc > 0 && pc != 0) ? double(pc < 0 ? -pc : pc) / jc : 0;
        std::printf("\n  >>> THE WIN: 60fps sprite ceiling  JSON %d  ->  POD %d  =  %.0f× more sprites <<<\n",
                    jc, (pc < 0 ? -pc : pc), ratio);
        std::printf("      (the JSON path can't even DELIVER more — IIO deep-copies a 7-field json per\n");
        std::printf("       sprite at ~10us each; the POD path hands the GPU packed instances at ~ns each)\n");
    }

    printRegime("PARTICLES (additive overdraw)",      "particles", particles);
    printRegime("TEXT (glyph quads)",                 "strings", text);
    printRegime("MONSTER TILEMAP (retained chunks)",  "chunks", tilemap);

    std::printf("\n  ===== GRAND FINALE — everything at once =====\n");
    if (finale.wallMs > 0) {
        std::printf("  80k sprites + 80k particles + 8k text + %d tilemap chunks (%d tiles)\n",
                    bench->tilemapChunks(), bench->tilemapChunks() * 65536);
        std::printf("  >>> %.1f FPS  (%.2f ms/frame, GPU %.2f / CPU %.2f, %u draws, %u tris) <<<\n",
                    finale.wallMs > 0 ? 1000.0 / finale.wallMs : 0, finale.wallMs,
                    finale.gpuMs, finale.cpuMs, finale.draws, finale.prims);
        std::printf("  %s\n", (finale.gpuMs > finale.cpuMs) ? "verdict: GPU-bound (fill-rate / overdraw)"
                                                            : "verdict: CPU-bound (our submit loop / sort / draw calls)");
    } else {
        std::printf("  (skipped — ESC pressed)\n");
    }
    std::printf("================================================================================\n");

    bench->shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
