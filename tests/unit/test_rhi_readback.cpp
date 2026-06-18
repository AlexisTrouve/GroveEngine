/**
 * GPU test: offscreen framebuffer readback (Slice ② — the render-test harness foundation).
 *
 * WHAT  : Render into an offscreen RGBA8 framebuffer, then read the pixels back to the CPU and
 *         assert their value. Here the "render" is just a clear to a known color — the simplest
 *         thing that exercises the whole chain (createFramebuffer -> setViewFramebuffer -> clear ->
 *         frame -> blit -> readTexture -> CPU pixels).
 *
 * WHY    : this is how the industry tests rendering without eyeballing — objective, automatic pixel
 *         assertions. Once this foundation works, ②.2 renders the tilemap and asserts analytically
 *         (zoom-out pixel == average of the tile colors, etc.).
 *
 * HOW    : needs a REAL GPU context (hidden SDL window + bgfx D3D11), so this is a [gpu] test. It
 *         skips cleanly (no failure) when there is no video/GPU, so a headless CI just ignores it;
 *         on a GPU machine the pixel assertions run for real.
 */

#define SDL_MAIN_HANDLED  // Catch2 owns main(); don't let SDL hijack it.

#include <catch2/catch_test_macros.hpp>

#include <SDL.h>
#include <SDL_syswm.h>

#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"

#include <cstdint>
#include <vector>

using namespace grove;

TEST_CASE("RHI offscreen framebuffer readback returns the cleared color", "[gpu][readback]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        WARN("SDL video unavailable — skipping GPU readback test");
        return;
    }

    SDL_Window* win = SDL_CreateWindow("rhi-readback", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       64, 64, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }

    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    REQUIRE(SDL_GetWindowWMInfo(win, &wmi));
#ifdef _WIN32
    void* nwh = wmi.info.win.window;
    void* ndt = nullptr;
#else
    void* nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
    void* ndt = wmi.info.x11.display;
#endif

    auto device = rhi::IRHIDevice::create();
    if (!device->init(nwh, ndt, 64, 64)) {
        SDL_DestroyWindow(win); SDL_Quit();
        WARN("no GPU context — skipping");
        return;
    }

    const uint16_t W = 32, H = 32;
    rhi::FramebufferHandle fb = device->createFramebuffer(W, H);

    // setViewClear takes 0xRRGGBBAA -> R=0x33, G=0x66, B=0xCC, A=0xFF.
    const uint32_t clearRGBA = 0x3366CCFFu;
    device->setViewFramebuffer(0, fb);
    device->setViewRect(0, 0, 0, W, H);
    device->setViewClear(0, clearRGBA, 1.0f);
    device->frame();  // clears the FB's render target

    std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 0);
    REQUIRE(device->readFramebuffer(fb, px.data(), static_cast<uint32_t>(px.size())));

    // Center pixel must be the clear color. RGBA8 readback is bytes [R,G,B,A].
    const size_t c = (static_cast<size_t>(H / 2) * W + (W / 2)) * 4;
    CHECK(static_cast<int>(px[c + 0]) == 0x33);
    CHECK(static_cast<int>(px[c + 1]) == 0x66);
    CHECK(static_cast<int>(px[c + 2]) == 0xCC);
    CHECK(static_cast<int>(px[c + 3]) == 0xFF);

    device->destroy(fb);
    device->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
}
