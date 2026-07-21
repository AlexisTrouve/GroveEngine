/**
 * GPU test: render:nineslice draws a continuous-border frame with the CORRECT source regions on real hardware.
 *
 * WHAT : upload a 32x32 border texture whose outer 8px RING is RED and whose inner 16x16 is BLUE, then draw ONE
 *        render:nineslice over the WHOLE framebuffer (inset 8). After the renderer runs, the FB EDGE pixels must
 *        read RED (the corner/edge quads sample the source's outer ring) and the FB CENTRE must read BLUE (the
 *        centre quad samples the inner region). If the 9-quad UV split were wrong — e.g. the centre sampled the
 *        border, or an edge sampled the middle — the centre would be red / the edge blue and the test fails.
 *        Decisive by construction.
 *
 * WHY  : the pure geometry (NineSliceUnit) and the CPU expansion into SpriteInstances (NineSliceCollectorTest)
 *        are headless-locked, but THIS project's own lesson is that GPU wiring bugs hide from headless texel
 *        tests (the baked-map white-texture bug — a create/upload key mismatch invisible until a --shot). This
 *        is the pixel readback that proves the composed UVs actually sample the right texels through the real
 *        sprite shader + AssetManager. [gpu] — skips cleanly without a GPU (like the other [gpu] tests).
 *
 * HOW  : the full-stack BgfxRendererModule + AssetManager + IIO harness of test_runtime_texture_gpu — a runtime
 *        texture feeds the frame art, render:nineslice{asset} is published, render into a framebuffer, read back.
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

TEST_CASE("render:nineslice samples the right source regions per quad (GPU)", "[gpu][nineslice]") {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { WARN("no SDL video — skipping"); return; }
    const int W = 64, H = 64;
    SDL_Window* win = SDL_CreateWindow("ns-gpu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { SDL_Quit(); WARN("no window — skipping"); return; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); REQUIRE(SDL_GetWindowWMInfo(win, &wmi));

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("ns_r");
    auto gIO = mgr.createInstance("ns_g");

    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode c("config");
        c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
        renderer->setConfiguration(c, rIO.get(), nullptr);
    }
    if (!renderer->getDevice()) {
        renderer->shutdown(); mgr.removeInstance("ns_r"); mgr.removeInstance("ns_g");
        SDL_DestroyWindow(win); SDL_Quit(); WARN("no GPU — skipping"); return;
    }
    assets::AssetManager* am = renderer->getAssetManager();
    REQUIRE(am != nullptr);

    // World camera: (0,0) top-left, zoom 1 -> 1px = 1 world unit over the whole FB, so a nineslice at (0,0)
    // sized WxH fills it. Published every frame (the module rebuilds the frame packet from IIO each process).
    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in);
    };

    // 1. CREATE + UPLOAD the border texture: 32x32, outer 8px ring RED, inner 16x16 BLUE.
    const int TW = 32, TH = 32, INSET = 8;
    { auto d = std::make_unique<JsonDataNode>("d");
      d->setString("id","frame"); d->setInt("width",TW); d->setInt("height",TH); d->setInt("color", 0);
      gIO->publish("render:texture:create", std::move(d)); }
    frame(); frame();
    REQUIRE(am->isResident("frame"));

    std::vector<uint8_t> px(static_cast<size_t>(TW)*TH*4);
    for (int y = 0; y < TH; ++y) for (int x = 0; x < TW; ++x) {
        uint8_t* p = &px[(static_cast<size_t>(y)*TW + x)*4];
        const bool border = (x < INSET || x >= TW-INSET || y < INSET || y >= TH-INSET);
        if (border) { p[0]=255; p[1]=0; p[2]=0; p[3]=255; }   // RED ring
        else        { p[0]=0;   p[1]=0; p[2]=255; p[3]=255; } // BLUE core
    }
    { auto d = std::make_unique<JsonDataNode>("d");
      d->setString("id","frame"); d->setInt("w",TW); d->setInt("h",TH);
      d->setBlob("pixels", px.data(), px.size());
      gIO->publish("render:texture:upload", std::move(d)); }
    frame(); frame(); frame();

    // 2. Draw ONE nineslice over the whole FB. Retained -> published once, persists across the readback frames.
    { auto d = std::make_unique<JsonDataNode>("d");
      d->setInt("renderId", 5000);
      d->setDouble("x",0); d->setDouble("y",0); d->setDouble("w",W); d->setDouble("h",H);
      d->setDouble("srcW",TW); d->setDouble("srcH",TH);
      d->setDouble("left",INSET); d->setDouble("right",INSET); d->setDouble("top",INSET); d->setDouble("bottom",INSET);
      d->setString("asset","frame");
      d->setInt("color", static_cast<int>(0xFFFFFFFFu)); d->setInt("layer", 10);
      gIO->publish("render:nineslice:add", std::move(d)); }

    // 3. RENDER into a framebuffer + read back.
    rhi::IRHIDevice* dev = renderer->getDevice();
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
    dev->setViewFramebuffer(0, fb); dev->setViewFramebuffer(1, fb);
    for (int i = 0; i < 4; ++i) frame();   // flush the retained nineslice through the readback fb

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0);
    REQUIRE(dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size())));
    auto chan = [&](int x, int y, int c){ return static_cast<int>(rgba[(static_cast<size_t>(y)*W + x)*4 + c]); };
    auto isRed  = [&](int x, int y){ return chan(x,y,0) > 150 && chan(x,y,2) < 90; };
    auto isBlue = [&](int x, int y){ return chan(x,y,2) > 150 && chan(x,y,0) < 90; };

    // The FB CENTRE samples the source core -> BLUE. The four EDGE midpoints sample the ring -> RED.
    INFO("centre RGB=" << chan(W/2,H/2,0) << "," << chan(W/2,H/2,1) << "," << chan(W/2,H/2,2)
         << " | left-edge R=" << chan(2,H/2,0) << " top-edge R=" << chan(W/2,2,0));
    CHECK(isBlue(W/2, H/2));    // centre quad -> inner region
    CHECK(isRed(2,   H/2));     // left edge -> ring
    CHECK(isRed(W-3, H/2));     // right edge -> ring
    CHECK(isRed(W/2, 2));       // top edge -> ring
    CHECK(isRed(W/2, H-3));     // bottom edge -> ring

    renderer->shutdown();
    mgr.removeInstance("ns_r"); mgr.removeInstance("ns_g");
    SDL_DestroyWindow(win); SDL_Quit();
}
