/**
 * GPU test: the bgfx side of the asset system. Drives AssetManager + BgfxTextureProvider against a REAL
 * device, loading the actual ship PNGs, and proves: on-demand load registers a renderable texture id,
 * the VRAM budget evicts, and PRIORITY keeps the important one resident. [gpu] — skips cleanly with no GPU.
 *
 * Each ship PNG is 128x128 RGBA8 = 65536 bytes. Budget 160000 fits 2, not 3 -> loading a 3rd evicts one.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

#include "RHI/RHIDevice.h"
#include "Resources/ResourceCache.h"
#include "Assets/BgfxTextureProvider.h"
#include "Assets/AssetManager.h"

#include <string>

using namespace grove;

TEST_CASE("AssetManager + bgfx provider load/evict real textures with priority (GPU)", "[gpu][assets]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("asset-gpu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       64, 64, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));
#ifdef _WIN32
    void* nwh = wmi.info.win.window; void* ndt = nullptr;
#else
    void* nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
    void* ndt = wmi.info.x11.display;
#endif
    auto device = rhi::IRHIDevice::create();
    if (!device->init(nwh, ndt, 64, 64)) { SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return; }

    ResourceCache cache;
    assets::BgfxTextureProvider provider(device.get(), &cache);
    assets::AssetManager am(&provider, /*vram budget*/ 160000);   // fits 2 of the 65536-byte textures

    const std::string base = "../../assets/textures/ship/";
    am.registerAsset("cockpit", base + "cockpit.png", /*priority*/ 5);   // important -> must survive eviction
    am.registerAsset("reactor", base + "reactor.png", 0);
    am.registerAsset("engine",  base + "engine.png",  0);

    // On-demand load -> a renderable texture id (the sprite pass resolves it via ResourceCache).
    const uint32_t tex = am.resolve("cockpit");
    REQUIRE(tex != 0);
    REQUIRE(cache.getTextureById(static_cast<uint16_t>(tex)).isValid());
    REQUIRE(am.residentBytes() == 65536);

    // Load two more -> 3 * 65536 = 196608 > budget -> one is evicted. cockpit (priority 5) must NOT be it;
    // reactor (loaded before engine, same priority) is the oldest low-priority -> evicted.
    am.resolve("reactor");
    am.resolve("engine");
    REQUIRE(am.residentBytes() <= 160000);
    REQUIRE(am.residentCount() == 2);
    REQUIRE(am.isResident("cockpit"));        // priority kept it
    REQUIRE(am.isResident("engine"));
    REQUIRE_FALSE(am.isResident("reactor"));  // evicted

    device->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
}
