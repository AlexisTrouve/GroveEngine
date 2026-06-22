/**
 * GPU test: TextPass clips text to a TextCommand's scissor rect (UI clipping, slice 2a-2a).
 *
 * WHAT : render a long RED string that spans well past the middle of the framebuffer, but clip it to
 *        the LEFT half ({0,0,P/2,P}). After TextPass runs, count lit (red) pixels per half: the LEFT
 *        half has text, the RIGHT half must be EMPTY (scissored away).
 *
 * WHY  : the clip rides on TextCommand; TextPass breaks the glyph batch on a clip change and emits a
 *        bgfx scissor. Robust to exact glyph metrics — we only assert "text on the left, none on the
 *        right". Without the scissor the long string spills right -> rightLit>0 -> fails by construction.
 *
 * HOW  : ortho world [0,P] -> full FB (1px = 1 unit). [gpu]: needs a real bgfx context; skips clean.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>

#include <SDL.h>
#include <SDL_syswm.h>

#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "RHI/RHICommandBuffer.h"
#include "Frame/FramePacket.h"
#include "Passes/TextPass.h"
#include "Shaders/ShaderManager.h"

#include <cstdint>
#include <vector>

using namespace grove;

TEST_CASE("TextPass scissors text to a clip rect (GPU)", "[gpu][text][clip]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("text-clip", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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
    rhi::ShaderHandle prog = shaders.getProgram("sprite");   // text reuses the sprite/textured program
    REQUIRE(prog.isValid());

    TextPass pass(prog);
    pass.setup(*device);

    const uint16_t P = 64;
    rhi::FramebufferHandle fb = device->createFramebuffer(P, P);

    const float g = static_cast<float>(P);
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float proj[16] = { 2.0f/g,0,0,0,  0,2.0f/g,0,0,  0,0,1,0,  -1.0f,-1.0f,0,1 };
    device->setViewFramebuffer(0, fb);
    device->setViewRect(0, 0, 0, P, P);
    device->setViewClear(0, 0x000000FFu, 1.0f);
    device->setViewTransform(0, view, proj);

    // A long red string that runs past the middle, clipped to the left half.
    const char* longStr = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";  // 30 wide glyphs -> spills past x=P/2
    TextCommand tc{};
    tc.x = 0.0f; tc.y = 24.0f;
    tc.text = longStr;
    tc.fontId = 0; tc.fontSize = 18; tc.color = 0xFF0000FFu; tc.layer = 0;
    tc.clipX = 0.0f; tc.clipY = 0.0f; tc.clipW = g * 0.5f; tc.clipH = g;   // left half only

    FramePacket frame;
    frame.texts = &tc; frame.textCount = 1;

    rhi::RHICommandBuffer cmd;
    pass.execute(frame, *device, cmd);
    device->executeCommandBuffer(cmd);
    device->frame();

    std::vector<uint8_t> px(static_cast<size_t>(P) * P * 4, 0);
    REQUIRE(device->readFramebuffer(fb, px.data(), static_cast<uint32_t>(px.size())));

    int leftLit = 0, rightLit = 0;
    for (int y = 0; y < P; ++y) {
        for (int x = 0; x < P; ++x) {
            const int R = static_cast<int>(px[(static_cast<size_t>(y) * P + x) * 4 + 0]);
            if (R > 100) { (x < P / 2 ? leftLit : rightLit) += 1; }
        }
    }
    INFO("leftLit=" << leftLit << " rightLit=" << rightLit);
    CHECK(leftLit > 0);       // text rendered in the clip region
    CHECK(rightLit == 0);     // nothing escaped past the scissor

    pass.shutdown(*device);
    SDL_DestroyWindow(win);
    SDL_Quit();
}
