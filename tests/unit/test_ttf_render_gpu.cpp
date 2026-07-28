/**
 * GPU test: a real TrueType face actually reaches the SCREEN, and the ellipsis really truncates.
 *
 * WHAT : bakes a system TTF into TextPass's font, renders through the real pass into an offscreen
 *        framebuffer, and reads the pixels back.
 *
 * WHY  : TtfFontUnit proves the bake produces sane METRICS, and IT_060 proves messages are published
 *        — neither proves a glyph is drawn. The engine's own rule is that reading code is not proof.
 *        Two properties are asserted on real pixels, and both FAIL BY CONSTRUCTION on the old font:
 *          1. "iii" covers a NARROWER horizontal span than "MMM". The 8x8 bitmap is monospace, so it
 *             would make the two spans equal — this is the quality jump, measured in lit pixels.
 *          2. `maxWidth` truncation keeps the drawn text inside its budget, ellipsis included. Without
 *             it the long string runs to the right edge.
 *
 * HOW  : same harness as the text-clip GPU test (ortho world [0,P] -> full FB, 1px = 1 unit).
 *        [gpu]: needs a real bgfx context; skips cleanly without one, and without a system font.
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
#include <string>
#include <vector>

using namespace grove;

namespace {
const char* kFontCandidates[] = {
    // The face the ENGINE SHIPS (assets/fonts/, Apache 2.0) comes first, so this test covers the
    // asset we actually distribute rather than whatever the machine happens to have installed.
    // Both spellings: visual demos run from the project root, ctest runs from build/tests.
    "assets/fonts/roboto-regular.ttf",
    "../../assets/fonts/roboto-regular.ttf",

    "C:/Windows/Fonts/arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
};
const char* kBoldCandidates[] = {
    "assets/fonts/roboto-bold.ttf",
    "../../assets/fonts/roboto-bold.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
};
}

// Leftmost lit column of the last renderText() call (anchor check below).
static int g_leftmost = 0;

TEST_CASE("A baked TTF renders to the framebuffer, proportionally (GPU)", "[gpu][text][ttf]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    SDL_Window* win = SDL_CreateWindow("ttf-render", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       256, 64, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));
#ifdef _WIN32
    void* nwh = wmi.info.win.window; void* ndt = nullptr;
#else
    void* nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
    void* ndt = wmi.info.x11.display;
#endif

    auto device = rhi::IRHIDevice::create();
    if (!device->init(nwh, ndt, 256, 64)) { SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return; }

    ShaderManager shaders;
    shaders.init(*device, device->getCapabilities().rendererName);
    rhi::ShaderHandle prog = shaders.getProgram("sprite");   // text reuses the sprite/textured program
    REQUIRE(prog.isValid());

    TextPass pass(prog);
    pass.setup(*device);

    // Swap the 8x8 default for a real face. Without one, there is nothing to prove here.
    bool haveFont = false;
    for (const char* f : kFontCandidates) {
        if (pass.getFont().loadTTF(*device, f, 32.0f)) { haveFont = true; break; }
    }
    if (!haveFont) {
        pass.shutdown(*device); shaders.shutdown(*device); device->shutdown();
        SDL_DestroyWindow(win); SDL_Quit();
        WARN("no system TTF — skipping the TTF render check");
        return;
    }

    const uint16_t W = 256, H = 64;
    rhi::FramebufferHandle fb = device->createFramebuffer(W, H);
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float proj[16] = { 2.0f/W,0,0,0,  0,2.0f/H,0,0,  0,0,1,0,  -1.0f,-1.0f,0,1 };

    // Render one string and return {lit pixel count, rightmost lit column}.
    auto renderText = [&](const char* s, float maxWidth, int& outLit, bool bold = false) -> int {
        device->setViewFramebuffer(0, fb);
        device->setViewRect(0, 0, 0, W, H);
        device->setViewClear(0, 0x000000FFu, 1.0f);
        device->setViewTransform(0, view, proj);

        TextCommand tc{};
        tc.x = 2.0f; tc.y = 16.0f;
        tc.text = s;
        tc.fontId = 0; tc.fontSize = 16; tc.color = 0xFF0000FFu; tc.layer = 0;
        tc.bold = bold ? 1 : 0;
        tc.maxWidth = maxWidth;

        FramePacket frame;
        frame.texts = &tc; frame.textCount = 1;

        rhi::RHICommandBuffer cmd;
        pass.execute(frame, *device, cmd);
        device->executeCommandBuffer(cmd);
        device->frame();

        std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 0);
        REQUIRE(device->readFramebuffer(fb, px.data(), static_cast<uint32_t>(px.size())));

        int lit = 0, rightmost = -1;
        g_leftmost = W;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (px[(static_cast<size_t>(y) * W + x) * 4 + 0] > 100) {   // red channel
                    ++lit;
                    if (x > rightmost) rightmost = x;
                    if (x < g_leftmost) g_leftmost = x;
                }
            }
        }
        outLit = lit;
        return rightmost;
    };

    // 1. Glyphs actually reach the framebuffer.
    int litM = 0, litI = 0;
    const int rightM = renderText("MMMMMMMMMM", 0.0f, litM);   // 10 glyphs
    INFO("MMMMMMMMMM lit=" << litM << " rightmost=" << rightM);
    REQUIRE(litM > 0);

    // 2. THE proportional check, on pixels.
    //
    // ⚠️ Compare a LONG run, and require a LARGE gap. A short string does NOT discriminate: the
    // rightmost lit column is the right edge of the last glyph's INK, and an 'i' has narrower ink than
    // an 'M' even in a MONOSPACE font — only its ADVANCE is identical. Measured: with the 8x8 face this
    // test passed happily on "iii" vs "MMM", proving nothing. Over 10 glyphs the advance difference
    // COMPOUNDS: ~12px total for monospace (ink only) versus ~95px for a real face. 40 sits well clear
    // of both, so the assertion is about advances, not ink.
    const int rightI = renderText("iiiiiiiiii", 0.0f, litI);
    INFO("iiiiiiiiii lit=" << litI << " rightmost=" << rightI << " gap=" << (rightM - rightI));
    REQUIRE(litI > 0);
    REQUIRE(rightM - rightI > 40);

    // 3. Truncation, end to end: a long string capped at 60px must stay inside its budget (plus a
    //    couple of px of antialiasing slack) instead of running to the right edge.
    int litLong = 0;
    const int rightUncapped = renderText("MMMMMMMMMMMM", 0.0f, litLong);
    const int rightCapped   = renderText("MMMMMMMMMMMM", 60.0f, litLong);
    INFO("uncapped=" << rightUncapped << " capped=" << rightCapped);
    REQUIRE(rightUncapped > rightCapped);       // the cap really shortened the drawn text
    REQUIRE(rightCapped <= 2 + 60 + 2);         // x origin + budget + AA slack

    // 3b. ANCHOR. Text must START at the requested x, not straddle it.
    //
    //     SpriteInstance.x/y is a CENTRE (vs_sprite offsets by -0.5). TextPass used to write the
    //     glyph's TOP-LEFT there, so every glyph sat half its own width too far left. With the old 8x8
    //     MONOSPACE font that was one constant shift of the whole string and nobody noticed; with a
    //     proportional face it varies per letter and reads as broken spacing ("Panel + widgets" ->
    //     "Panel+w idgets"). This locks the fix where a monospace font structurally could not.
    int litAnchor = 0;
    renderText("M", 0.0f, litAnchor);
    INFO("text x=2, leftmost lit column=" << g_leftmost);
    REQUIRE(litAnchor > 0);
    REQUIRE(g_leftmost >= 1);   // x=2 minus a little bearing/AA; the bug put it ~7px further left

    // 4. Bold renders through the SECOND atlas.
    //
    //    ⚠️ Measured first, asserted second. My initial discriminator was the horizontal EXTENT, on the
    //    theory that a real weight is wider — it is not, meaningfully: Roboto Bold's 'M' advance matches
    //    Regular's almost exactly (measured right=90 for both). What separates the two by a mile is INK:
    //    418 lit pixels regular vs 617 bold, +48%. So the assertion is on ink, with a threshold far
    //    below the observed gap. The exact metric-level proof that the two faces really are distinct
    //    lives in TtfFontUnit (bold 'M' has visibly thicker strokes), where it can be exact.
    bool haveBold = false;
    for (const char* f : kBoldCandidates) {
        if (pass.getFontBold().loadTTF(*device, f, 32.0f)) { haveBold = true; break; }
    }
    if (haveBold) {
        int litReg = 0, litBold = 0;
        const int rightReg  = renderText("MMMMMMMM", 0.0f, litReg, /*bold=*/false);
        const int rightBold = renderText("MMMMMMMM", 0.0f, litBold, /*bold=*/true);
        INFO("regular lit=" << litReg << " | bold lit=" << litBold);
        REQUIRE(litReg > 0);
        REQUIRE(litBold > litReg * 5 / 4);   // +25% floor; observed +48%
    } else {
        WARN("no bold face — skipping the real-bold check");
    }

    device->destroy(fb);
    pass.shutdown(*device);
    shaders.shutdown(*device);
    device->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
}
