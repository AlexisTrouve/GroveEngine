/**
 * GPU E2E: the async load WIRING inside BgfxRendererModule (phase 3). This is the integration the component
 * test doesn't cover — it drives the real module: config flag `assetAsyncLoad` ON, ThreadedDecoder built in
 * init(), and pumpAsync() called inside process(). A render:sprite that names its texture by asset id must
 * stream in ACROSS FRAMES through the module's own frame loop, NOT by poking the AssetManager directly.
 *
 * Own executable on purpose: bgfx is a process-wide singleton, so each device-initialising GPU test runs in
 * its own process (same convention as the other asset GPU tests). [gpu] — skips cleanly without a GPU.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Assets/AssetManager.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <memory>
#include <thread>
#include <chrono>

using namespace grove;

TEST_CASE("render:sprite by assetId streams in via the module's async pump (GPU)", "[gpu][assets][async]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("asset-async-mod", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       128, 128, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("aam_r");
    auto gIO = mgr.createInstance("aam_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", 128); c.setInt("windowHeight", 128); c.setBool("vsync", false);
        c.setBool("assetAsyncLoad", true);            // <-- the wiring under test
        c.setInt("assetDecodeThreads", 1);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown();
        mgr.removeInstance("aam_r"); mgr.removeInstance("aam_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    assets::AssetManager* am = renderer->getAssetManager();
    REQUIRE(am != nullptr);
    am->registerAsset("cockpit", "../../assets/textures/ship/cockpit.png", /*priority*/ 5);

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportW",128); cam->setInt("viewportH",128);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    // Publish a sprite that names its texture by asset id (no numeric textureId).
    { auto s = std::make_unique<JsonDataNode>("d");
      s->setDouble("x", 20); s->setDouble("y", 20); s->setDouble("scaleX", 64); s->setDouble("scaleY", 64);
      s->setString("asset", "cockpit"); s->setInt("layer", 1000);
      gIO->publish("render:sprite", std::move(s)); }

    // Pump frames until it streams in. The DISCRIMINATING signal is isLoading(): the loading flag is set ONLY on
    // the async branch of resolve() (which only runs when a decoder is wired) — in synchronous mode the asset
    // jumps straight to resident and isLoading is NEVER true. So observing it proves the config flag was honored,
    // the decoder was built, and resolve took the off-thread path. `resident` then proves process()'s pumpAsync
    // uploaded it. Bounded (~1s) so broken wiring fails instead of hanging the suite.
    bool sawLoading = false, resident = false;
    for (int i = 0; i < 200 && !resident; ++i) {
        frame();
        if (am->isLoading("cockpit")) sawLoading = true;   // captured on the frame resolve() kicks the decode
        resident = am->isResident("cockpit");
        if (!resident) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(sawLoading);   // the async path was actually taken through the module (flag honored, decoder wired)
    REQUIRE(resident);     // streamed in through the module's own pumpAsync in process()

    renderer->shutdown();
    mgr.removeInstance("aam_r"); mgr.removeInstance("aam_g");
    SDL_DestroyWindow(win); SDL_Quit();
}
