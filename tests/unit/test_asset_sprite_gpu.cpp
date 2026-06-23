/**
 * GPU test: a render:sprite that references a texture by ASSET ID (string) streams it in. We register
 * "cockpit" with the renderer's AssetManager, publish a render:sprite carrying {asset:"cockpit"} (no numeric
 * textureId), pump a frame, and assert the asset is now RESIDENT — i.e. the sprite path resolved the id and
 * loaded the texture on demand. [gpu] — skips cleanly without a GPU.
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

using namespace grove;

TEST_CASE("render:sprite by assetId streams the texture in via the AssetManager (GPU)", "[gpu][assets]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("asset-sprite", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       128, 128, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("as_r");
    auto gIO = mgr.createInstance("as_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", 128); c.setInt("windowHeight", 128); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {   // no GPU context (headless CI) -> skip
        renderer->shutdown();
        mgr.removeInstance("as_r"); mgr.removeInstance("as_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    assets::AssetManager* am = renderer->getAssetManager();
    REQUIRE(am != nullptr);
    am->registerAsset("cockpit", "../../assets/textures/ship/cockpit.png", /*priority*/ 5);
    REQUIRE_FALSE(am->isResident("cockpit"));   // registered != loaded — only on demand

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportW",128); cam->setInt("viewportH",128);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    // A sprite that names its texture by asset id (no numeric textureId).
    { auto s = std::make_unique<JsonDataNode>("d");
      s->setDouble("x", 20); s->setDouble("y", 20); s->setDouble("scaleX", 64); s->setDouble("scaleY", 64);
      s->setString("asset", "cockpit"); s->setInt("layer", 1000);
      gIO->publish("render:sprite", std::move(s)); }
    frame(); frame();   // a couple of frames so the cross-instance message is delivered + parsed

    REQUIRE(am->isResident("cockpit"));   // the sprite's asset id resolved -> on-demand load

    renderer->shutdown();
    mgr.removeInstance("as_r"); mgr.removeInstance("as_g");
    SDL_DestroyWindow(win); SDL_Quit();
}
