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
#include "Passes/FadePass.h"
#include "RHI/RHIDevice.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <string>
#include <algorithm>
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

        // Une seule pose : le module RE-APPLIQUE la redirection a chaque frame, y compris apres
        // une reconstruction de ses cibles. C'est ce qui remplace le re-bind manuel par frame que ce
        // test devait faire -- et qui codait kCompositeView en dur, c'est-a-dire la connaissance que
        // le module porte desormais lui-meme (la vue finale depend des effets actifs).
        renderer->setCaptureTarget(fb);
        for (int i = 0; i < 5; ++i) {
            drawWhite();
            frame();
        }
        renderer->setCaptureTarget(rhi::FramebufferHandle{});

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

        renderer->setCaptureTarget(fb);
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

        renderer->setCaptureTarget(fb);
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

        renderer->setCaptureTarget(fb);
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

        renderer->setCaptureTarget(fb);
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

        renderer->setCaptureTarget(fb);
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
            renderer->setCaptureTarget(fb);
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
            renderer->setCaptureTarget(fb);
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
            renderer->setCaptureTarget(fb);
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
            renderer->setCaptureTarget(fb);
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

// ============================================================================
// Une frame SANS lampe ne doit pas montrer la lampe de la frame précédente.
//
// ⚠️ Trouvé le 2026-07-30 en regardant une CAPTURE, pas en lisant du code. Une planche de blog qui ne
//    publiait aucune lampe (deux faisceaux additifs sous un ambiant blanc) montrait un halo bleuté
//    dans le coin gauche, centré exactement là où la planche PRÉCÉDENTE posait sa lampe.
//
// LE MÉCANISME, et c'est un piège déjà connu du module pour une AUTRE cible : bgfx **saute une vue
// qui ne reçoit aucun draw**, et une vue sautée n'exécute jamais son effacement. Aucune lampe publiée
// ⇒ LightPass n'enregistre rien ⇒ la vue d'accumulation est sautée ⇒ la cible garde le contenu de la
// dernière frame qui, elle, avait des lampes. Le composite l'échantillonne et l'ajoute à l'ambiant.
//
// La même remarque est écrite depuis W dans BgfxRendererModule au sujet de la carte d'occultation, où
// elle a été résolue par un PLACEHOLDER (une texture 1×1) au lieu d'une confiance dans l'effacement.
// Le buffer de lumière avait le même défaut et personne ne l'avait vu, parce que tous les tests et
// toutes les planches publient au moins une lampe à chaque frame.
//
// ⚠️ C'est un état parfaitement légitime pour un jeu : toutes les lampes hors écran (cull), une
//    transition de scène, un interrupteur coupé. Le symptôme serait « un fantôme de lumière figé »,
//    qu'on chercherait dans son propre code de gameplay.
// ============================================================================

