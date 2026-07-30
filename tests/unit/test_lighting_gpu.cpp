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

#include <cmath>
#include <cstdlib>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_test_macros.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Passes/CompositePass.h"
#include "Passes/PresentPass.h"
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

TEST_CASE("lighting: a radial light brightens its centre, under a PANNED+ZOOMED camera (GPU)",
          "[gpu][light]") {
    // The camera is deliberately far from the origin AND zoomed. That is not decoration:
    //
    //   - lights are published in WORLD coordinates and drawn on their own view, so if that view
    //     lost the world camera the lamp would land somewhere else entirely — with a pan of 1000 it
    //     would be far off-screen and the frame would come back uniformly dark;
    //   - with the DEFAULT camera the world transform is near-identity, so a missing matrix would
    //     change almost nothing and this test would pass while proving nothing.
    //
    // So the pan is what makes the assertion below bite on the transform, not just on the falloff.
    // Predicted in docs/design/lighting-2d.md §7.4 before this test existed.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 64, H = 64;
    SDL_Window* win = SDL_CreateWindow("light2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li2_r");
    auto gIO = mgr.createInstance("li2_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li2_r"); mgr.removeInstance("li2_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    // Camera at world (1000,1000), zoom 2 -> the 64px viewport shows world 1000..1032.
    const double camX = 1000.0, camY = 1000.0, zoom = 2.0;
    const double midWorldX = camX + (W / 2.0) / zoom;   // 1016 -> screen centre
    const double midWorldY = camY + (H / 2.0) / zoom;

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x", camX); cam->setDouble("y", camY); cam->setDouble("zoom", zoom);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    for (int i = 0; i < 6; ++i) {
        // A white sprite covering the whole view (32 world units at zoom 2 = 64 px).
        { auto s = std::make_unique<JsonDataNode>("d");
          s->setDouble("cx", midWorldX); s->setDouble("cy", midWorldY);
          s->setDouble("scaleX", W / zoom); s->setDouble("scaleY", H / zoom);
          s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
          gIO->publish("render:sprite", std::move(s)); }
        // A DIM ambient, so anything bright can only come from the lamp.
        { auto a = std::make_unique<JsonDataNode>("a");
          a->setInt("color", static_cast<int>(0x303030FFu));
          gIO->publish("render:ambient", std::move(a)); }
        // The lamp, centred on the sprite in WORLD space. radius 8 world = 16 px on screen.
        { auto l = std::make_unique<JsonDataNode>("l");
          l->setDouble("cx", midWorldX); l->setDouble("cy", midWorldY);
          l->setDouble("radius", 8.0);
          l->setInt("color", static_cast<int>(0xFFFFFFFFu));
          l->setDouble("intensity", 1.0);
          gIO->publish("render:light", std::move(l)); }

        dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
        frame();
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));

    auto lumaAt = [&](int x, int y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y)*W + x)*4];
        return (p[0] + p[1] + p[2]) / 3;
    };

    const int centre = lumaAt(W/2, H/2);
    // Corners: outside the lamp's 16px screen radius, so they carry the ambient alone.
    const int corner = (lumaAt(3,3) + lumaAt(W-4,3) + lumaAt(3,H-4) + lumaAt(W-4,H-4)) / 4;

    INFO("centre=" << centre << " corner=" << corner);

    // THE assertion. It fails two different ways for two different bugs: if the lamp never drew, or
    // if the light view lost the world camera and the lamp landed off-screen, centre == corner.
    REQUIRE(centre > corner + 40);

    // The corners must still show the ambient — a composite that lit everything uniformly, or one
    // that dropped the scene, would break this.
    REQUIRE(corner > 5);

    renderer->shutdown();
    mgr.removeInstance("li2_r");
    mgr.removeInstance("li2_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}

TEST_CASE("lighting: a CONE light brightens forward and leaves behind dark (GPU)", "[gpu][light][cone]") {
    // The whole point of a cone is asymmetry, so the two probes sit at the SAME DISTANCE from the
    // lamp, one in front and one behind. Comparing "near vs far" instead would only re-prove the
    // radial falloff L2 already covers, and would be green with the cone entirely ignored.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 64, H = 64;
    SDL_Window* win = SDL_CreateWindow("light3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li3_r");
    auto gIO = mgr.createInstance("li3_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li3_r"); mgr.removeInstance("li3_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    // The lamp sits at the centre and points RIGHT (dirDeg 0 = +x, the grove::fx convention).
    const double cx = W * 0.5, cy = H * 0.5;
    for (int i = 0; i < 6; ++i) {
        { auto s = std::make_unique<JsonDataNode>("d");
          s->setDouble("cx", cx); s->setDouble("cy", cy);
          s->setDouble("scaleX", W); s->setDouble("scaleY", H);
          s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
          gIO->publish("render:sprite", std::move(s)); }
        { auto a = std::make_unique<JsonDataNode>("a");
          a->setInt("color", static_cast<int>(0x303030FFu));
          gIO->publish("render:ambient", std::move(a)); }
        { auto l = std::make_unique<JsonDataNode>("l");
          l->setDouble("cx", cx); l->setDouble("cy", cy);
          l->setDouble("radius", 28.0);
          l->setInt("color", static_cast<int>(0xFFFFFFFFu));
          l->setDouble("intensity", 1.5);
          l->setDouble("dirDeg", 0.0);        // +x
          l->setDouble("spreadDeg", 60.0);    // ±30°
          gIO->publish("render:light", std::move(l)); }

        dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
        frame();
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));

    auto lumaAt = [&](int x, int y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y)*W + x)*4];
        return (p[0] + p[1] + p[2]) / 3;
    };

    const int offset = 14;                                   // same radius, opposite sides
    const int front = lumaAt(static_cast<int>(cx) + offset, static_cast<int>(cy));
    const int back  = lumaAt(static_cast<int>(cx) - offset, static_cast<int>(cy));

    INFO("front=" << front << " back=" << back);

    // A light that ignored the cone would return the same number twice — same distance, same falloff.
    REQUIRE(front > back + 60);
    // And behind the lamp there must be nothing but the ambient: a cone that merely DIMMED the back
    // instead of cutting it would still pass the comparison above.
    REQUIRE(back < 60);

    renderer->shutdown();
    mgr.removeInstance("li3_r");
    mgr.removeInstance("li3_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}

TEST_CASE("lighting: a wall casts a SHADOW — dark behind, lit beside, same distance (GPU)",
          "[gpu][light][occluder]") {
    // The two probes sit at the SAME DISTANCE from the lamp, one behind the wall and one clear of
    // it. That is the whole design of this test: comparing "behind the wall" to "far away" would
    // only re-prove the radial falloff L2 already covers, and would be green with occlusion
    // entirely absent. Same trap, and same remedy, as the cone test's equal-distance probes.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 96, H = 96;
    SDL_Window* win = SDL_CreateWindow("light4", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li4_r");
    auto gIO = mgr.createInstance("li4_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li4_r"); mgr.removeInstance("li4_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    // Lamp at the centre. A vertical wall to its RIGHT, leaving the LEFT clear.
    const double cx = W * 0.5, cy = H * 0.5;
    const double wallX = cx + 16.0;

    for (int i = 0; i < 6; ++i) {
        { auto s = std::make_unique<JsonDataNode>("d");
          s->setDouble("cx", cx); s->setDouble("cy", cy);
          s->setDouble("scaleX", W); s->setDouble("scaleY", H);
          s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
          gIO->publish("render:sprite", std::move(s)); }
        { auto a = std::make_unique<JsonDataNode>("a");
          a->setInt("color", static_cast<int>(0x303030FFu));
          gIO->publish("render:ambient", std::move(a)); }
        { auto l = std::make_unique<JsonDataNode>("l");
          l->setDouble("cx", cx); l->setDouble("cy", cy);
          l->setDouble("radius", 44.0);
          l->setInt("color", static_cast<int>(0xFFFFFFFFu));
          l->setDouble("intensity", 2.0);
          gIO->publish("render:light", std::move(l)); }
        // The wall: a tall thin rect. x,y = top-left CORNER.
        { auto o = std::make_unique<JsonDataNode>("o");
          o->setDouble("x", wallX); o->setDouble("y", 0.0);
          o->setDouble("w", 4.0);   o->setDouble("h", static_cast<double>(H));
          gIO->publish("render:occluder", std::move(o)); }

        dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
        frame();
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));

    auto lumaAt = [&](int x, int y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y)*W + x)*4];
        return (p[0] + p[1] + p[2]) / 3;
    };

    const int offset = 28;   // beyond the wall on the right, and its mirror on the clear left
    const int shadowed = lumaAt(static_cast<int>(cx) + offset, static_cast<int>(cy));
    const int lit      = lumaAt(static_cast<int>(cx) - offset, static_cast<int>(cy));

    INFO("shadowed=" << shadowed << " lit=" << lit);

    // Occlusion absent would return the same number twice: same distance, same falloff, same cone.
    REQUIRE(lit > shadowed + 50);
    // And behind the wall there must be nothing but the ambient — a wall that merely DIMMED would
    // still satisfy the comparison above.
    REQUIRE(shadowed < 60);

    renderer->shutdown();
    mgr.removeInstance("li4_r");
    mgr.removeInstance("li4_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}

TEST_CASE("lighting: a red filter TINTS the light behind it — red survives, blue collapses (GPU)",
          "[gpu][light][filter]") {
    // THE assertion of plan F, and it is deliberately not about brightness. A filter darkens, but so
    // does a semi-opaque wall, and so does simply moving further from the lamp. What distinguishes a
    // TINT is that the channels DIVERGE: red comes through untouched while blue is eaten. A test on
    // luminance would be green with a plain grey pane — that is, green while proving nothing.
    //
    // Probes on the same circle around the lamp, as in the wall test: one behind the glass, one
    // clear of it. Comparing "behind" to "far away" would only re-prove the radial falloff.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 96, H = 96;
    SDL_Window* win = SDL_CreateWindow("light5", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li5_r");
    auto gIO = mgr.createInstance("li5_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li5_r"); mgr.removeInstance("li5_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    const double cx = W * 0.5, cy = H * 0.5;
    const double glassX = cx + 16.0;

    for (int i = 0; i < 6; ++i) {
        // A WHITE scene, so any colour in the result came from the light path and nowhere else.
        { auto s = std::make_unique<JsonDataNode>("d");
          s->setDouble("cx", cx); s->setDouble("cy", cy);
          s->setDouble("scaleX", W); s->setDouble("scaleY", H);
          s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
          gIO->publish("render:sprite", std::move(s)); }
        // A DIM ambient: it adds to every channel equally, so a bright one would dilute the very
        // divergence being measured. It cannot be zero — zero turns lighting off entirely.
        { auto a = std::make_unique<JsonDataNode>("a");
          a->setInt("color", static_cast<int>(0x101010FFu));
          gIO->publish("render:ambient", std::move(a)); }
        // A WHITE lamp: the tint must be produced by the glass, not smuggled in by the light.
        { auto l = std::make_unique<JsonDataNode>("l");
          l->setDouble("cx", cx); l->setDouble("cy", cy);
          l->setDouble("radius", 44.0);
          l->setInt("color", static_cast<int>(0xFFFFFFFFu));
          l->setDouble("intensity", 4.0);
          gIO->publish("render:light", std::move(l)); }
        // The stained glass: a tall thin pane, x,y = top-left CORNER. Its thin axis (4) is the
        // thickness `color` is stated for.
        { auto f = std::make_unique<JsonDataNode>("f");
          f->setDouble("x", glassX); f->setDouble("y", 0.0);
          f->setDouble("w", 4.0);    f->setDouble("h", static_cast<double>(H));
          f->setInt("color", static_cast<int>(0xFF3333FFu));   // red glass: transmits red, eats blue
          gIO->publish("render:filter", std::move(f)); }

        dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
        frame();
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));

    auto channelAt = [&](int x, int y, int c) {
        return static_cast<int>(rgba[(static_cast<size_t>(y)*W + x)*4 + c]);
    };

    const int offset = 28;   // beyond the glass on the right, and its mirror on the clear left
    const int px = static_cast<int>(cx) + offset, py = static_cast<int>(cy);
    const int qx = static_cast<int>(cx) - offset;

    const int rBehind = channelAt(px, py, 0), bBehind = channelAt(px, py, 2);
    const int rClear  = channelAt(qx, py, 0), bClear  = channelAt(qx, py, 2);

    INFO("behind r=" << rBehind << " b=" << bBehind << " | clear r=" << rClear << " b=" << bClear);

    // 1. The channels diverge behind the glass. A GREY pane — or any plain attenuation — would
    //    return two equal numbers here, which is the whole point of asserting on the ratio.
    REQUIRE(rBehind > bBehind + 40);

    // 2. Red passes through very nearly untouched. Without this, a filter that merely dimmed
    //    everything a little more in blue would still satisfy (1).
    REQUIRE(rBehind > rClear - 30);

    // 3. ...and blue really did collapse, rather than red having been boosted.
    REQUIRE(bBehind < bClear - 40);

    // 4. On the clear side the light is still WHITE: the glass tinted the ray that crossed it, not
    //    the lamp. A filter applied globally would show up right here.
    REQUIRE(std::abs(rClear - bClear) < 12);

    renderer->shutdown();
    mgr.removeInstance("li5_r");
    mgr.removeInstance("li5_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}

TEST_CASE("lighting: a shadow edge is a STRAIGHT line, not a staircase (GPU)",
          "[gpu][light][occluder][march]") {
    // The regression this locks. The occlusion march used a FIXED number of steps between the lamp
    // and the fragment, so one step spanned `distance / N` world units — the further the fragment,
    // the coarser the sampling. The shadow edge came out as a STAIRCASE whose tread was the step
    // length (measured ~19 px under a 340-unit lamp), and it got worse as lamps got bigger.
    //
    // The geometry says what the answer must be: the shadow boundary cast by a POINT light past a
    // corner is a RAY, i.e. a straight line. So the test fits a line through the measured boundary
    // and bounds the deviation. A staircase fails it by construction; asserting merely that "there
    // is a shadow" would not, which is why this test measures a SHAPE and not a level.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 192, H = 192;
    SDL_Window* win = SDL_CreateWindow("light6", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li6_r");
    auto gIO = mgr.createInstance("li6_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li6_r"); mgr.removeInstance("li6_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    // Lamp upper-left; a block whose BOTTOM-LEFT corner casts the edge we measure.
    //
    // ⚠️ The corner that casts is the one NEAREST the lamp on that side, not the far one. With the
    // lamp to the LEFT of the block, a ray only escapes below the block if it is already under y=80
    // when it reaches x=60 — the far corner never gets a say. Predicting from the far corner gives a
    // slope of 0.56 against a true 0.91, and the test fails while the engine is right.
    const double lx = 16.0, ly = 40.0;
    const double bx = 60.0, bw = 28.0, bh = 80.0;   // block spans y 0..80, x 60..88
    const double cornerX = bx, cornerY = bh;

    for (int i = 0; i < 6; ++i) {
        { auto s = std::make_unique<JsonDataNode>("d");
          s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);
          s->setDouble("scaleX", W); s->setDouble("scaleY", H);
          s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
          gIO->publish("render:sprite", std::move(s)); }
        { auto a = std::make_unique<JsonDataNode>("a");
          a->setInt("color", static_cast<int>(0x101010FFu));
          gIO->publish("render:ambient", std::move(a)); }
        { auto l = std::make_unique<JsonDataNode>("l");
          l->setDouble("cx", lx); l->setDouble("cy", ly);
          l->setDouble("radius", 400.0);
          l->setInt("color", static_cast<int>(0xFFFFFFFFu));
          l->setDouble("intensity", 3.0);
          gIO->publish("render:light", std::move(l)); }
        { auto o = std::make_unique<JsonDataNode>("o");
          o->setDouble("x", bx); o->setDouble("y", 0.0);
          o->setDouble("w", bw); o->setDouble("h", bh);
          gIO->publish("render:occluder", std::move(o)); }

        dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
        frame();
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
    auto luma = [&](int x, int y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y)*W + x)*4];
        return (p[0] + p[1] + p[2]) / 3;
    };

    // The boundary is where luma climbs out of "ambient only". Scanning DOWNWARD from the top, the
    // first lit row of a column is that column's shadow edge.
    const int kLitThreshold = 60;
    std::vector<double> xs, ys;
    for (int x = 110; x <= 180; x += 5) {
        for (int y = 0; y < H; ++y) {
            if (luma(x, y) > kLitThreshold) { xs.push_back(static_cast<double>(x)); ys.push_back(static_cast<double>(y)); break; }
        }
    }
    REQUIRE(xs.size() >= 12);   // the edge has to be FOUND before its shape can be judged

    // Least-squares line through the measured boundary, then the worst deviation from it.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double n = static_cast<double>(xs.size());
    for (size_t i = 0; i < xs.size(); ++i) { sx += xs[i]; sy += ys[i]; sxx += xs[i]*xs[i]; sxy += xs[i]*ys[i]; }
    const double slope = (n*sxy - sx*sy) / (n*sxx - sx*sx);
    const double icept = (sy - slope*sx) / n;
    double worst = 0.0;
    for (size_t i = 0; i < xs.size(); ++i) {
        const double dev = std::abs(ys[i] - (slope*xs[i] + icept));
        if (dev > worst) worst = dev;
    }

    // ...and it must be the RIGHT line: the ray from the lamp through the block's corner. A
    // perfectly straight edge in the wrong place would otherwise sail through.
    const double expectedSlope = (cornerY - ly) / (cornerX - lx);

    INFO("slope=" << slope << " expected=" << expectedSlope << " worst=" << worst << "px n=" << xs.size());

    // 2.5 px comes from MEASURING both states, not from taste: with the dither the edge deviates
    // 1.6 px from its fit line, without it 3.5 px (and its longest flat run goes from 3 px to 11).
    // A looser bound would sail through the staircase this test exists to forbid.
    REQUIRE(worst < 2.5);                              // straight, not a staircase
    REQUIRE(std::abs(slope - expectedSlope) < 0.06);   // and following the geometry

    // ...and the edge is ANTIALIASED, which is a separate property from being straight.
    //
    // An occluder writes 0, so the march's verdict is BINARY: one sample inside the wall annihilates
    // the product. Left alone that yields a hard cliff — perfectly straight and perfectly ugly, which
    // the two assertions above would happily accept. The lamp pass dithers the boundary across
    // neighbouring pixels and the composite resolves that dither into a ramp; what proves the pair is
    // still working is the EXISTENCE of intermediate values at the boundary.
    int columnsWithRamp = 0, columnsChecked = 0;
    for (size_t i = 0; i < xs.size(); ++i) {
        const int x  = static_cast<int>(xs[i]);
        const int ey = static_cast<int>(ys[i]);
        if (ey < 12 || ey > H - 12) continue;
        ++columnsChecked;
        const int dark  = luma(x, ey - 8);    // clearly in shadow
        const int bright = luma(x, ey + 8);   // clearly lit
        if (bright - dark < 40) continue;     // no usable contrast in this column
        for (int y = ey - 3; y <= ey + 3; ++y) {
            const int v = luma(x, y);
            if (v > dark + 15 && v < bright - 15) { ++columnsWithRamp; break; }
        }
    }
    INFO("columns with a ramp: " << columnsWithRamp << " / " << columnsChecked);
    REQUIRE(columnsChecked >= 10);
    REQUIRE(columnsWithRamp * 2 >= columnsChecked);   // a majority, not a lucky pixel

    renderer->shutdown();
    mgr.removeInstance("li6_r");
    mgr.removeInstance("li6_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}

TEST_CASE("lighting: fog absorbs EXPONENTIALLY — doubling the density squares what survives (GPU)",
          "[gpu][light][fog]") {
    // THE assertion of plan A1, and it is deliberately not "it got darker". A constant darkening, a
    // linear one, or a semi-opaque wall would all be darker too. What characterises Beer-Lambert is
    // that doubling alpha SQUARES the surviving light — so the test measures the same probe under
    // three media (none, alpha, 2*alpha) and checks L2 * L0 == L1^2.
    //
    // A linear absorber would give L1 = L0 - d and L2 = L0 - 2d, i.e. L2*L0 far below L1^2 (and here
    // it would land at zero). The relation cannot be satisfied by accident.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 96, H = 96;
    SDL_Window* win = SDL_CreateWindow("light7", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li7_r");
    auto gIO = mgr.createInstance("li7_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li7_r"); mgr.removeInstance("li7_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    const double lampX = 16.0, lampY = 48.0;
    const double fogX = 30.0, fogW = 40.0;   // the ray crosses 40 world units of medium
    const int probeX = 80, probeY = 48;

    // Mean luminance of a small patch at the probe, under a given fog density (0 = no fog at all).
    auto measure = [&](double density) -> int {
        for (int i = 0; i < 6; ++i) {
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);
              s->setDouble("scaleX", W); s->setDouble("scaleY", H);
              s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
              gIO->publish("render:sprite", std::move(s)); }
            { auto a = std::make_unique<JsonDataNode>("a");
              a->setInt("color", static_cast<int>(0x101010FFu));
              gIO->publish("render:ambient", std::move(a)); }
            { auto l = std::make_unique<JsonDataNode>("l");
              l->setDouble("cx", lampX); l->setDouble("cy", lampY);
              l->setDouble("radius", 200.0);
              l->setInt("color", static_cast<int>(0xFFFFFFFFu));
              l->setDouble("intensity", 1.2);
              gIO->publish("render:light", std::move(l)); }
            if (density > 0.0) {
                auto f = std::make_unique<JsonDataNode>("f");
                f->setDouble("x", fogX); f->setDouble("y", 0.0);
                f->setDouble("w", fogW); f->setDouble("h", static_cast<double>(H));
                f->setDouble("density", density);
                gIO->publish("render:fog", std::move(f));
            }
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
              cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) return -1;
        // Averaged over a patch: the march dithers its start per pixel, so a single sample carries
        // one step's worth of jitter (~5% here). The law is what is under test, not the jitter.
        long sum = 0; int n = 0;
        for (int y = probeY - 3; y <= probeY + 3; ++y)
            for (int x = probeX - 3; x <= probeX + 3; ++x) {
                const uint8_t* p = &rgba[(static_cast<size_t>(y)*W + x)*4];
                sum += (p[0] + p[1] + p[2]) / 3; ++n;
            }
        return n ? static_cast<int>(sum / n) : -1;
    };

    // alpha chosen so one crossing leaves about half the light: ln(2)/40.
    const double alpha = 0.01733;
    const int m0 = measure(0.0);
    const int m1 = measure(alpha);
    const int m2 = measure(alpha * 2.0);

    // The ambient rides on every reading and has no path through the fog — it is global by
    // construction — so it must be removed before the law can be read.
    const double ambient = 16.0;   // 0x10 / 255 * 255, scene is white
    const double L0 = m0 - ambient, L1 = m1 - ambient, L2 = m2 - ambient;

    INFO("m0=" << m0 << " m1=" << m1 << " m2=" << m2 << "  L0=" << L0 << " L1=" << L1 << " L2=" << L2);

    REQUIRE(L0 > 60.0);            // there is light to absorb in the first place
    REQUIRE(L1 < L0 - 15.0);       // the fog absorbs
    REQUIRE(L2 > 4.0);             // ...but does not simply zero it, which a linear model would

    // The law. 25% tolerance: the readback is 8-bit, the march is discrete, and the fog's edges land
    // between samples — but a linear or constant absorber misses this by a factor, not a percent.
    REQUIRE_THAT(L2 * L0, Catch::Matchers::WithinRel(L1 * L1, 0.25));

    renderer->shutdown();
    mgr.removeInstance("li7_r");
    mgr.removeInstance("li7_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}

TEST_CASE("lighting: a scattering medium GLOWS where there is no scene at all (GPU)",
          "[gpu][light][fog][scatter]") {
    // THE assertion of plan A2, and the scene is BLACK on purpose.
    //
    // The composite is `scene * (ambient + light) + scattered`. On any lit background the first term
    // is already non-zero, so a test would see light with OR without the scattered term and pass
    // while proving nothing. Against black, `scene * (ambient + light)` is EXACTLY zero — so a lit
    // pixel can only be explained by the additive term, which is the whole architectural claim of
    // this slice. Predicted in docs/design/lighting-attenuators.md §6 before this test existed.
    //
    // It is also the feature's real use case: a beam crossing a nebula in the void must be visible
    // although there is no surface anywhere for it to land on.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 96, H = 96;
    SDL_Window* win = SDL_CreateWindow("light8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li8_r");
    auto gIO = mgr.createInstance("li8_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li8_r"); mgr.removeInstance("li8_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);
    const double cx = W * 0.5, cy = H * 0.5;

    // fogMode: 0 = no fog, 1 = fog that only ABSORBS, 2 = fog that also SCATTERS.
    auto measureCentre = [&](int fogMode) -> int {
        for (int i = 0; i < 6; ++i) {
            // THE VOID. The clear defaults to a dark grey, which is not black — and "nearly black"
            // would leave a residue that could be mistaken for the very glow under test.
            { auto c = std::make_unique<JsonDataNode>("c");
              c->setInt("color", 0x000000FF);
              gIO->publish("render:clear", std::move(c)); }
            { auto a = std::make_unique<JsonDataNode>("a");
              a->setInt("color", static_cast<int>(0x202020FFu));   // non-zero: 0 would switch lighting OFF
              gIO->publish("render:ambient", std::move(a)); }
            { auto l = std::make_unique<JsonDataNode>("l");
              l->setDouble("cx", cx); l->setDouble("cy", cy);
              l->setDouble("radius", 40.0);
              l->setInt("color", static_cast<int>(0xFFFFFFFFu));
              l->setDouble("intensity", 2.0);
              gIO->publish("render:light", std::move(l)); }
            if (fogMode > 0) {
                auto f = std::make_unique<JsonDataNode>("f");
                f->setDouble("x", 0.0); f->setDouble("y", 0.0);
                f->setDouble("w", static_cast<double>(W)); f->setDouble("h", static_cast<double>(H));
                f->setDouble("density", 0.002);            // barely absorbing: absorption is not the subject
                if (fogMode == 2) f->setDouble("scatter", 0.8);
                gIO->publish("render:fog", std::move(f));
            }
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
              cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) return -1;
        const uint8_t* p = &rgba[(static_cast<size_t>(static_cast<int>(cy))*W + static_cast<int>(cx))*4];
        return (p[0] + p[1] + p[2]) / 3;
    };

    const int noFog    = measureCentre(0);
    const int absorbing = measureCentre(1);
    const int scattering = measureCentre(2);

    INFO("noFog=" << noFog << " absorbing=" << absorbing << " scattering=" << scattering);

    // 1. Against black, a lamp alone lights NOTHING. This is the control: it proves the background
    //    really is void, so anything measured below cannot come from the multiplicative term.
    REQUIRE(noFog < 6);

    // 2. Absorption alone changes nothing either — a medium that only absorbs is INVISIBLE in the
    //    void, which is precisely why plan A had to be rewritten to include scattering.
    REQUIRE(absorbing < 6);

    // 3. ...and with `scatter` the void GLOWS. Zero to bright, with the scene term pinned at zero
    //    throughout: the additive term is the only possible explanation.
    REQUIRE(scattering > 80);

    renderer->shutdown();
    mgr.removeInstance("li8_r");
    mgr.removeInstance("li8_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}

TEST_CASE("lighting: a nebula absorbs PROGRESSIVELY, and its bounding quad is invisible (GPU)",
          "[gpu][light][nebula]") {
    // Two claims, and the second is the one that makes this primitive worth existing.
    //
    // 1. PROGRESSIVE. A ray through the core is absorbed far more than a ray grazing the edge. A
    //    uniform disc — or a rect — would absorb the same everywhere it covered.
    // 2. The BOUNDING QUAD IS INVISIBLE. The volume is drawn as a square, but its density reaches
    //    exactly zero at the disc's rim, so a ray that crosses the square OUTSIDE the disc must come
    //    out bit-for-bit as if no nebula existed. Get that wrong and every cloud in the game wears a
    //    visible box — the exact failure that stacking rects produced.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 128, H = 128;
    SDL_Window* win = SDL_CreateWindow("light9", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("li9_r");
    auto gIO = mgr.createInstance("li9_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("li9_r"); mgr.removeInstance("li9_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }

    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    const double lampX = 10.0, lampY = 64.0;
    const double nebX = 64.0, nebY = 64.0, nebR = 25.0;

    auto render = [&](bool withNebula, std::vector<uint8_t>& out) {
        for (int i = 0; i < 6; ++i) {
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);
              s->setDouble("scaleX", W); s->setDouble("scaleY", H);
              s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
              gIO->publish("render:sprite", std::move(s)); }
            { auto a = std::make_unique<JsonDataNode>("a");
              a->setInt("color", static_cast<int>(0x080808FFu));
              gIO->publish("render:ambient", std::move(a)); }
            { auto l = std::make_unique<JsonDataNode>("l");
              l->setDouble("cx", lampX); l->setDouble("cy", lampY);
              l->setDouble("radius", 400.0);
              l->setInt("color", static_cast<int>(0xFFFFFFFFu));
              l->setDouble("intensity", 2.5);
              gIO->publish("render:light", std::move(l)); }
            if (withNebula) {
                auto n = std::make_unique<JsonDataNode>("n");
                n->setDouble("cx", nebX); n->setDouble("cy", nebY);
                n->setDouble("radius", nebR);
                n->setDouble("density", 0.05);   // scatter left at 0: absorption is what is under test
                gIO->publish("render:nebula", std::move(n));
            }
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
              cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            dev->setViewFramebuffer(CompositePass::kCompositeView, fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        out.assign(static_cast<size_t>(W)*H*4, 0);
        REQUIRE(dev->readFramebuffer(fb, out.data(), static_cast<uint32_t>(out.size())));
    };

    std::vector<uint8_t> without, with;
    render(false, without);
    render(true, with);

    auto luma = [&](const std::vector<uint8_t>& buf, int x, int y) {
        const uint8_t* p = &buf[(static_cast<size_t>(y)*W + x)*4];
        return (p[0] + p[1] + p[2]) / 3;
    };

    // Three probes, all beyond the volume, each compared to ITS OWN no-nebula value — so the radial
    // falloff of the lamp cancels out and only the medium's effect remains.
    //  - core:    the ray passes straight through the centre (impact parameter 0)
    //  - grazing: the ray clips the disc well off-centre (impact ~13 of 25)
    //  - clear:   the ray crosses the bounding SQUARE but never the disc
    const int probes[3][2] = { {110, 64}, {110, 40}, {110, 8} };
    double ratio[3];
    for (int i = 0; i < 3; ++i) {
        const double a = luma(without, probes[i][0], probes[i][1]);
        const double b = luma(with,    probes[i][0], probes[i][1]);
        REQUIRE(a > 40.0);            // there is light to absorb at every probe
        ratio[i] = b / a;
    }

    INFO("core=" << ratio[0] << " grazing=" << ratio[1] << " clear=" << ratio[2]);

    // 1. The core absorbs substantially.
    REQUIRE(ratio[0] < 0.80);

    // 2. PROGRESSIVE: the grazing ray keeps measurably more light than the core ray. A volume of
    //    uniform density would return the same figure for both, and assertion 1 alone would pass.
    REQUIRE(ratio[1] > ratio[0] + 0.06);

    // 3. THE QUAD IS INVISIBLE. This ray crosses the drawn square but never the disc, so it must be
    //    untouched. A square-shaped medium, or a falloff that does not reach zero at the rim, shows
    //    up here and nowhere else.
    REQUIRE(ratio[2] > 0.985);

    renderer->shutdown();
    mgr.removeInstance("li9_r");
    mgr.removeInstance("li9_g");
    SDL_DestroyWindow(win);
    SDL_Quit();
}

// ============================================================================
// Bloom (plan B) — la lueur DÉBORDE hors du rayon de la lampe.
//
// LE DISCRIMINANT, conçu avant d'écrire le shader (lighting-bloom.md §6.3) : on échantillonne un pixel
// SITUÉ HORS du rayon de la lampe. La retombée y vaut *exactement* 0 par construction — c'est ce qui
// rend le quad de la lampe correct — donc toute lumière mesurée là ne peut venir que du bloom.
// Mesurer au centre de la lampe ne discriminerait rien : il est déjà saturé, et une passe de bloom
// cassée y donnerait le même blanc.
//
// Quatre mesures, dans cet ordre :
//   1. la RÉFÉRENCE — bloom éteint.
//   2. la PLOMBERIE — bloom allumé mais avec un seuil INATTEIGNABLE : la sortie doit être identique à
//      la référence, alors que le trajet HDR complet a été emprunté (composite -> cible HDR ->
//      extraction -> deux flous -> présentation). Ça sépare « la plomberie déforme-t-elle l'image »
//      de « la lueur fonctionne-t-elle », deux échecs qu'une seule mesure mélangerait.
//   3. la LUEUR — même scène, seuil atteignable : le pixel hors rayon s'éclaircit.
//   4. la LOCALITÉ — un pixel du coin, hors de portée du flou, ne bouge PAS. Sans cette mesure, un
//      shader qui ajouterait une constante ou échantillonnerait la mauvaise texture passerait la 3.
//
// ⚠️ La lecture se fait sur la vue de PRÉSENTATION quand le bloom est actif, pas sur celle du
//    composite : le module redirige celle-ci vers la cible HDR à chaque frame. Un test qui lirait le
//    composite mesurerait la frame AVANT la lueur et serait vert en ne prouvant rien — le même piège
//    qu'en L1 (lighting-2d.md §7), un cran plus loin.
// ============================================================================

TEST_CASE("bloom: the glow reaches OUTSIDE the lamp radius, and only nearby (GPU)",
          "[gpu][light][bloom]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }

    // 128x128 et pas 32 : il faut de la place pour la lampe ET pour la portée du flou au-delà d'elle,
    // plus un coin franchement hors de portée pour la mesure de localité.
    const int W = 128, H = 128;
    SDL_Window* win = SDL_CreateWindow("bloom", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("bl_r");
    auto gIO = mgr.createInstance("bl_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("bl_r"); mgr.removeInstance("bl_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    const double cx = W * 0.5, cy = H * 0.5;
    const double lampRadius = 30.0;                       // unités MONDE = pixels (caméra par défaut)
    // 5 px HORS du bord de la lampe. Le premier essai échantillonnait à 45 px (15 px dehors) et
    // mesurait +5/255 : réel, mais faible — et pour une raison physique, pas un bug. Seule la partie
    // du disque dont la luminance dépasse le seuil brille (ici d < 15 px), et le flou étale cette
    // petite tache sur un rayon de 55 px, donc dilue son énergie d'environ un facteur 13. Mesurer
    // juste au bord garde le discriminant intact — la retombée y vaut EXACTEMENT zéro — avec un signal
    // franc. La résolution en croix du composite ne s'étend que d'un pixel, elle ne peut pas polluer
    // 5 px plus loin.
    const int    sampleX = static_cast<int>(cx + 35.0);
    const int    sampleY = static_cast<int>(cy);
    // Intensité de bloom 2 : un réglage de jeu courant (1 à 3), et deux fois plus punitif pour la
    // mesure de plomberie — une fuite du seuil inatteignable y serait doublée.
    const double bloomIntensity = 2.0;

    // Une scène BLANCHE : le terme de scène vaut 1 partout, donc ce qui sort du composite EST le terme
    // d'éclairage. Sans surface éclairée il n'y aurait rien du tout — la lumière MULTIPLIE la scène,
    // donc une scène noire resterait noire, lampe ou pas.
    auto publishScene = [&] {
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setInt("viewportX", 0); cam->setInt("viewportY", 0);
          cam->setInt("viewportW", W); cam->setInt("viewportH", H);
          gIO->publish("render:camera", std::move(cam)); }
        { auto s = std::make_unique<JsonDataNode>("d");
          s->setDouble("cx", cx); s->setDouble("cy", cy);          // cx,cy = CENTRE
          s->setDouble("scaleX", W); s->setDouble("scaleY", H);
          s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
          gIO->publish("render:sprite", std::move(s)); }
        // Ambiant TRÈS sombre : hors de la lampe la scène retombe à ~4 %, bien sous le début du genou
        // (seuil/2), donc ces pixels ne brillent pas d'eux-mêmes et tout ce qu'on mesurera là au-delà
        // de l'ambiant sera venu d'ailleurs.
        { auto a = std::make_unique<JsonDataNode>("a");
          a->setInt("color", static_cast<int>(0x0A0A0AFFu));
          gIO->publish("render:ambient", std::move(a)); }
        // La lampe : intensité 4, donc son disque dépasse largement le seuil de 1 et a de quoi
        // alimenter l'extraction. C'est la raison d'être du RGBA16F.
        { auto l = std::make_unique<JsonDataNode>("l");
          l->setDouble("cx", cx); l->setDouble("cy", cy);
          l->setDouble("radius", lampRadius);
          l->setInt("color", static_cast<int>(0xFFFFFFFFu));
          l->setDouble("intensity", 4.0);
          gIO->publish("render:light", std::move(l)); }
    };

    // Rend 5 frames avec les réglages donnés puis relit la cible. `bloomOn == false` publie
    // intensity 0, ce qui éteint tout le post-traitement (le contournement) : la frame finale sort
    // alors du composite lui-même, donc on lit sa vue.
    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
    auto render = [&](bool bloomOn, double intensity, double threshold, double radius) {
        for (int i = 0; i < 5; ++i) {
            publishScene();
            { auto b = std::make_unique<JsonDataNode>("b");
              b->setDouble("intensity", bloomOn ? intensity : 0.0);
              b->setDouble("threshold", threshold);
              b->setDouble("radius", radius);
              gIO->publish("render:bloom", std::move(b)); }
            // Ré-attaché CHAQUE frame : le module possède la redirection de vue et rebâtit ses cibles.
            dev->setViewFramebuffer(bloomOn ? PresentPass::kPresentView : CompositePass::kCompositeView, fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
    };
    auto lumaAt = [&](int x, int y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y) * W + x) * 4];
        return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
    };

    // ---- 1. Le bloom ÉTEINT : la référence -------------------------------------------------
    render(false, 0.0, 1.0, 40.0);
    const int offSample = lumaAt(sampleX, sampleY);
    const int offCentre = lumaAt(static_cast<int>(cx), static_cast<int>(cy));
    const int offCorner = lumaAt(4, 4);
    INFO("OFF  sample=" << offSample << " centre=" << offCentre << " corner=" << offCorner);
    REQUIRE(offCentre > 200);              // la lampe éclaire vraiment (sanité de la scène)
    REQUIRE(offSample < 40);               // et le point d'échantillonnage est bien hors de la lampe

    // ---- 2. La PLOMBERIE : bloom allumé, seuil inatteignable -------------------------------
    // Rien dans cette scène n'atteint une luminance de 40, donc l'extraction rend zéro partout et la
    // présentation ajoute zéro. L'image doit être celle du composite — bien qu'elle ait traversé la
    // cible HDR, deux flous et une passe de plus.
    render(true, bloomIntensity, 40.0, 40.0);
    const int plumbSample = lumaAt(sampleX, sampleY);
    const int plumbCentre = lumaAt(static_cast<int>(cx), static_cast<int>(cy));
    INFO("PLUMB sample=" << plumbSample << " (off " << offSample << ") centre=" << plumbCentre
         << " (off " << offCentre << ")");
    CHECK(std::abs(plumbSample - offSample) <= 2);   // 2 pour l'arrondi 8 bits, pas par tolérance
    CHECK(std::abs(plumbCentre - offCentre) <= 2);

    // ---- 3. La LUEUR : même scène, seuil atteignable ---------------------------------------
    render(true, bloomIntensity, 1.0, 40.0);
    const int onSample = lumaAt(sampleX, sampleY);
    const int onCorner = lumaAt(4, 4);
    INFO("ON   sample=" << onSample << " (off " << offSample << ") corner=" << onCorner
         << " (off " << offCorner << ")");

    // L'assertion qui mord : de la lumière là où la retombée de la lampe vaut EXACTEMENT zéro.
    // MESURÉ : 10 (éteint) -> 40 (allumé), pendant que le coin reste à 10. La marge de +15 est donc
    // deux fois moindre que l'effet réel, et le plancher de bruit est mesuré à 0 par le contrôle de
    // localité ci-dessous — ce n'est pas une marge choisie pour passer.
    CHECK(onSample > offSample + 15);

    // ---- 4. La LOCALITÉ : le coin ne bouge pas ---------------------------------------------
    // À ~90 px du centre, donc hors de portée d'un flou dont les taps atteignent 40 px depuis le bord
    // du disque (30 + 40 = 70). Un shader qui ajouterait une constante, ou qui échantillonnerait la
    // frame composée au lieu de la lueur floutée, éclaircirait ce coin aussi.
    CHECK(std::abs(onCorner - offCorner) <= 3);

    renderer->shutdown();
    mgr.removeInstance("bl_r");
    mgr.removeInstance("bl_g");
    dev = nullptr;
    SDL_DestroyWindow(win);
    SDL_Quit();
}
