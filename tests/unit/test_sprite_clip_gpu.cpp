/**
 * GPU test: SpritePass clips a sprite to its reserved[] scissor rect (UI clipping, slice 2a-1).
 *
 * WHAT : render ONE red sprite covering the whole framebuffer, but tag it with a clip rect = the
 *        LEFT half (reserved = {0,0,P/2,P}). After SpritePass runs, the left-center pixel must be RED
 *        (inside the clip) and the right-center pixel must be the CLEAR color (scissored away).
 *
 * WHY  : the clip rect rides in SpriteInstance.reserved[] (uploaded but ignored by the shader);
 *        SpritePass reads it CPU-side and emits a per-batch bgfx scissor. This proves that path on
 *        real hardware. Without the scissor the whole FB is red -> the right-half assert fails, so the
 *        test is decisive by construction.
 *
 * HOW  : an ortho mapping world [0,P] -> the full FB, so a sprite at (0,0) scaled PxP fills it. [gpu]:
 *        needs a real bgfx context; skips cleanly without one (like the tilemap LOD GPU test).
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>

#include <SDL.h>
#include <SDL_syswm.h>

#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "RHI/RHICommandBuffer.h"
#include "Frame/FramePacket.h"
#include "Passes/SpritePass.h"
#include "Shaders/ShaderManager.h"

#include <cstdint>
#include <vector>

using namespace grove;

TEST_CASE("SpritePass scissors a sprite to its reserved clip rect (GPU)", "[gpu][sprite][clip]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("sprite-clip", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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

    ShaderManager shaders;
    shaders.init(*device, device->getCapabilities().rendererName);
    rhi::ShaderHandle prog = shaders.getProgram("sprite");
    REQUIRE(prog.isValid());

    SpritePass pass(prog);
    pass.setup(*device);

    const uint16_t P = 64;
    rhi::FramebufferHandle fb = device->createFramebuffer(P, P);

    // ortho mapping world [0,P] -> NDC, so a sprite at (0,0) scaled PxP fills the whole FB.
    const float g = static_cast<float>(P);
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float proj[16] = { 2.0f/g,0,0,0,  0,2.0f/g,0,0,  0,0,1,0,  -1.0f,-1.0f,0,1 };
    device->setViewFramebuffer(0, fb);
    device->setViewRect(0, 0, 0, P, P);
    device->setViewClear(0, 0x000000FFu, 1.0f);   // black clear: scissored-away pixels stay black
    device->setViewTransform(0, view, proj);

    SpriteInstance s{};
    s.x = 0.0f; s.y = 0.0f; s.scaleX = g; s.scaleY = g;
    s.u0 = 0.0f; s.v0 = 0.0f; s.u1 = 1.0f; s.v1 = 1.0f;
    s.textureId = 0.0f;                 // default white 4x4 texture, tinted by the color below
    s.layer = 0.0f;
    s.r = 1.0f; s.g = 0.0f; s.b = 0.0f; s.a = 1.0f;   // red
    s.reserved[0] = 0.0f; s.reserved[1] = 0.0f;       // clip to the LEFT half of the framebuffer
    s.reserved[2] = g * 0.5f; s.reserved[3] = g;

    FramePacket frame;
    frame.sprites = &s; frame.spriteCount = 1;
    frame.mainView.viewportW = 100000; frame.mainView.viewportH = 100000;  // disable the view cull

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, *device, cmd);
    device->executeCommandBuffer(cmd);
    device->frame();

    std::vector<uint8_t> px(static_cast<size_t>(P) * P * 4, 0);
    REQUIRE(device->readFramebuffer(fb, px.data(), static_cast<uint32_t>(px.size())));
    auto chan = [&](int x, int y, int c) {
        return static_cast<int>(px[(static_cast<size_t>(y) * P + x) * 4 + c]);
    };
    const int lx = P / 4, rx = 3 * P / 4, cy = P / 2;   // left-center (kept) vs right-center (clipped)
    INFO("left R=" << chan(lx,cy,0) << " | right R=" << chan(rx,cy,0) << " G=" << chan(rx,cy,1) << " B=" << chan(rx,cy,2));
    // Left half: the red sprite is drawn.
    CHECK(chan(lx, cy, 0) > 200);
    CHECK(chan(lx, cy, 1) < 60);
    CHECK(chan(lx, cy, 2) < 60);
    // Right half: scissored away -> stays the black clear color.
    CHECK(chan(rx, cy, 0) < 60);
    CHECK(chan(rx, cy, 1) < 60);
    CHECK(chan(rx, cy, 2) < 60);

    pass.shutdown(*device);
    SDL_DestroyWindow(win);
    SDL_Quit();
}