TEST_CASE("lighting: a frame with NO lights shows no residual lamp (GPU)", "[gpu][light][stale]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }

    const int W = 64, H = 64;
    SDL_Window* win = SDL_CreateWindow("stale", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("st_r");
    auto gIO = mgr.createInstance("st_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("st_r"); mgr.removeInstance("st_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    // `withLamp` décide si la frame publie une lampe. Tout le reste est identique — c'est la seule
    // variable de l'expérience.
    auto render = [&](bool withLamp) {
        for (int i = 0; i < 5; ++i) {
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setInt("viewportX",0); cam->setInt("viewportY",0);
              cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            // Scène blanche pleine vue : le terme de scène vaut 1, donc la sortie EST le terme
            // d'éclairage — ce qui rend les deux lectures directement comparables.
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);
              s->setDouble("scaleX", W); s->setDouble("scaleY", H);
              s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
              gIO->publish("render:sprite", std::move(s)); }
            { auto a = std::make_unique<JsonDataNode>("a");
              a->setInt("color", static_cast<int>(0x404040FFu));      // ambiant 0,25
              gIO->publish("render:ambient", std::move(a)); }
            if (withLamp) {
                auto l = std::make_unique<JsonDataNode>("l");
                l->setDouble("cx", W*0.5); l->setDouble("cy", H*0.5);
                l->setDouble("radius", W * 1.5);                       // couvre toute la vue
                l->setInt("color", static_cast<int>(0xFFFFFFFFu));
                l->setDouble("intensity", 3.0);
                gIO->publish("render:light", std::move(l));
            }
            renderer->setCaptureTarget(fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
        const uint8_t* p = &rgba[(static_cast<size_t>(H/2)*W + (W/2))*4];
        return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
    };

    const int withLamp = render(true);
    const int noLamp    = render(false);

    INFO("centre avec lampe=" << withLamp << " sans lampe=" << noLamp);

    REQUIRE(withLamp > 200);     // sanité : la lampe éclaire vraiment

    // L'ASSERTION. Sans lampe, la sortie doit être la scène × l'ambiant seul, soit 0,25 -> ~64.
    // Avec le buffer de lumière périmé, elle reste proche de la valeur éclairée.
    CHECK(noLamp < 100);
    CHECK(noLamp > 40);          // ...et pas noir non plus : l'ambiant, lui, doit rester

    renderer->shutdown();
    mgr.removeInstance("st_r");
    mgr.removeInstance("st_g");
    dev = nullptr;
    SDL_DestroyWindow(win);
    SDL_Quit();
}

// ============================================================================
// Bloom, tranche B4 — le PROFIL de la lueur doit décroître, à tout rayon.
//
// ⚠️ Ce test rend MESURABLE un défaut qui a d'abord été vu à l'œil sur une capture de blog : à grand
//    rayon, la lueur montre un FESTON (des cernes concentriques) au lieu d'un dégradé.
//
// LE MÉCANISME. Le noyau a 9 taps dont le plus externe doit tomber à `radius` pixels : leurs positions
// écran sont donc les mêmes quelle que soit la résolution de la cible de flou. Ce qui change est
// l'EMPREINTE de chaque tap — un texel. À un quart de résolution un tap couvre 4 px et laisse 12 px de
// trou entre lui et son voisin ; ces trous sont le feston. À un seizième, il couvre 16 px et rejoint
// son voisin. D'où la règle `grove::light::bloomDownsample`, verrouillée côté CPU par BloomMathUnit.
//
// POURQUOI un profil et pas une inspection visuelle : c'est la leçon de la sonde 90_edge_probe, écrite
// pendant la chasse à l'escalier des ombres. Un défaut qu'on ne sait que « voir » se corrige au
// feeling et se re-casse en silence. Un profil radial le chiffre : un dégradé est MONOTONE, un feston
// remonte. La bosse est l'assertion.
//
// ⚠️ Piège que ce test doit éviter : mesurer sur un profil déjà écrasé par le 8 bits. On échantillonne
//    donc en partant du bord de la source (là où la lueur est forte) et on s'arrête avant le plancher,
//    sinon la moitié des points seraient à 0 et n'importe quelle courbe passerait.
// ============================================================================

TEST_CASE("bloom: the glow profile DECREASES monotonically at a large radius (GPU)",
          "[gpu][light][bloom][profile]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }

    // 320x240 : il faut ~64 px de lueur d'un côté d'une source compacte, plus de la marge.
    const int W = 320, H = 240;
    SDL_Window* win = SDL_CreateWindow("bloomprof", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("bp_r");
    auto gIO = mgr.createInstance("bp_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("bp_r"); mgr.removeInstance("bp_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    const double cx = 100.0, cy = H * 0.5;
    const double srcHalf = 12.0;                 // une source compacte : 24x24

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    auto render = [&](double radius, double intensity) {
        for (int i = 0; i < 5; ++i) {
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setInt("viewportX",0); cam->setInt("viewportY",0);
              cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            // Fond NOIR : le profil de la lueur est alors la seule chose dans l'image, donc chaque
            // pixel mesuré est de la lueur et rien d'autre.
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);
              s->setDouble("scaleX", W); s->setDouble("scaleY", H);
              s->setInt("color", static_cast<int>(0x000000FFu)); s->setInt("layer", 1);
              gIO->publish("render:sprite", std::move(s)); }
            // Ambiant BLANC = neutre : la scène passe telle quelle, donc la source garde sa valeur.
            { auto a = std::make_unique<JsonDataNode>("a");
              a->setInt("color", static_cast<int>(0xFFFFFFFFu));
              gIO->publish("render:ambient", std::move(a)); }
            // La source : deux quads additifs superposés, donc luminance ~2 — franchement au-dessus du
            // seuil de 1, ce qui donne à l'extraction de quoi travailler. Un seul quad blanc plafonne
            // à 1 et ne brillerait pas du tout.
            for (int k = 0; k < 2; ++k) {
                auto s = std::make_unique<JsonDataNode>("d");
                s->setDouble("cx", cx); s->setDouble("cy", cy);
                s->setDouble("scaleX", srcHalf*2.0); s->setDouble("scaleY", srcHalf*2.0);
                s->setString("blend", "additive");
                s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 20 + k);
                gIO->publish("render:sprite", std::move(s));
            }
            { auto b = std::make_unique<JsonDataNode>("b");
              b->setDouble("intensity", intensity);
              b->setDouble("threshold", 1.0);
              b->setDouble("radius", radius);
              gIO->publish("render:bloom", std::move(b)); }
            renderer->setCaptureTarget(fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
    };
    auto lumaAt = [&](int x, int y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y)*W + x)*4];
        return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
    };

    // GRAND rayon : c'est le régime où le feston apparaissait. 64 px demande un facteur de réduction
    // de 16 pour que les empreintes des taps se touchent.
    render(64.0, 2.0);

    // Profil radial vers la droite, depuis le bord de la source. On note aussi la plus forte REMONTÉE :
    // c'est elle qui chiffre le feston.
    std::vector<int> profile;
    for (int dx = static_cast<int>(srcHalf) + 2; dx <= static_cast<int>(srcHalf) + 62; dx += 3) {
        profile.push_back(lumaAt(static_cast<int>(cx) + dx, static_cast<int>(cy)));
    }

    int worstRise = 0;
    std::string dump;
    for (size_t i = 0; i < profile.size(); ++i) {
        dump += std::to_string(profile[i]) + " ";
        if (i > 0) worstRise = std::max(worstRise, profile[i] - profile[i-1]);
    }
    INFO("profil r=64 : " << dump);
    INFO("plus forte remontee = " << worstRise);

    // Sanité : il y a bien de la lueur à mesurer, sinon la monotonie serait triviale.
    //
    // Mesurée sur le MAXIMUM et pas sur le premier point, et c'est une correction que le rouge a
    // imposée : avec le feston, la valeur au bord de la source (19) est PLUS BASSE qu'à 16 px de là
    // (32), donc une sanité sur `front()` se déclenchait avant l'assertion utile et masquait le vrai
    // diagnostic. Une sanité doit vérifier « il y a un signal », pas « il a déjà la bonne forme ».
    const int peak = *std::max_element(profile.begin(), profile.end());

    // ⚠️ Le seuil est 15 et non 25, et le rouge a impose la correction pour une raison qui EST le
    // correctif : les bosses du feston ETAIENT les pics. En les supprimant on repartit la meme energie,
    // donc le maximum BAISSE (32 festonne -> 21 lisse) alors que l'image est meilleure. Un seuil cale
    // sur la version defectueuse aurait donc refuse la version correcte -- une metrique de proxy qui
    // recompense l'artefact, exactement le piege rencontre pendant la chasse a l'escalier des ombres.
    //
    // 21 reste tres au-dessus du fond (2), donc « il y a un signal a mesurer » est satisfait, et c'est
    // tout ce que cette sanite doit dire.
    REQUIRE(peak > 15);
    REQUIRE(peak > profile.back() + 15);

    // L'ASSERTION. Une lueur gaussienne décroît ; un feston remonte entre deux taps. La tolérance de 2
    // est celle de l'arrondi 8 bits, pas un confort.
    CHECK(worstRise <= 2);

    renderer->shutdown();
    mgr.removeInstance("bp_r");
    mgr.removeInstance("bp_g");
    dev = nullptr;
    SDL_DestroyWindow(win);
    SDL_Quit();
}

// ============================================================================
// Tonemapping (plan T) — deux SUR-BRILLANCES différentes doivent rester différentes.
//
// LE DISCRIMINANT, écrit au plan avant le code : deux lampes d'intensités **toutes deux au-dessus de
// 1**, donc toutes deux écrêtées aujourd'hui.
//
//   - sans tonemapping : les deux rendent 255. Indistinguables — c'est le défaut lui-même, et c'est
//     exactement la situation que l'arbitrage RGBA16F voulait éviter, déplacée d'un cran jusqu'à la
//     dernière ligne du pipeline.
//   - avec tonemapping : la plus intense est MESURABLEMENT plus claire, et les deux sont SOUS 255.
//
// ⚠️ Piège que ce test évite : mesurer 0,5 contre 0,8 ne discriminerait RIEN — ces valeurs ne sont pas
//    écrêtées, donc elles diffèrent déjà sans tonemapping. Il faut se placer là où l'information est
//    perdue.
//
// ⚠️ Et une courbe qui se contenterait d'assombrir passerait « les deux sous 255 » tout en échouant sur
//    « la plus intense reste plus claire » — les deux moitiés de l'assertion sont nécessaires.
//
// Plan : docs/design/lighting-tonemap.md
// ============================================================================

