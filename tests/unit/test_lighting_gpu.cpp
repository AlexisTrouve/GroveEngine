/**
 * GPU test: the lighting COMPOSITE actually reaches the pixels (lighting L1).
 *
 * Everything else about L1 is provable headlessly — the ambient parses, the pass emits or skips its
 * draw, the shaders compile. None of that proves the composite MULTIPLIES anything: a pass that
 * sampled the wrong texture, ignored u_ambient, or drew a quad the camera dragged off-screen would
 * satisfy every headless assertion and still render a wrong frame.
 *
 * So this reads back real pixels: one white full-screen sprite, composited under two different
 * ambients. Half-grey ambient must come out MEASURABLY DARKER than white ambient, and roughly half
 * as bright. A composite that ignored the ambient would return the same value twice; one that
 * ignored the scene would return the ambient itself regardless of what was drawn.
 *
 * ⚠️ The test binds the COMPOSITE view, not view 0 — the trap this file exists to avoid. Once
 *    lighting is on, the module redirects view 0 into the scene target, so the usual
 *    "setViewFramebuffer(0, fb)" trick of the other [gpu] tests would read back the UNLIT scene and
 *    pass while proving nothing. Predicted in docs/design/lighting-2d.md §7 before this was written.
 *
 * [gpu] — skips cleanly without a GPU.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Passes/CompositePass.h"
#include "RHI/RHIDevice.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <vector>
#include <memory>

using namespace grove;

TEST_CASE("lighting: the composite multiplies the scene by the ambient (GPU)", "[gpu][light]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 32, H = 32;
    SDL_Window* win = SDL_CreateWindow("light", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li_r");
    auto gIO = mgr.createInstance("li_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li_r"); mgr.removeInstance("li_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    // A WHITE sprite covering the view: the scene term is 1.0 everywhere, so whatever comes out of
    // the composite IS the ambient — which makes the two readings directly comparable.
    auto drawWhite = [&]{
        auto s = std::make_unique<JsonDataNode>("d");
        s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);     // cx,cy = CENTRE (anchor convention)
        s->setDouble("scaleX", W); s->setDouble("scaleY", H);
        s->setInt("color", static_cast<int>(0xFFFFFFFFu));
        s->setInt("layer", 10);
        gIO->publish("render:sprite", std::move(s));
    };

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    // Average luminance of the composited frame under `ambient`.
    auto luminanceUnder = [&](uint32_t ambient) -> int {
        { auto a = std::make_unique<JsonDataNode>("a");
          a->setInt("color", static_cast<int>(ambient));
          gIO->publish("render:ambient", std::move(a)); }

        for (int i = 0; i < 5; ++i) {
            drawWhite();
            // Re-bind EVERY frame: the module owns view redirection now and rebuilds its targets on
            // a resize, so a bind done once could be silently replaced.
            dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
            frame();
        }

        std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) return -1;

        long sum = 0; int n = 0;
        for (int sy = 4; sy < H - 4; sy += 4) for (int sx = 4; sx < W - 4; sx += 4) {
            const uint8_t* p = &rgba[(static_cast<size_t>(sy)*W + sx)*4];
            sum += (p[0] + p[1] + p[2]) / 3;      // grey, so any channel would do — averaged for noise
            ++n;
        }
        return n ? static_cast<int>(sum / n) : -1;
    };

    const int full = luminanceUnder(0xFFFFFFFFu);   // ambient 1.0 -> white * 1.0
    const int half = luminanceUnder(0x808080FFu);   // ambient 0.5 -> white * 0.5

    INFO("luminance full=" << full << " half=" << half);
    REQUIRE(full > 0);
    REQUIRE(half > 0);

    // The ordering assertion is the one that bites: a composite ignoring u_ambient returns the same
    // number twice, and the margin makes "same" impossible to mistake for "slightly different".
    REQUIRE(full > half + 40);

    // ...and the ratio pins that it MULTIPLIES rather than merely dimming by some other means.
    // Generous bounds: the readback is 8-bit and the scene target is half-float, so exact halves are
    // not on offer — but 0.5x cannot be confused with 0.25x or 0.9x.
    const double ratio = static_cast<double>(half) / static_cast<double>(full);
    INFO("ratio=" << ratio);
    REQUIRE(ratio > 0.30);
    REQUIRE(ratio < 0.70);

    renderer->shutdown();
    mgr.removeInstance("li_r");
    mgr.removeInstance("li_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}
