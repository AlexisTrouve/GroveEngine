/**
 * Headless visual demo of ASYNC ASSET STREAMING (phase 3). A grid of sprites referenced by string assetId is
 * rendered while assetAsyncLoad is ON: each cell starts as a visible MAGENTA placeholder (the texture isn't
 * resident yet — its decode is in flight on the worker thread) and POPS IN to the real ship texture once the
 * worker finishes and the module's pumpAsync() uploads it. We capture the framebuffer frame-by-frame so the
 * progressive fill-in is visible as a sequence of PNGs — that pop-in IS the async behaviour (a sync load would
 * show every cell filled on frame 0, blocking the render thread).
 *
 * Run from the project ROOT (relative asset paths): capture_async_stream [prefix] [W] [H]
 *   -> writes <prefix>_0.png .. <prefix>_N.png + <prefix>_final.png
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include "BgfxRendererModule.h"
#include "Assets/AssetManager.h"
#include "Resources/ResourceCache.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <chrono>

using namespace grove;
using nlohmann::json;

// Minimal PNG writer (same tiny encoder as the other captures).
static void svpng(FILE* fp, unsigned w, unsigned h, const unsigned char* img, int alpha) {
    static const unsigned t[] = { 0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c };
    unsigned a = 1, b = 0, c, p = w * (alpha ? 4 : 3) + 1, x, y, i;
#define PUT(u) fputc(u, fp)
#define U8A(ua, l) for (i = 0; i < l; i++) PUT((ua)[i]);
#define U32(u) do { PUT((u)>>24); PUT(((u)>>16)&255); PUT(((u)>>8)&255); PUT((u)&255); } while(0)
#define U8C(u) do { PUT(u); c ^= (u); c = (c>>4)^t[c&15]; c = (c>>4)^t[c&15]; } while(0)
#define U8AC(ua, l) for (i = 0; i < l; i++) U8C((ua)[i])
#define U16LC(u) do { U8C((u)&255); U8C(((u)>>8)&255); } while(0)
#define U32C(u) do { U8C((u)>>24); U8C(((u)>>16)&255); U8C(((u)>>8)&255); U8C((u)&255); } while(0)
#define U8ADLER(u) do { U8C(u); a=(a+(u))%65521; b=(b+a)%65521; } while(0)
#define BEG(s, l) do { U32(l); c = ~0U; U8AC(s, 4); } while(0)
#define END() U32(~c)
    U8A("\x89PNG\r\n\32\n", 8); BEG("IHDR", 13); U32C(w); U32C(h); U8C(8); U8C(alpha?6:2); U8AC("\0\0\0",3); END();
    BEG("IDAT", 2 + h*(5+p) + 4); U8AC("\x78\1", 2);
    for (y=0; y<h; y++) { U8C(y==h-1); U16LC(p); U16LC(~p & 0xffff); U8ADLER(0);
        for (x=0; x<w*(alpha?4:3); x++, img++) U8ADLER(*img); }
    U32C((b<<16)|a); END(); BEG("IEND", 0); END();
}

int main(int argc, char** argv) {
    const std::string prefix = argc > 1 ? argv[1] : "async_stream";
    const int W = argc > 2 ? std::atoi(argv[2]) : 760;
    const int H = argc > 3 ? std::atoi(argv[3]) : 520;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL\n"); return 1; }
    SDL_Window* win = SDL_CreateWindow("cap-async", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("acs_r"); auto gIO = mgr.createInstance("acs_g");

    // Renderer with ASYNC ON + a single decode thread (so the streaming is gradual enough to see).
    auto renderer = std::make_unique<BgfxRendererModule>();
    { JsonDataNode c("config");
      c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
      c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
      c.setBool("assetAsyncLoad", true); c.setInt("assetDecodeThreads", 1);
      renderer->setConfiguration(c, rIO.get(), nullptr); }

    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
    ResourceCache* cache = renderer->getResourceCache();
    assets::AssetManager* am = renderer->getAssetManager();
    if (!cache || !am) { std::fprintf(stderr, "no asset system\n"); return 2; }

    // A bright MAGENTA placeholder so a not-yet-streamed cell is obviously "loading" (vs the real ship later).
    {
        std::vector<uint8_t> mag(8 * 8 * 4);
        for (size_t i = 0; i < 8 * 8; ++i) { mag[i*4]=255; mag[i*4+1]=0; mag[i*4+2]=255; mag[i*4+3]=255; }
        rhi::TextureDesc d; d.width = 8; d.height = 8; d.format = rhi::TextureDesc::RGBA8; d.mipLevels = 1;
        d.data = mag.data(); d.dataSize = static_cast<uint32_t>(mag.size());
        rhi::TextureHandle h = dev->createTexture(d);
        const uint16_t pid = cache->registerTexture(h);
        am->setPlaceholder(pid);
    }

    // 24 assets in a 6x4 grid, cycling the 4 real ship PNGs (different ids -> 24 independent streams).
    const int COLS = 6, ROWS = 4, N = COLS * ROWS;
    const char* files[4] = { "assets/textures/ship/cockpit.png", "assets/textures/ship/reactor.png",
                             "assets/textures/ship/engine.png",  "assets/textures/ship/gun.png" };
    for (int i = 0; i < N; ++i) {
        char id[16]; std::snprintf(id, sizeof id, "ship/%02d", i);
        am->registerAsset(id, files[i % 4], /*priority*/ 0);
    }

    // Republish the whole (ephemeral) scene each frame: camera + 24 sprites referenced by asset id.
    auto publishScene = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        for (int i = 0; i < N; ++i) {
            const int col = i % COLS, row = i / COLS;
            char id[16]; std::snprintf(id, sizeof id, "ship/%02d", i);
            auto s = std::make_unique<JsonDataNode>("d");
            s->setDouble("cx", 30 + col * 120); s->setDouble("cy", 40 + row * 120);
            s->setDouble("scaleX", 96); s->setDouble("scaleY", 96);
            s->setString("asset", id); s->setInt("layer", 1000);
            gIO->publish("render:sprite", std::move(s));
        }
    };
    auto frame = [&]{ publishScene(); JsonDataNode in("input"); in.setDouble("deltaTime", 0.016); renderer->process(in); };

    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
    dev->setViewFramebuffer(0, fb); dev->setViewFramebuffer(1, fb);

    std::vector<uint8_t> rgba(static_cast<size_t>(W)*H*4, 0), rgb(static_cast<size_t>(W)*H*3);
    auto capture = [&](const std::string& path) {
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) { std::fprintf(stderr,"readback fail\n"); return; }
        for (size_t i=0;i<static_cast<size_t>(W)*H;++i){ rgb[i*3]=rgba[i*4]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2]; }
        FILE* fp = std::fopen(path.c_str(), "wb"); if(!fp){ std::fprintf(stderr,"open fail %s\n", path.c_str()); return; }
        svpng(fp, W, H, rgb.data(), 0); std::fclose(fp);
        std::fprintf(stdout, "wrote %s  (resident %zu/%d)\n", path.c_str(), am->residentCount(), N);
    };

    // Frame 0: every cell just got REQUESTED -> all magenta placeholders (decode in flight, nothing uploaded yet).
    frame();
    capture(prefix + "_0.png");

    // Stream in: a few frames with a small real wait so the single worker decodes a batch each step. Capture
    // each -> the grid fills with real ships progressively (the visible signature of off-thread streaming).
    for (int k = 1; k <= 6; ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        frame();
        capture(prefix + "_" + std::to_string(k) + ".png");
        if (am->residentCount() >= static_cast<size_t>(N)) break;
    }

    // Guarantee a fully-streamed final frame.
    for (int i = 0; i < 200 && am->residentCount() < static_cast<size_t>(N); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        frame();
    }
    frame();
    capture(prefix + "_final.png");

    renderer->shutdown();
    mgr.removeInstance("acs_r"); mgr.removeInstance("acs_g");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