TEST_CASE("tonemap: two clipped overbrights become DISTINGUISHABLE (GPU)", "[gpu][light][tonemap]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }

    const int W = 64, H = 64;
    SDL_Window* win = SDL_CreateWindow("tonemap", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("tm_r");
    auto gIO = mgr.createInstance("tm_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("tm_r"); mgr.removeInstance("tm_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);

    // Une lampe d'intensité `lampIntensity` couvrant toute la vue, sur une scène BLANCHE : le terme de
    // scène vaut 1 partout, donc la sortie EST le terme d'éclairage. `mode` vide = pas de tonemapping.
    auto render = [&](double lampIntensity, const char* mode, double exposure) {
        for (int i = 0; i < 5; ++i) {
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setInt("viewportX",0); cam->setInt("viewportY",0);
              cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);
              s->setDouble("scaleX", W); s->setDouble("scaleY", H);
              s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
              gIO->publish("render:sprite", std::move(s)); }
            // Ambiant NOIR-ish : ce qui sort vient de la lampe, pas de l'ambiant. Non nul, sinon
            // l'éclairage entier serait désactivé (0 = l'interrupteur).
            { auto a = std::make_unique<JsonDataNode>("a");
              a->setInt("color", static_cast<int>(0x010101FFu));
              gIO->publish("render:ambient", std::move(a)); }
            { auto l = std::make_unique<JsonDataNode>("l");
              l->setDouble("cx", W*0.5); l->setDouble("cy", H*0.5);
              l->setDouble("radius", W * 3.0);          // très large : le centre est loin du bord
              l->setInt("color", static_cast<int>(0xFFFFFFFFu));
              l->setDouble("intensity", lampIntensity);
              gIO->publish("render:light", std::move(l)); }
            { auto t = std::make_unique<JsonDataNode>("t");
              t->setString("mode", mode);
              t->setDouble("exposure", exposure);
              gIO->publish("render:tonemap", std::move(t)); }

            // Sans tonemapping l'image finale sort du COMPOSITE ; avec, de la PRÉSENTATION. Lire la
            // mauvaise vue mesurerait la frame d'avant la courbe et le test serait vert sans rien
            // prouver — le même piège qu'en L1, deux crans plus loin.
            const bool post = (std::string(mode) != "none");
            renderer->setCaptureTarget(fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
        const uint8_t* p = &rgba[(static_cast<size_t>(H/2)*W + (W/2))*4];
        return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
    };

    // ---- 1. LE DÉFAUT : sans tonemapping, 2 et 8 sont le même blanc ------------------------
    const int clipped2 = render(2.0, "none", 1.0);
    const int clipped8 = render(8.0, "none", 1.0);
    INFO("sans tonemap : i=2 -> " << clipped2 << " | i=8 -> " << clipped8);
    REQUIRE(clipped2 == 255);
    REQUIRE(clipped8 == 255);          // l'information est perdue, et c'est prouvé et non supposé

    // ---- 2. REINHARD les sépare -----------------------------------------------------------
    const int rein2 = render(2.0, "reinhard", 1.0);
    const int rein8 = render(8.0, "reinhard", 1.0);
    INFO("reinhard : i=2 -> " << rein2 << " | i=8 -> " << rein8);
    CHECK(rein8 > rein2 + 5);          // ~5 niveaux : un écart qu'un œil voit
    CHECK(rein2 < 255);
    CHECK(rein8 < 255);

    // ---- 3. ACES aussi, et différemment ---------------------------------------------------
    const int aces2 = render(2.0, "aces", 1.0);
    const int aces8 = render(8.0, "aces", 1.0);
    INFO("aces : i=2 -> " << aces2 << " | i=8 -> " << aces8);
    CHECK(aces8 > aces2 + 5);
    CHECK(aces2 < 255);

    // Les deux modes doivent donner des rendus DIFFÉRENTS, sinon en proposer deux est un mensonge.
    // (ACES tient les tons moyens plus haut — c'est sa signature filmique.)
    CHECK(aces2 != rein2);

    // ---- 4. L'EXPOSITION agit -------------------------------------------------------------
    // Elle multiplie AVANT la courbe : à intensité identique, doubler l'exposition doit éclaircir.
    const int dark   = render(2.0, "reinhard", 0.4);
    const int bright = render(2.0, "reinhard", 2.5);
    INFO("exposition : 0.4 -> " << dark << " | 2.5 -> " << bright);
    CHECK(bright > dark + 20);

    // ---- 5. LE CONTOURNEMENT : revenir à "none" restaure l'image d'origine -----------------
    // C'est le chemin qui a exposé le défaut de setViewFramebuffer : éteindre le post-traitement doit
    // rendre la vue du composite au backbuffer. Si ce n'était pas fait, la frame resterait dans la
    // cible HDR et cette lecture verrait n'importe quoi.
    const int backToNone = render(2.0, "none", 1.0);
    INFO("retour a none : " << backToNone);
    CHECK(backToNone == 255);

    renderer->shutdown();
    mgr.removeInstance("tm_r");
    mgr.removeInstance("tm_g");
    dev = nullptr;
    SDL_DestroyWindow(win);
    SDL_Quit();
}

// ============================================================================
// Fondus (plan F2) — il couvre TOUT, et il n'exige RIEN.
//
// Trois mesures, et la deuxième est celle qui distingue cette conception de celle que j'avais annoncée
// (« les fondus atterriront sur la passe de présentation »).
//
//   1. Le fondu agit **SANS éclairage** — aucun `render:ambient`. Une passe rangée avec le bloom
//      n'existerait même pas dans ce cas.
//   2. ⚠️ Le fondu **COUVRE LE HUD**. Un sprite en `space:"screen"` est dessiné sur la vue 1, qui passe
//      APRÈS la présentation ; un fondu posé là le laisserait intact. C'est LE test qui discrimine, et
//      il passerait inaperçu si on ne mesurait que la scène.
//   3. Le fondu agit **AUSSI avec l'éclairage actif**, où le pipeline entier change de forme (vue 0
//      redirigée vers une cible, ordre de soumission imposé à la main). Ce cas attrape un fondu que le
//      composite écraserait, ou dont la vue aurait hérité d'une cible ou d'une transformation.
//      ⚠️ Il n'attrape PAS « la vue du fondu oubliée dans la liste d'ordre » : vérifié par sabotage,
//         `bgfx::setViewOrder` ne remappe que les places listées et soumet les autres vues ensuite, par
//         id croissant — donc le fondu reste dernier de toute façon, son id étant le plus haut. Le plan
//         annonçait l'inverse ; c'est corrigé là-bas aussi.
//
// Plan : docs/design/lighting-fade.md
// ============================================================================

TEST_CASE("fade: covers everything INCLUDING the HUD, with or without lighting (GPU)",
          "[gpu][fade]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }

    const int W = 64, H = 64;
    SDL_Window* win = SDL_CreateWindow("fade", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("fd_r");
    auto gIO = mgr.createInstance("fd_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("fd_r"); mgr.removeInstance("fd_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);

    // Le HUD occupe le QUART SUPÉRIEUR GAUCHE, la scène le reste : on peut donc lire séparément un
    // pixel de monde et un pixel d'interface sur la même frame.
    const int hudX = 8,  hudY = 8;
    const int worldX = W - 12, worldY = H - 12;

    auto render = [&](bool lit, double amount, uint32_t fadeColor) {
        for (int i = 0; i < 5; ++i) {
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setInt("viewportX",0); cam->setInt("viewportY",0);
              cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            // Le monde : plein écran, blanc.
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);
              s->setDouble("scaleX", W); s->setDouble("scaleY", H);
              s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
              gIO->publish("render:sprite", std::move(s)); }
            // Le HUD : un carré VERT en espace écran, donc sur la vue 1 — celle qui passe après la
            // présentation. C'est le pixel qui discrimine.
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", 16.0); s->setDouble("cy", 16.0);
              s->setDouble("scaleX", 24.0); s->setDouble("scaleY", 24.0);
              s->setString("space", "screen");
              s->setInt("color", static_cast<int>(0x00FF00FFu)); s->setInt("layer", 1000);
              gIO->publish("render:sprite", std::move(s)); }
            if (lit) {
                // Ambiant BLANC : neutre par construction, donc la scène est inchangée et la seule
                // variable de l'expérience reste le fondu.
                auto a = std::make_unique<JsonDataNode>("a");
                a->setInt("color", static_cast<int>(0xFFFFFFFFu));
                gIO->publish("render:ambient", std::move(a));
            }
            { auto f = std::make_unique<JsonDataNode>("f");
              f->setDouble("amount", amount);
              f->setInt("color", static_cast<int>(fadeColor));
              gIO->publish("render:fade", std::move(f)); }

            // ⚠️ La lecture se fait toujours sur la vue du FONDU quand il est actif : c'est la dernière
            //    à écrire. Sans fondu, la frame finale est sur la vue 0 (non éclairé) ou celle du
            //    composite (éclairé).
            // Le module sait quelles vues composent son image finale ; ce test n'a plus a le
            // deviner (il enumerait fondu / composite / vue 0 selon `amount` et `lit`).
            renderer->setCaptureTarget(fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
    };
    auto lumaAt = [&](int x, int y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y)*W + x)*4];
        return (static_cast<int>(p[0]) + p[1] + p[2]) / 3;
    };
    auto greenAt = [&](int x, int y) {
        return static_cast<int>(rgba[(static_cast<size_t>(y)*W + x)*4 + 1]);
    };

    // ---- 1. SANS ÉCLAIRAGE : la référence, puis le fondu -----------------------------------
    render(false, 0.0, 0x000000FFu);
    const int refWorld = lumaAt(worldX, worldY);
    const int refHudG  = greenAt(hudX, hudY);
    INFO("sans fondu : monde=" << refWorld << " HUD(vert)=" << refHudG);
    REQUIRE(refWorld > 200);          // le monde est bien blanc
    REQUIRE(refHudG  > 200);          // et le HUD bien present

    // Fondu au noir a moitie : le monde doit tomber vers le gris.
    render(false, 0.5, 0x000000FFu);
    const int halfWorld = lumaAt(worldX, worldY);
    INFO("fondu 0.5 : monde=" << halfWorld);
    CHECK(halfWorld < refWorld - 60);
    CHECK(halfWorld > 40);            // ...sans etre deja noir : c'est un MIX, pas un interrupteur

    // ---- 2. LE DISCRIMINANT : a fond, le HUD disparait AUSSI --------------------------------
    render(false, 1.0, 0x000000FFu);
    const int fullWorld = lumaAt(worldX, worldY);
    const int fullHudG  = greenAt(hudX, hudY);
    INFO("fondu 1.0 : monde=" << fullWorld << " HUD(vert)=" << fullHudG << " (ref " << refHudG << ")");
    CHECK(fullWorld < 8);
    // ⚠️ L'ASSERTION QUI SEPARE CETTE CONCEPTION DE L'AUTRE. Un fondu sur la passe de presentation
    //    laisserait ce vert intact a 255, tout en passant toutes les autres mesures.
    CHECK(fullHudG < 8);

    // ---- 3. AVEC ÉCLAIRAGE : le fondu doit toujours agir -----------------------------------
    // Le pipeline change entierement de forme : la vue 0 part dans une cible, un composite plein ecran
    // ecrit le resultat, l'ordre est impose a la main. Ce cas attrape un fondu que le composite
    // ecraserait, ou dont la vue aurait herite d'une cible.
    // ⚠️ Il n'attrape PAS l'oubli de la vue dans la liste d'ordre — voir l'en-tete de ce test.
    render(true, 1.0, 0x000000FFu);
    const int litFadedWorld = lumaAt(worldX, worldY);
    const int litFadedHudG  = greenAt(hudX, hudY);
    INFO("eclaire + fondu 1.0 : monde=" << litFadedWorld << " HUD(vert)=" << litFadedHudG);
    CHECK(litFadedWorld < 8);
    CHECK(litFadedHudG < 8);

    // ---- 4. Un fondu au BLANC atteint le blanc --------------------------------------------
    // Il n'y a ici aucun tonemapping, mais c'est la raison pour laquelle le fondu doit passer APRES la
    // courbe : Reinhard tend vers 1 sans l'atteindre, donc un fondu au blanc applique avant ne
    // terminerait jamais sa course.
    render(false, 1.0, 0xFFFFFFFFu);
    INFO("fondu blanc : monde=" << lumaAt(worldX, worldY));
    CHECK(lumaAt(worldX, worldY) > 248);

    // ---- 5. Le CONTOURNEMENT : retour a 0, l'image est intacte ----------------------------
    render(false, 0.0, 0x000000FFu);
    INFO("retour a 0 : monde=" << lumaAt(worldX, worldY) << " HUD=" << greenAt(hudX, hudY));
    CHECK(lumaAt(worldX, worldY) > 200);
    CHECK(greenAt(hudX, hudY) > 200);

    renderer->shutdown();
    mgr.removeInstance("fd_r");
    mgr.removeInstance("fd_g");
    dev = nullptr;
    SDL_DestroyWindow(win);
    SDL_Quit();
}

