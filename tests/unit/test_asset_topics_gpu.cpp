/**
 * GPU test: the asset FEED — declarative manifest at boot + runtime asset:* topics. The renderer loads a
 * manifest (config "assetManifest"); the game then drives the registry by data:
 *   asset:register / asset:preload / asset:unload.
 * Verifies the registry + streaming respond. [gpu] — skips cleanly without a GPU.
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

TEST_CASE("asset manifest + asset:* topics feed the AssetManager (GPU)", "[gpu][assets]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("asset-topics", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       128, 128, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("at_r");
    auto gIO = mgr.createInstance("at_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", 128); c.setInt("windowHeight", 128); c.setBool("vsync", false);
        c.setString("assetManifest", "../../tests/fixtures/asset_manifest.json");
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("at_r"); mgr.removeInstance("at_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    assets::AssetManager* am = renderer->getAssetManager();
    REQUIRE(am != nullptr);

    // --- Manifest loaded at boot: metadata registered, nothing resident yet. ---
    REQUIRE(am->isRegistered("cockpit"));
    REQUIRE(am->isRegistered("reactor"));
    REQUIRE_FALSE(am->isResident("cockpit"));

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("zoom",1.0); cam->setInt("viewportW",128); cam->setInt("viewportH",128);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };
    auto pub = [&](const char* topic, std::unique_ptr<JsonDataNode> d){ gIO->publish(topic, std::move(d)); frame(); frame(); };

    // --- asset:preload {group} -> the whole "ship" group streams in. ---
    { auto d=std::make_unique<JsonDataNode>("d"); d->setString("group","ship"); pub("asset:preload", std::move(d)); }
    REQUIRE(am->isResident("cockpit"));
    REQUIRE(am->isResident("reactor"));
    REQUIRE(am->isResident("engine"));

    // --- asset:register {id,path} at runtime -> registered (not resident until referenced). ---
    { auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","gun"); d->setString("path","../../assets/textures/ship/gun.png");
      pub("asset:register", std::move(d)); }
    REQUIRE(am->isRegistered("gun"));
    REQUIRE_FALSE(am->isResident("gun"));

    // --- asset:unload {id} -> frees a resident asset. ---
    { auto d=std::make_unique<JsonDataNode>("d"); d->setString("id","cockpit"); pub("asset:unload", std::move(d)); }
    REQUIRE_FALSE(am->isResident("cockpit"));
    REQUIRE(am->isResident("reactor"));   // others untouched

    // --- ATLAS (phase 2): the manifest's atlas registered the sheet + sub-sprites; a render:sprite by a
    //     sub-sprite id streams the SHARED sheet in (one texture for many sprites). ---
    REQUIRE(am->isRegistered("ui/a"));
    REQUIRE(am->isRegistered("uiSheet"));
    REQUIRE_FALSE(am->isResident("uiSheet"));
    { auto s=std::make_unique<JsonDataNode>("d"); s->setDouble("x",10); s->setDouble("y",10);
      s->setDouble("scaleX",32); s->setDouble("scaleY",32); s->setString("asset","ui/a"); s->setInt("layer",1000);
      gIO->publish("render:sprite", std::move(s)); }
    frame(); frame();
    REQUIRE(am->isResident("uiSheet"));   // the atlas sub-sprite resolved + loaded its sheet

    renderer->shutdown();
    mgr.removeInstance("at_r"); mgr.removeInstance("at_g");
    SDL_DestroyWindow(win); SDL_Quit();
}
