/**
 * test_fx_demo — a BY-EYE windowed demo of the FX layer (grove::fx + FxModule), rendered FOR REAL through
 * BgfxRenderer: particle-burst explosions (F3 Emitter) and rising floating damage numbers (F2 Text).
 *
 * WHAT : hosts FxModule + BgfxRendererModule on a DebugEngine, registers three archetypes as data
 *        (a `spark` particle prefab, an `explosion` emitter archetype, a `damage_number` text archetype),
 *        then spawns them via `fx:*` topics and lets the engine tick + draw. No bespoke effect code — the
 *        whole show is the engine's fixed behavior library (velocity / fade / lifetime) + the Emitter.
 * WHY  : the FX logic is already E2E-locked headless (FxWorldUnit + IT_059d/e/f), but "je veux voir" — the
 *        point of a cosmetic VFX layer is that it LOOKS right. This is the visual half: motion you can watch.
 * HOW  : particles are solid-colour sprites tinted by their `color` (the renderer multiplies the sprite
 *        colour, alpha included — so `fade` visibly dims them). A 20x20 white runtime texture ("fx_dot") is
 *        the particle bitmap; each `spark` prefab tints it. Damage numbers ride `render:text` (default font).
 *
 * Usage:
 *   test_fx_demo                  # interactive window — LMB = explosion at cursor, Space = damage number,
 *                                 # and a burst auto-spawns every ~0.8 s. Esc / close the window to quit.
 *   test_fx_demo --shot out.png   # headless: spawn a few bursts + numbers, tick to mid-flight, capture one
 *                                 # PNG (the offscreen-framebuffer proof the effects actually render).
 *
 * Run from the project root (default asset paths). Windows/SDL only (like the other visual demos).
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "FxModule.h"

#include <grove/DebugEngine.h>
#include <grove/IModuleSystem.h>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include "PngCapture.h"      // writeRgbaAsPng (shared svpng-backed writer)

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace grove;
using nlohmann::json;

int main(int argc, char** argv) {
    // --- args: interactive by default; `--shot <png>` captures one headless frame. ---
    bool shot = false;
    std::string outPath = "fx_demo.png";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shot") { shot = true; if (i + 1 < argc) outPath = argv[++i]; }
    }
    const int W = 1280, H = 720;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    // Hidden window for the headless shot; a real visible window for the interactive demo.
    const Uint32 winFlags = shot ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    SDL_Window* win = SDL_CreateWindow("GroveEngine — FX demo (explosions + damage numbers)",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, winFlags);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    // The "game side" IIO instance publishes fx:* + the per-frame camera/clear. The engine creates the
    // modules' own routed instances internally (registerStaticModule).
    auto& mgr = IntraIOManager::getInstance();
    auto gIO = mgr.createInstance("fx_demo_game");

    DebugEngine engine;
    engine.initialize();

    // Register FX BEFORE the renderer so each frame FxModule.process() publishes its render:* commands
    // before the renderer consumes them (same-frame draw) — the same ordering rule as the UI-on-engine demo.
    engine.registerStaticModule("fx", std::make_unique<FxModule>(), ModuleSystemType::SEQUENTIAL,
                                std::make_unique<JsonDataNode>("config"));
    auto rendererOwned = std::make_unique<BgfxRendererModule>();
    BgfxRendererModule* renderer = rendererOwned.get();
    {
        auto rCfg = std::make_unique<JsonDataNode>("config");
        rCfg->setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        rCfg->setInt("windowWidth", W); rCfg->setInt("windowHeight", H); rCfg->setBool("vsync", !shot);
        engine.registerStaticModule("renderer", std::move(rendererOwned), ModuleSystemType::SEQUENTIAL, std::move(rCfg));
    }

    // --- helpers ------------------------------------------------------------------------------------
    auto pushJson = [&](const std::string& topic, json j) {
        gIO->publish(topic, std::make_unique<JsonDataNode>("d", std::move(j)));
    };
    // One frame: publish the (static) camera + a dark clear, then engine.step() runs fx then renderer.
    auto frame = [&](float dt) {
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x", 0); cam->setDouble("y", 0); cam->setDouble("zoom", 1.0);
          cam->setInt("viewportX", 0); cam->setInt("viewportY", 0); cam->setInt("viewportW", W); cam->setInt("viewportH", H);
          gIO->publish("render:camera", std::move(cam)); }
        { auto clr = std::make_unique<JsonDataNode>("clear");
          clr->setInt("color", static_cast<int>(0x0A0E18FFu)); gIO->publish("render:clear", std::move(clr)); }
        engine.step(dt);
    };

    // A white runtime texture is the particle bitmap; each spark prefab TINTS it via the sprite colour. Its
    // pixel SIZE on screen is the sprite scaleX/scaleY (a unit quad), NOT the texture dimensions — the burst
    // sets the scale below. 8x8 is plenty for a solid dot.
    { auto d = std::make_unique<JsonDataNode>("tex");
      d->setString("id", "fx_dot"); d->setInt("width", 8); d->setInt("height", 8);
      d->setInt("color", static_cast<int>(0xFFFFFFFFu)); gIO->publish("render:texture:create", std::move(d)); }

    // --- archetypes (registered once, spawned many times) -------------------------------------------
    // A particle = a tinted dot that fades out + dies over its life. The emitter adds the launch velocity.
    auto sparkPrefab = [&](const char* name, uint32_t color, double life) {
        pushJson("fx:prefab", json{{"name", name},
            {"sprite", {{"asset", "fx_dot"}, {"color", color}, {"layer", 900}}},
            {"behaviors", json::array({ json{{"type","fade"},{"seconds",life}},
                                        json{{"type","lifetime"},{"seconds",life}} })}});
    };
    sparkPrefab("spark_fire", 0xFFB020FF, 0.75);   // orange
    sparkPrefab("spark_ice",  0x40D8FFFF, 0.85);   // cyan
    // Two explosion archetypes carry an emitter (a one-shot burst of `count` sparks in a full circle).
    pushJson("fx:prefab", json{{"name","boom_fire"},
        {"emitter", {{"prefab","spark_fire"},{"count",34},{"speedMin",70},{"speedMax",260},{"spreadDeg",360}}}});
    pushJson("fx:prefab", json{{"name","boom_ice"},
        {"emitter", {{"prefab","spark_ice"},{"count",28},{"speedMin",60},{"speedMax",210},{"spreadDeg",360}}}});
    // A floating damage number: a text that rises, fades, and self-expires (the string is per-instance).
    pushJson("fx:prefab", json{{"name","damage_number"},
        {"text", {{"text",""},{"color",0xFFFFFFFF},{"layer",1000},{"fontSize",30}}},
        {"behaviors", json::array({ json{{"type","velocity"},{"vx",0},{"vy",-70},{"drag",0}},
                                    json{{"type","fade"},{"seconds",1.1}},
                                    json{{"type","lifetime"},{"seconds",1.1}} })}});
    // A trail particle (for the comet): a dim dot that fades + dies fast, so the trail tapers behind the head.
    pushJson("fx:prefab", json{{"name","trail_dust"},
        {"sprite", {{"asset","fx_dot"},{"color",0x50B0FF80},{"layer",850}}},
        {"behaviors", json::array({ json{{"type","fade"},{"seconds",0.6}},
                                    json{{"type","lifetime"},{"seconds",0.6}} })}});

    int uid = 0;
    auto explode = [&](double cx, double cy, bool ice) {
        // Particles inherit the emitter's Transform, incl. scale — and sprite scaleX/scaleY IS the pixel size
        // (unit quad). ~20 px dots read well on a 1280x720 view.
        pushJson("fx:spawn", json{{"id","boom_" + std::to_string(++uid)},
            {"archetype", ice ? "boom_ice" : "boom_fire"},
            {"transform", {{"cx",cx},{"cy",cy},{"scaleX",20.0},{"scaleY",20.0}}}});
    };
    auto damage = [&](double cx, double cy, const std::string& text, uint32_t color) {
        pushJson("fx:spawn", json{{"id","dmg_" + std::to_string(++uid)}, {"archetype","damage_number"},
            {"transform", {{"cx",cx},{"cy",cy}}}, {"text", {{"text",text},{"color",color}}}});
    };
    // A comet: ONE entity that is a bright sprite head + a `move` behavior + a CONTINUOUS emitter (the trail) +
    // a lifetime. It flies, drops trail particles at its moving position, and self-cleans — the F4 showcase.
    auto comet = [&](double cx, double cy, double vx, double vy) {
        pushJson("fx:spawn", json{{"id","comet_" + std::to_string(++uid)},
            {"transform", {{"cx",cx},{"cy",cy},{"scaleX",13.0},{"scaleY",13.0}}},
            {"sprite", {{"asset","fx_dot"},{"color",0xFFFFFFFF},{"layer",950}}},              // bright white head
            {"emitter", {{"prefab","trail_dust"},{"ratePerSec",70.0},{"oneShot",false},
                         {"speedMin",0.0},{"speedMax",25.0},{"spreadDeg",360.0}}},            // gentle puff trail
            {"behaviors", json::array({ json{{"type","move"},{"vx",vx},{"vy",vy}},
                                        json{{"type","lifetime"},{"seconds",3.0}} })}});
    };

    for (int i = 0; i < 3; ++i) frame(1.0f / 60.0f);   // settle: prefab registration, first camera/clear

    // =============================== HEADLESS SHOT ===============================
    if (shot) {
        // Fire a spread of bursts + numbers, then tick to mid-flight so particles are spread + partly faded
        // and the numbers have risen — the most representative single frame.
        explode(360, 400, false); damage(360, 340, "-142", 0xFF5050FF);
        explode(760, 320, true);  damage(760, 260, "-88",  0x60E0FFFF);
        explode(980, 470, false); damage(980, 410, "CRIT!",0xFFE060FF);
        damage(560, 520, "-25", 0xFFFFFFFF);
        comet(140, 180, 320.0, 40.0);                   // a comet flying right — leaves a continuous trail
        for (int i = 0; i < 10; ++i) frame(0.028f);     // ~0.28 s — bursts mid-flight + the comet's trail formed

        // Redirect both views into an offscreen framebuffer, render, read back, write the PNG.
        rhi::IRHIDevice* dev = renderer->getDevice();
        if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
        rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
        dev->setViewFramebuffer(0, fb); dev->setViewFramebuffer(1, fb);
        frame(0.016f); frame(0.016f);                   // render into the fb (readback is a frame behind)

        std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
            std::fprintf(stderr, "readback failed\n"); return 3;
        }
        if (!mvdemo::writeRgbaAsPng(outPath, W, H, rgba)) { std::fprintf(stderr, "png write failed\n"); return 4; }
        std::fprintf(stdout, "wrote %s (%dx%d) — FX rendered through the engine\n", outPath.c_str(), W, H);

        engine.shutdown(); mgr.removeInstance("fx_demo_game");
        SDL_DestroyWindow(win); SDL_Quit();
        return 0;
    }

    // =============================== INTERACTIVE ================================
    std::fprintf(stdout, "FX demo — LMB: explosion  |  RMB: comet (trail)  |  Space: damage number  |  auto every ~0.8s  |  Esc: quit\n");
    bool running = true;
    float autoTimer = 0.0f;
    int tick = 0;
    const char* dmgStrings[] = {"-25", "-88", "-142", "CRIT!", "-310", "-64"};
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE)
                damage(W * 0.5, H * 0.55, dmgStrings[tick % 6], 0xFFE060FF);
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                explode(e.button.x, e.button.y, (tick & 1) != 0);
                damage(e.button.x, e.button.y - 40, dmgStrings[tick % 6], 0xFF6060FF);
                ++tick;
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                // Comet from the cursor toward the screen centre — a continuous trail follows the moving head.
                const double dx = W * 0.5 - e.button.x, dy = H * 0.5 - e.button.y;
                const double len = std::sqrt(dx * dx + dy * dy) + 1.0;
                comet(e.button.x, e.button.y, dx / len * 300.0, dy / len * 300.0);
            }
        }
        // Auto-spawn a burst on a moving path so there's always something on screen to watch.
        autoTimer += 1.0f / 60.0f;
        if (autoTimer >= 0.8f) {
            autoTimer = 0.0f;
            const double cx = W * 0.5 + std::sin(tick * 0.7) * 380.0;
            const double cy = H * 0.5 + std::cos(tick * 1.3) * 180.0;
            explode(cx, cy, (tick & 1) != 0);
            damage(cx, cy - 40, dmgStrings[tick % 6], (tick & 1) ? 0x60E0FFFF : 0xFFB030FF);
            ++tick;
        }
        frame(1.0f / 60.0f);
        SDL_Delay(16);
    }

    engine.shutdown(); mgr.removeInstance("fx_demo_game");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