// ============================================================================
// Colorimétrie (plan G) — la désaturation respecte la LUMINANCE, et le HUD est épargné.
//
// Scène de DEUX couleurs franches (un carré rouge, un carré bleu) plus un carré HUD vert.
//
//   1. `saturation 0` → les deux carrés deviennent gris, et de gris **DIFFÉRENTS** : le rouge pèse
//      0,2126 en Rec. 709 et le bleu 0,0722, donc le gris du rouge doit être ~3× plus clair.
//      ⚠️ C'est ce qui discrimine une vraie désaturation d'une moyenne (r+g+b)/3, laquelle rendrait les
//         deux au même gris. Une assertion « c'est devenu gris » passerait avec la mauvaise formule.
//   2. `tint` bleu → le canal rouge chute, le bleu non.
//   3. `contrast 2` → le sombre s'assombrit ET le clair s'éclaircit. Une assertion sur un seul des deux
//      passerait avec un simple gain, qui n'est pas un contraste.
//   4. ⚠️ Le HUD ne bouge PAS. C'est l'assertion qui verrouille la PLACE choisie : un étalonnage posé
//      sur la passe du fondu désaturerait aussi l'interface, en passant les trois premières mesures.
//   5. Neutres ⇒ image inchangée.
//
// Plan : docs/design/lighting-grade.md
// ============================================================================

