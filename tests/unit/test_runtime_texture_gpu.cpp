/**
 * GPU test: RUNTIME textures / painting. The game can create a texture at runtime by a stable string id and
 * paint colored sub-rects into it — then render it like any streamed asset (render:sprite{asset:"id"}).
 *
 *   render:texture:create {id, width, height, color?}   -> create a resident RGBA8 texture filled with color
 *   render:texture:paint  {id, x, y, w, h, color}        -> fill a sub-rect (region update)
 *
 * Proves with a framebuffer READBACK (not just "no crash"): a canvas filled RED then painted GREEN in a
 * corner renders BOTH colours — so the create-fill AND the region paint actually wrote pixels, and the
 * texture is reachable by its asset id. [gpu] — skips cleanly without a GPU.
 */

#define SDL_MAIN_HANDLED

#include <catch2/catch_test_macros.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Assets/AssetManager.h"
#include "RHI/RHIDevice.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include <vector>
#include <memory>

using namespace grove;

TEST_CASE("runtime texture: create + paint a sub-rect, render by asset id (GPU)", "[gpu][assets][runtime]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 32, H = 32;
    SDL_Window* win = SDL_CreateWindow("rt-tex", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("rt_r");
    auto gIO = mgr.createInstance("rt_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("rt_r"); mgr.removeInstance("rt_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    assets::AssetManager* am = renderer->getAssetManager();
    REQUIRE(am != nullptr);

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    // 1. CREATE a 16x16 canvas filled RED, by string id.
    { auto d = std::make_unique<JsonDataNode>("d");
      d->setString("id","canvas"); d->setInt("width",16); d->setInt("height",16);
      d->setInt("color", static_cast<int>(0xFF0000FFu));   // red
      gIO->publish("render:texture:create", std::move(d)); }
    frame(); frame();

    REQUIRE(am->isResident("canvas"));                     // created + registered as a resident asset
    float u0,v0,u1,v1; REQUIRE(am->resolveSprite("canvas", u0,v0,u1,v1) != 0);

    // 2. PAINT the top-left 8x8 quadrant GREEN (region update).
    { auto d = std::make_unique<JsonDataNode>("d");
      d->setString("id","canvas"); d->setInt("x",0); d->setInt("y",0); d->setInt("w",8); d->setInt("h",8);
      d->setInt("color", static_cast<int>(0x00FF00FFu));   // green
      gIO->publish("render:texture:paint", std::move(d)); }
    frame(); frame(); frame();   // deliver the paint message + apply the region update before we render

    // 3. RENDER the canvas as a sprite filling the view, into a framebuffer, and read it back.
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
    dev->setViewFramebuffer(0, fb); dev->setViewFramebuffer(1, fb);
    auto drawCanvas = [&]{
        { auto s = std::make_unique<JsonDataNode>("d");
          s->setDouble("x", W*0.5); s->setDouble("y", H*0.5);          // sprite x/y = CENTER
          s->setDouble("scaleX", W); s->setDouble("scaleY", H);        // fill the whole view
          s->setString("asset", "canvas"); s->setInt("layer", 10);
          gIO->publish("render:sprite", std::move(s)); }
    };
    for (int i = 0; i < 4; ++i) { drawCanvas(); frame(); }   // ephemeral sprite each fb frame; flush readback

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));

    // Sample a grid: the painted corner reads GREEN, the rest reads RED. Flip-robust: just require BOTH a
    // clearly-green and a clearly-red pixel to exist -> create-fill AND paint both wrote real pixels.
    int greens = 0, reds = 0;
    for (int sy = 2; sy < H; sy += 4) for (int sx = 2; sx < W; sx += 4) {
        const uint8_t* p = &rgba[(static_cast<size_t>(sy)*W + sx)*4];
        const int r = p[0], g = p[1];
        if (g > 110 && g > r + 40) ++greens;
        else if (r > 110 && r > g + 40) ++reds;
    }
    INFO("greens=" << greens << " reds=" << reds);
    REQUIRE(greens >= 1);   // the painted sub-rect
    REQUIRE(reds   >= 1);   // the create-time fill

    renderer->shutdown();
    mgr.removeInstance("rt_r"); mgr.removeInstance("rt_g");
    SDL_DestroyWindow(win); SDL_Quit();
}

TEST_CASE("runtime texture: render:texture:upload writes RAW rgba pixels (GPU) — video slice 6c-0c", "[gpu][assets][runtime]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 32, H = 32;
    SDL_Window* win = SDL_CreateWindow("rt-upl", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("up_r");
    auto gIO = mgr.createInstance("up_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("up_r"); mgr.removeInstance("up_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    assets::AssetManager* am = renderer->getAssetManager();
    REQUIRE(am != nullptr);

    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    // 1. CREATE a 16x16 canvas (filled transparent — the upload replaces every pixel anyway).
    const int TW = 16, TH = 16;
    { auto d = std::make_unique<JsonDataNode>("d");
      d->setString("id","vid"); d->setInt("width",TW); d->setInt("height",TH); d->setInt("color", 0);
      gIO->publish("render:texture:create", std::move(d)); }
    frame(); frame();
    REQUIRE(am->isResident("vid"));

    // 2. UPLOAD raw RGBA: left half BLUE (0,0,255), right half YELLOW (255,255,0) — a real per-pixel frame.
    std::vector<uint8_t> px(static_cast<size_t>(TW)*TH*4);
    for (int y = 0; y < TH; ++y) for (int x = 0; x < TW; ++x) {
        uint8_t* p = &px[(static_cast<size_t>(y)*TW + x)*4];
        if (x < TW/2) { p[0]=0;   p[1]=0;   p[2]=255; p[3]=255; }   // blue
        else          { p[0]=255; p[1]=255; p[2]=0;   p[3]=255; }   // yellow
    }
    { auto d = std::make_unique<JsonDataNode>("d");
      d->setString("id","vid"); d->setInt("w",TW); d->setInt("h",TH);
      d->setBlob("pixels", px.data(), px.size());
      gIO->publish("render:texture:upload", std::move(d)); }
    frame(); frame(); frame();   // deliver + apply the region update before rendering

    // 3. RENDER filling the view + read back.
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
    dev->setViewFramebuffer(0, fb); dev->setViewFramebuffer(1, fb);
    auto drawCanvas = [&]{
        { auto s = std::make_unique<JsonDataNode>("d");
          s->setDouble("x", W*0.5); s->setDouble("y", H*0.5);
          s->setDouble("scaleX", W); s->setDouble("scaleY", H);
          s->setString("asset", "vid"); s->setInt("layer", 10);
          gIO->publish("render:sprite", std::move(s)); }
    };
    for (int i = 0; i < 4; ++i) { drawCanvas(); frame(); }

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));

    // BOTH a clearly-blue and a clearly-yellow pixel must exist -> the raw upload wrote real per-pixel data.
    int blues = 0, yellows = 0;
    for (int sy = 2; sy < H; sy += 4) for (int sx = 2; sx < W; sx += 4) {
        const uint8_t* p = &rgba[(static_cast<size_t>(sy)*W + sx)*4];
        const int r = p[0], g = p[1], b = p[2];
        if (b > 110 && b > r + 40 && b > g + 40) ++blues;
        else if (r > 110 && g > 110 && b < 80)   ++yellows;
    }
    INFO("blues=" << blues << " yellows=" << yellows);
    REQUIRE(blues   >= 1);
    REQUIRE(yellows >= 1);

    renderer->shutdown();
    mgr.removeInstance("up_r"); mgr.removeInstance("up_g");
    SDL_DestroyWindow(win); SDL_Quit();
}
