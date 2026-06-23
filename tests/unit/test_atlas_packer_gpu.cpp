/**
 * GPU test: RUNTIME atlas packing (phase 2b). packAtlas() decodes N separate PNGs, shelf-packs them into one
 * sheet texture, and registers their sub-sprites with distinct UVs — N images, ONE shared resident texture.
 * Also drives it data-driven via the asset:pack topic. [gpu] — skips cleanly without a GPU.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Assets/AssetManager.h"
#include "Assets/AtlasPacker.h"
#include "Resources/ResourceCache.h"
#include "RHI/RHIDevice.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <vector>
#include <memory>

using namespace grove;
using nlohmann::json;

TEST_CASE("packAtlas builds one shared sheet from N PNGs with distinct UVs (GPU)", "[gpu][assets]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("atlas-pack", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       128, 128, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("ap_r");
    auto gIO = mgr.createInstance("ap_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", 128); c.setInt("windowHeight", 128); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("ap_r"); mgr.removeInstance("ap_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    rhi::IRHIDevice* dev = renderer->getDevice();
    ResourceCache* cache = renderer->getResourceCache();
    assets::AssetManager* am = renderer->getAssetManager();
    REQUIRE(dev); REQUIRE(cache); REQUIRE(am);

    // --- DIRECT packAtlas: 3 ship PNGs -> one sheet. ---
    std::vector<assets::PackEntry> entries = {
        {"icon/cockpit", "../../assets/textures/ship/cockpit.png"},
        {"icon/reactor", "../../assets/textures/ship/reactor.png"},
        {"icon/engine",  "../../assets/textures/ship/engine.png"},
    };
    REQUIRE(assets::packAtlas(*dev, *cache, *am, "iconSheet", entries));

    REQUIRE(am->isResident("iconSheet"));            // packed sheet is resident (pinned)
    REQUIRE(am->isRegistered("icon/cockpit"));
    REQUIRE_FALSE(am->isResident("icon/cockpit"));   // sub-sprite shares the sheet, not its own texture

    float u0,v0,u1,v1, bu0,bv0,bu1,bv1;
    const uint32_t tA = am->resolveSprite("icon/cockpit", u0,v0,u1,v1);
    const uint32_t tB = am->resolveSprite("icon/reactor", bu0,bv0,bu1,bv1);
    REQUIRE(tA != 0);
    REQUIRE(tB == tA);                               // SAME shared sheet texture (the packing win)
    REQUIRE(u1 < 0.9f);                               // a sub-region, not the whole sheet
    REQUIRE((u0 != bu0 || v0 != bv0));               // the two icons occupy DIFFERENT regions

    // --- DATA-DRIVEN via the asset:pack topic. ---
    {
        json sprites = json::array({
            { {"id","p/gun"},     {"path","../../assets/textures/ship/gun.png"} },
            { {"id","p/cockpit"}, {"path","../../assets/textures/ship/cockpit.png"} }
        });
        gIO->publish("asset:pack", std::make_unique<JsonDataNode>("d", json{ {"sheet","packed2"}, {"sprites", sprites} }));
    }
    { JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
      renderer->process(in); }   // deliver + run the topic
    REQUIRE(am->isResident("packed2"));
    REQUIRE(am->isRegistered("p/gun"));

    renderer->shutdown();
    mgr.removeInstance("ap_r"); mgr.removeInstance("ap_g");
    SDL_DestroyWindow(win); SDL_Quit();
}