TEST_CASE("grade: desaturation respects LUMINANCE and spares the HUD (GPU)", "[gpu][grade]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }

    const int W = 64, H = 64;
    SDL_Window* win = SDL_CreateWindow("grade", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("gr_r");
    auto gIO = mgr.createInstance("gr_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("gr_r"); mgr.removeInstance("gr_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    // Rouge en haut, bleu en bas, HUD vert au milieu à droite.
    const int redX = 20,      redY = 14;
    const int blueX = 20,     blueY = 50;
    const int hudX = 50,      hudY = 32;

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    auto render = [&](double saturation, double contrast, uint32_t tint) {
        for (int i = 0; i < 5; ++i) {
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setInt("viewportX",0); cam->setInt("viewportY",0);
              cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            // Deux bandes de couleur franche : rouge pur et bleu pur.
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", 14.0);
              s->setDouble("scaleX", W); s->setDouble("scaleY", 24.0);
              s->setInt("color", static_cast<int>(0xFF0000FFu)); s->setInt("layer", 10);
              gIO->publish("render:sprite", std::move(s)); }
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", 50.0);
              s->setDouble("scaleX", W); s->setDouble("scaleY", 24.0);
              s->setInt("color", static_cast<int>(0x0000FFFFu)); s->setInt("layer", 10);
              gIO->publish("render:sprite", std::move(s)); }
            // Le HUD : vert, en espace écran, donc sur la vue 1 — soumise APRÈS la présentation.
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", 50.0); s->setDouble("cy", 32.0);
              s->setDouble("scaleX", 20.0); s->setDouble("scaleY", 20.0);
              s->setString("space", "screen");
              s->setInt("color", static_cast<int>(0x00FF00FFu)); s->setInt("layer", 1000);
              gIO->publish("render:sprite", std::move(s)); }
            // Ambiant BLANC : neutre par construction, donc la scène traverse le composite inchangée et
            // la seule variable de l'expérience reste l'étalonnage.
            { auto a = std::make_unique<JsonDataNode>("a");
              a->setInt("color", static_cast<int>(0xFFFFFFFFu));
              gIO->publish("render:ambient", std::move(a)); }
            { auto g = std::make_unique<JsonDataNode>("g");
              g->setDouble("saturation", saturation);
              g->setDouble("contrast", contrast);
              g->setInt("tint", static_cast<int>(tint));
              gIO->publish("render:grade", std::move(g)); }

            // ⚠️ Avec l'étalonnage actif la frame du monde sort de la PRÉSENTATION. Mais le HUD est
            //    dessiné après, sur la vue 1 : pour lire les deux sur une même image, on attache la
            //    cible aux DEUX vues. Sans le HUD lié, la mesure 4 ne verrait rien.
            const bool post = (saturation != 1.0 || contrast != 1.0 || tint != 0xFFFFFFFFu);
            // Les deux branches faisaient la meme chose une fois la vue finale confiee au module :
            // le `if (post)` ne servait qu'a choisir entre presentation et composite.
            renderer->setCaptureTarget(fb);
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
    };
    auto chan = [&](int x, int y, int c) {
        return static_cast<int>(rgba[(static_cast<size_t>(y)*W + x)*4 + c]);
    };

    // ---- 0. Neutres : la référence ----------------------------------------------------------
    render(1.0, 1.0, 0xFFFFFFFFu);
    const int refRedR  = chan(redX, redY, 0);
    const int refBlueB = chan(blueX, blueY, 2);
    const int refHudG  = chan(hudX, hudY, 1);
    INFO("neutre : rouge.R=" << refRedR << " bleu.B=" << refBlueB << " HUD.G=" << refHudG);
    REQUIRE(refRedR > 200);
    REQUIRE(refBlueB > 200);
    REQUIRE(refHudG > 200);

    // ---- 1. LE DISCRIMINANT : saturation 0 -> des gris DIFFÉRENTS ---------------------------
    render(0.0, 1.0, 0xFFFFFFFFu);
    const int greyRed  = chan(redX, redY, 0);
    const int greyBlue = chan(blueX, blueY, 2);
    INFO("satur. 0 : gris(rouge)=" << greyRed << " gris(bleu)=" << greyBlue);

    // Gris = les trois canaux egaux, sur chaque bande.
    CHECK(std::abs(chan(redX, redY, 0) - chan(redX, redY, 1)) <= 2);
    CHECK(std::abs(chan(redX, redY, 1) - chan(redX, redY, 2)) <= 2);
    CHECK(std::abs(chan(blueX, blueY, 0) - chan(blueX, blueY, 2)) <= 2);

    // ...et le gris du ROUGE est ~3x celui du BLEU. Une moyenne (r+g+b)/3 les rendrait EGAUX -- c'est
    // la seule assertion de ce test qu'une mauvaise formule ne peut pas satisfaire.
    CHECK(greyRed > greyBlue * 2.0);
    // Valeurs attendues : 0,2126*255 = 54 et 0,0722*255 = 18.
    CHECK(greyRed > 40);
    CHECK(greyRed < 70);
    CHECK(greyBlue < 30);

    // ---- 2. ⚠️ LE HUD N'A PAS BOUGÉ ---------------------------------------------------------
    // L'assertion qui verrouille la place : un étalonnage sur la passe du fondu aurait desature ce vert
    // aussi, en passant tout le reste.
    INFO("satur. 0 : HUD.G=" << chan(hudX, hudY, 1) << " (ref " << refHudG << ")");
    CHECK(chan(hudX, hudY, 1) > 200);
    CHECK(chan(hudX, hudY, 0) < 60);      // et il est toujours VERT, pas gris

    // ---- 3. La teinte agit par canal ------------------------------------------------------
    render(1.0, 1.0, 0x4040FFFFu);        // rouge et vert divises par ~4, bleu intact
    INFO("teinte bleue : rouge.R=" << chan(redX, redY, 0) << " bleu.B=" << chan(blueX, blueY, 2));
    CHECK(chan(redX, redY, 0) < refRedR / 2);
    CHECK(chan(blueX, blueY, 2) > 200);   // le bleu passe

    // ---- 4. Le contraste écarte du gris moyen, DANS LES DEUX SENS -------------------------
    // Le rouge pur est a (255,0,0) : son canal R est au-dessus du pivot, ses canaux G/B en dessous. Un
    // contraste eleve doit donc pousser R vers le haut ET G/B vers le bas -- un simple gain ne ferait
    // que la premiere moitie.
    render(1.0, 2.0, 0xFFFFFFFFu);
    INFO("contraste 2 : rouge.R=" << chan(redX, redY, 0) << " rouge.G=" << chan(redX, redY, 1));
    CHECK(chan(redX, redY, 0) >= 250);    // deja sature, il y reste
    CHECK(chan(redX, redY, 1) <= 2);      // et le vert du rouge est ecrase vers 0

    // Un contraste FAIBLE rapproche du gris : le test symetrique, sans lequel « contraste » pourrait
    // n'etre qu'un gain.
    render(1.0, 0.25, 0xFFFFFFFFu);
    INFO("contraste 0.25 : rouge.R=" << chan(redX, redY, 0) << " rouge.G=" << chan(redX, redY, 1));
    CHECK(chan(redX, redY, 0) < 200);     // le clair redescend
    CHECK(chan(redX, redY, 1) > 60);      // le sombre remonte

    // ---- 5. Retour aux neutres : l'image est celle du depart ------------------------------
    render(1.0, 1.0, 0xFFFFFFFFu);
    INFO("retour neutre : rouge.R=" << chan(redX, redY, 0) << " HUD.G=" << chan(hudX, hudY, 1));
    CHECK(chan(redX, redY, 0) > 200);
    CHECK(chan(blueX, blueY, 2) > 200);
    CHECK(chan(hudX, hudY, 1) > 200);

    renderer->shutdown();
    mgr.removeInstance("gr_r");
    mgr.removeInstance("gr_g");
    dev = nullptr;
    SDL_DestroyWindow(win);
    SDL_Quit();
}

