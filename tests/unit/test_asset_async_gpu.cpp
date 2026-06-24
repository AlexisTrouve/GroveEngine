/**
 * GPU test: the REAL async load path (phase 3) end to end. ThreadedDecoder decodes an actual PNG on a worker
 * thread; BgfxTextureProvider uploads it on this (render) thread when pumpAsync() picks up the finished decode.
 * Proves: resolve() returns immediately WITHOUT loading (placeholder), the texture appears after pump once the
 * worker is done, and the resident bytes match the real image. [gpu] — skips cleanly with no GPU.
 *
 * The ship PNG is 128x128 RGBA8 = 65536 bytes.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

#include "RHI/RHIDevice.h"
#include "Resources/ResourceCache.h"
#include "Assets/BgfxTextureProvider.h"
#include "Assets/AssetManager.h"
#include "Assets/ThreadedDecoder.h"

#include <string>
#include <thread>
#include <chrono>

using namespace grove;

TEST_CASE("AssetManager async: ThreadedDecoder decodes off-thread, pump uploads a real PNG (GPU)", "[gpu][assets][async]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("asset-async-gpu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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
    assets::AssetManager am(&provider, /*vram budget*/ 1000000);
    assets::ThreadedDecoder decoder(/*workers*/ 1);
    am.setAsyncDecoder(&decoder);
    am.setPlaceholder(0);

    am.registerAsset("cockpit", "../../assets/textures/ship/cockpit.png", /*priority*/ 5);

    // First touch: async path kicks an off-thread decode and returns the placeholder. Upload only happens on
    // pump, which we haven't called -> the asset CANNOT be resident yet, regardless of how fast the worker is.
    REQUIRE(am.resolve("cockpit") == 0);
    REQUIRE_FALSE(am.isResident("cockpit"));

    // Pump until the worker's decode lands and gets uploaded (bounded: ~1s so a stuck decode fails instead of
    // hanging the suite). Real timing-dependent wait, hence the loop + tiny sleep.
    bool resident = false;
    for (int i = 0; i < 200 && !resident; ++i) {
        am.pumpAsync();
        resident = am.isResident("cockpit");
        if (!resident) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(resident);                                       // the texture streamed in off-thread

    const uint32_t tex = am.resolve("cockpit");             // now resolves to the REAL uploaded texture
    REQUIRE(tex != 0);
    REQUIRE(cache.getTextureById(static_cast<uint16_t>(tex)).isValid());
    REQUIRE(am.residentBytes() == 65536);                   // 128*128*4 — matches the real image

    device->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
}