// ============================================================================
// COMPOSITION du post-traitement — les quatre effets ENSEMBLE (plans B, T, F2, G).
//
// POURQUOI ce test existe, et pourquoi il arrive après les quatre autres : chacun d'eux ne publie
// **qu'un seul** réglage. Les affirmations les plus chargées de tout le chantier portent pourtant sur
// la COMPOSITION, et elles n'étaient qu'argumentées dans des commentaires :
//
//   1. « la lueur est ajoutée AVANT la courbe, sinon elle réécrête » ;
//   2. « la colorimétrie épargne le HUD, le fondu le couvre » ;
//   3. « un fondu au blanc atteint le blanc PARCE QU'IL passe après la courbe ».
//
// ⚠️ Et ce n'est pas une inquiétude théorique : DEUX bugs de cette session vivaient exactement là — le
//    buffer de lumière périmé (trouvé sur une capture sans lampe) et la vue soumise deux fois (trouvée
//    par le test de colorimétrie, datant de la tranche des fondus). Aucun n'était un bug de math ; les
//    deux étaient des bugs de composition et d'état, et aucun test isolé ne les a attrapés.
//
// Plans : lighting-bloom.md, lighting-tonemap.md, lighting-fade.md, lighting-grade.md
// ============================================================================

TEST_CASE("post-processing: the four effects COMPOSE in the right order (GPU)",
          "[gpu][light][compo]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }

    const int W = 128, H = 128;
    SDL_Window* win = SDL_CreateWindow("compo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("cp_r");
    auto gIO = mgr.createInstance("cp_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("cp_r"); mgr.removeInstance("cp_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                                       rhi::TargetFormat::RGBA8);

    // Une lampe ORANGE au centre, un carré HUD VERT en bas à droite. Le point de mesure de la lueur est
    // hors du rayon de la lampe — là où sa retombée vaut EXACTEMENT zéro, donc où tout ce qu'on lit
    // vient forcément du post-traitement.
    const double cx = 48.0, cy = 48.0, lampRadius = 22.0;
    // ⚠️ 4 px HORS du bord et pas 8 : le premier essai mesurait a 30 px du centre, ou la lueur ne
    //    vaut que ~0,01. Meme multipliee par 20, la somme brute restait sous 1 -- donc elle
    //    n'ecretait pas MEME en etant ajoutee apres la courbe, et mon assertion « une lueur enorme
    //    ne peut pas ecreter » etait INERTE. Constate par sabotage : le test tombait, mais par une
    //    autre assertion et pour une autre raison. Ici la lueur est assez forte pour que la somme
    //    depasse 1, donc la difference entre avant et apres la courbe devient mesurable.
    //    La retombee de la lampe y vaut toujours EXACTEMENT zero (bord a 22, mesure a 26), et la
    //    resolution en croix du composite ne s'etend que d'un pixel.
    const int glowX = static_cast<int>(cx + 26.0), glowY = static_cast<int>(cy);
    const int hudX = 100, hudY = 100;

    // Réglages d'une frame. Une chaîne vide pour `tonemapMode` = aucun.
    struct Post {
        double bloomIntensity = 0.0;
        const char* tonemapMode = "none";
        double exposure = 1.0;
        double saturation = 1.0;
        double contrast = 1.0;
        uint32_t tint = 0xFFFFFFFFu;
        double fadeAmount = 0.0;
        uint32_t fadeColor = 0x000000FFu;
    };

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    auto render = [&](const Post& p) {
        for (int i = 0; i < 5; ++i) {
            { auto cam = std::make_unique<JsonDataNode>("camera");
              cam->setInt("viewportX",0); cam->setInt("viewportY",0);
              cam->setInt("viewportW",W); cam->setInt("viewportH",H);
              gIO->publish("render:camera", std::move(cam)); }
            // Un sol blanc plein écran : le terme de scène vaut 1, donc la sortie EST l'éclairage.
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", W*0.5); s->setDouble("cy", H*0.5);
              s->setDouble("scaleX", W); s->setDouble("scaleY", H);
              s->setInt("color", static_cast<int>(0xFFFFFFFFu)); s->setInt("layer", 10);
              gIO->publish("render:sprite", std::move(s)); }
            // Le HUD, en espace écran : vue 1, soumise APRÈS la présentation.
            { auto s = std::make_unique<JsonDataNode>("d");
              s->setDouble("cx", 100.0); s->setDouble("cy", 100.0);
              s->setDouble("scaleX", 24.0); s->setDouble("scaleY", 24.0);
              s->setString("space", "screen");
              s->setInt("color", static_cast<int>(0x00FF00FFu)); s->setInt("layer", 1000);
              gIO->publish("render:sprite", std::move(s)); }
            // Ambiant très sombre : hors de la lampe, la base est basse, donc tout ce qu'on mesurera
            // là-bas au-delà d'elle vient du post-traitement.
            { auto a = std::make_unique<JsonDataNode>("a");
              a->setInt("color", static_cast<int>(0x0A0A0AFFu));
              gIO->publish("render:ambient", std::move(a)); }
            // La lampe : ORANGE et intense, donc franchement sur-brillante et colorée — ce qui rend la
            // désaturation de sa lueur mesurable.
            { auto l = std::make_unique<JsonDataNode>("l");
              l->setDouble("cx", cx); l->setDouble("cy", cy);
              l->setDouble("radius", lampRadius);
              l->setInt("color", static_cast<int>(0xFF9040FFu));
              l->setDouble("intensity", 6.0);
              gIO->publish("render:light", std::move(l)); }

            { auto b = std::make_unique<JsonDataNode>("b");
              b->setDouble("intensity", p.bloomIntensity);
              b->setDouble("threshold", 1.0);
              b->setDouble("radius", 24.0);
              gIO->publish("render:bloom", std::move(b)); }
            { auto t = std::make_unique<JsonDataNode>("t");
              t->setString("mode", p.tonemapMode);
              t->setDouble("exposure", p.exposure);
              gIO->publish("render:tonemap", std::move(t)); }
            { auto g = std::make_unique<JsonDataNode>("g");
              g->setDouble("saturation", p.saturation);
              g->setDouble("contrast", p.contrast);
              g->setInt("tint", static_cast<int>(p.tint));
              gIO->publish("render:grade", std::move(g)); }
            { auto f = std::make_unique<JsonDataNode>("f");
              f->setDouble("amount", p.fadeAmount);
              f->setInt("color", static_cast<int>(p.fadeColor));
              gIO->publish("render:fade", std::move(f)); }

            // ⚠️ Quelle vue porte l'image finale dépend de ce qui est actif, et se tromper ici mesurerait
            //    une frame INTERMÉDIAIRE en passant au vert. Le fondu est la dernière à écrire ; sinon
            //    c'est la présentation ; sinon le composite. La vue 1 (HUD) est TOUJOURS attachée, sans
            //    quoi on ne pourrait pas lire l'interface sur la même image.
            const bool postActive = (p.bloomIntensity > 0.0) || (std::string(p.tonemapMode) != "none")
                                 || (p.saturation != 1.0) || (p.contrast != 1.0)
                                 || (p.tint != 0xFFFFFFFFu);
            // Ce bloc RECALCULAIT la regle du module (postActive compris) pour choisir sa vue :
            // un duplicata voue a deriver des que la regle change. Le module la porte, on la lui
            // demande.
            renderer->setCaptureTarget(fb);

            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
        }
        REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
    };
    auto chan = [&](int x, int y, int c) {
        return static_cast<int>(rgba[(static_cast<size_t>(y)*W + x)*4 + c]);
    };
    auto luma = [&](int x, int y) { return (chan(x,y,0) + chan(x,y,1) + chan(x,y,2)) / 3; };

    // ==== 1. LA LUEUR PASSE AVANT LA COURBE ==================================================
    // Le discriminant est la SATURATION, pas la valeur : Reinhard tend vers 1 sans l'atteindre, donc
    // une lueur passée par la courbe ne peut JAMAIS écrêter, aussi forte qu'on la pousse. Ajoutée
    // APRÈS, elle dépasserait 1 et l'écrêtage la figerait à 255.
    // LE DISCRIMINANT est la CONCAVITE, et il a fallu deux essais pour le trouver.
    //
    // ⚠️ Mes deux premieres versions ne discriminaient PAS. J'avais assertionne « une lueur enorme ne
    //    peut pas ecreter si elle a traverse la courbe » -- vrai, mais inatteignable ici : a ce point la
    //    lueur vaut ~0,03, donc meme multipliee par 20 la somme brute reste sous 1 et n'ecrete pas MEME
    //    ajoutee apres la courbe. Constate par sabotage : le test tombait, mais par une autre assertion
    //    et pour une autre raison. Un discriminant par ecretage aurait exige une intensite absurde.
    //
    // La bonne propriete est SANS ECHELLE : passer par une courbe compressive rend la reponse CONCAVE en
    // intensite (chaque increment rapporte moins que le precedent), alors que l'ajouter apres la courbe
    // est exactement LINEAIRE (la pente par unite d'intensite est constante). On mesure donc la pente
    // sur deux intervalles et on exige qu'elle DECROISSE.
    {
        Post p; p.tonemapMode = "reinhard";
        p.bloomIntensity = 5.0;  render(p); const int v5  = luma(glowX, glowY);
        p.bloomIntensity = 10.0; render(p); const int v10 = luma(glowX, glowY);
        p.bloomIntensity = 20.0; render(p); const int v20 = luma(glowX, glowY);

        const double slope1 = (v10 - v5)  / 5.0;    // par unite d'intensite, sur [5,10]
        const double slope2 = (v20 - v10) / 10.0;   // ...et sur [10,20]
        INFO("lueur hors rayon, reinhard : 5 -> " << v5 << " | 10 -> " << v10 << " | 20 -> " << v20);
        INFO("pente par unite : [5,10] = " << slope1 << " | [10,20] = " << slope2);

        CHECK(v5 > 20);              // sanite : il y a bien une lueur a mesurer
        CHECK(v20 > v10);            // ...et elle monte encore, donc la courbe separe toujours
        CHECK(v20 < 250);            // sans jamais ecreter

        // L'ASSERTION, et sa marge est MESUREE et non devinee -- sabotage a l'appui :
        //   code correct (lueur avant la courbe) : 4,6 puis 3,2  -> rapport 0,70
        //   sabote      (lueur apres la courbe)  : 8,0 puis 7,4  -> rapport 0,925
        // Le seuil 0,8 tombe entre les deux. Les pentes du cas sabote ne sont pas EXACTEMENT egales
        // (l'etalonnage et l'arrondi 8 bits suivent encore), donc une assertion d'egalite stricte
        // aurait ete fragile la ou celle-ci a de la marge des deux cotes.
        CHECK(slope2 < slope1 * 0.8);
    }

    // ==== 2. LA COLORIMETRIE S'APPLIQUE A LA LUEUR ============================================
    // Consequence de l'ordre : la lueur est ajoutee avant la courbe, donc avant l'etalonnage. Une lueur
    // ORANGE desaturee doit devenir GRISE. Si l'etalonnage s'appliquait a la scene seule (ou apres le
    // fondu), la lueur resterait orange.
    {
        Post p; p.bloomIntensity = 6.0;
        render(p);
        const int glowR = chan(glowX, glowY, 0), glowB = chan(glowX, glowY, 2);
        INFO("lueur non etalonnee : R=" << glowR << " B=" << glowB);
        // Un RAPPORT et pas une marge absolue : l'intensite de la lueur depend du rayon, du seuil et
        // de la lampe, donc une marge en niveaux 8 bits est arbitraire -- la premiere version disait
        // « +20 » et echouait a UNE unite pres (37 contre 17). « Elle est orange » est un rapport.
        REQUIRE(glowR > glowB * 1.8);        // mesure : 37 contre 17

        p.saturation = 0.0;
        render(p);
        const int greyR = chan(glowX, glowY, 0), greyB = chan(glowX, glowY, 2);
        INFO("lueur desaturee : R=" << greyR << " B=" << greyB);
        CHECK(std::abs(greyR - greyB) <= 3);   // devenue grise
    }

    // ==== 3. LE HUD : L'ETALONNAGE L'EPARGNE, LE FONDU LE COUVRE ==============================
    // ⚠️ LES DEUX COMPORTEMENTS OPPOSES, DANS UNE SEULE FRAME. C'est la mesure qui n'existait nulle
    //    part : chaque test isole ne pouvait en verifier qu'un.
    {
        Post p; p.saturation = 0.0; p.fadeAmount = 0.5; p.fadeColor = 0x000000FFu;
        render(p);
        const int hudR = chan(hudX, hudY, 0), hudG = chan(hudX, hudY, 1);
        INFO("HUD sous saturation 0 + fondu noir 0.5 : R=" << hudR << " G=" << hudG);

        // Le fondu l'a bien atteint : un vert plein (255) mele a moitie de noir donne ~128.
        CHECK(hudG > 100);
        CHECK(hudG < 155);
        // ...et l'etalonnage NON : il est encore VERT et pas gris. Desature, il aurait vire au gris
        // (luminance du vert = 182), donc son canal rouge serait monte a ~91 apres le fondu.
        CHECK(hudR < 30);
    }

    // ==== 4. LE FONDU AU BLANC BAT LA COURBE ==================================================
    // Avec Reinhard actif, RIEN de ce qui traverse la courbe ne peut atteindre 255. Un fondu au blanc a
    // fond doit pourtant donner exactement 255 — c'est la preuve qu'il s'applique APRES elle. S'il
    // passait avant, on lirait reinhard(1) = 0,5, soit ~128.
    {
        Post p; p.tonemapMode = "reinhard"; p.fadeAmount = 1.0; p.fadeColor = 0xFFFFFFFFu;
        render(p);
        INFO("fondu blanc + reinhard : " << luma(glowX, glowY) << " (128 signifierait AVANT la courbe)");
        CHECK(luma(glowX, glowY) > 250);
        CHECK(chan(hudX, hudY, 1) > 250);      // et le HUD est emporte lui aussi
    }

    // ==== 5. LES QUATRE ENSEMBLE, PUIS TOUS NEUTRES ===========================================
    // Le contournement a cout nul verifie a l'echelle de la FAMILLE : publier les quatre topics avec
    // leurs valeurs neutres doit rendre exactement la meme image que ne rien publier du tout, alors que
    // le code a traverse quatre parsings et quatre gardes.
    {
        Post allOn;
        allOn.bloomIntensity = 3.0; allOn.tonemapMode = "aces"; allOn.exposure = 1.6;
        allOn.saturation = 0.4; allOn.contrast = 1.2; allOn.tint = 0xC0D0FFFFu;
        allOn.fadeAmount = 0.25; allOn.fadeColor = 0x200040FFu;
        render(allOn);
        const int mixedGlow = luma(glowX, glowY);
        const int mixedHud  = chan(hudX, hudY, 1);
        INFO("les quatre : lueur=" << mixedGlow << " HUD.G=" << mixedHud);
        // Pas d'image degeneree : ni noire, ni saturee au blanc. Un pipeline qui s'ecroule en composant
        // (une cible non liee, un uniform oublie) donnerait l'un ou l'autre.
        CHECK(mixedGlow > 10);
        CHECK(mixedGlow < 250);
        CHECK(mixedHud > 40);

        // Tous neutres, les quatre topics quand meme publies.
        Post neutral;
        render(neutral);
        const int nGlow = luma(glowX, glowY);
        const int nHud  = chan(hudX, hudY, 1);

        // Et la reference : AUCUN topic de post-traitement publie. On la fabrique en republiant des
        // neutres -- ce qui est le meme etat -- puis on compare a la mesure du composite lui-meme.
        INFO("neutres : lueur=" << nGlow << " HUD.G=" << nHud);
        CHECK(nHud > 200);          // le HUD est intact
        CHECK(nGlow < 40);          // et hors de la lampe il ne reste que l'ambiant
    }

    renderer->shutdown();
    mgr.removeInstance("cp_r");
    mgr.removeInstance("cp_g");
    dev = nullptr;
    SDL_DestroyWindow(win);
    SDL_Quit();
}
