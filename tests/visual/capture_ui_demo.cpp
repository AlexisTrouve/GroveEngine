/**
 * Headless CAPTURE of the "Fleet Command" demo to a PNG.
 *
 * Drives the REAL pipeline (UIModule + BgfxRendererModule on a hidden bgfx window), pushes the fleet model
 * + selects a ship, then for the capture frame redirects the renderer's views into an offscreen framebuffer
 * (BgfxRendererModule::getDevice() -> createFramebuffer / setViewFramebuffer), renders, reads the pixels
 * back, and writes a PNG. Lets us SEE exactly what the engine draws, headless.
 *
 * Usage: capture_ui_demo [out.png]   (run from the project root for the asset path)
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "UIModule.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <vector>
#include <string>
#include <memory>

using namespace grove;
using nlohmann::json;

// --- svpng: minimal public-domain PNG writer (Milo Yip). Writes uncompressed (stored) DEFLATE, no deps. ---
static void svpng(FILE* fp, unsigned w, unsigned h, const unsigned char* img, int alpha) {
    static const unsigned t[] = { 0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c };
    unsigned a = 1, b = 0, c, p = w * (alpha ? 4 : 3) + 1, x, y, i;
#define SVPNG_PUT(u) fputc(u, fp)
#define SVPNG_U8A(ua, l) for (i = 0; i < l; i++) SVPNG_PUT((ua)[i]);
#define SVPNG_U32(u) do { SVPNG_PUT((u) >> 24); SVPNG_PUT(((u) >> 16) & 255); SVPNG_PUT(((u) >> 8) & 255); SVPNG_PUT((u) & 255); } while(0)
#define SVPNG_U8C(u) do { SVPNG_PUT(u); c ^= (u); c = (c >> 4) ^ t[c & 15]; c = (c >> 4) ^ t[c & 15]; } while(0)
#define SVPNG_U8AC(ua, l) for (i = 0; i < l; i++) SVPNG_U8C((ua)[i])
#define SVPNG_U16LC(u) do { SVPNG_U8C((u) & 255); SVPNG_U8C(((u) >> 8) & 255); } while(0)
#define SVPNG_U32C(u) do { SVPNG_U8C((u) >> 24); SVPNG_U8C(((u) >> 16) & 255); SVPNG_U8C(((u) >> 8) & 255); SVPNG_U8C((u) & 255); } while(0)
#define SVPNG_U8ADLER(u) do { SVPNG_U8C(u); a = (a + (u)) % 65521; b = (b + a) % 65521; } while(0)
#define SVPNG_BEGIN(s, l) do { SVPNG_U32(l); c = ~0U; SVPNG_U8AC(s, 4); } while(0)
#define SVPNG_END() SVPNG_U32(~c)
    SVPNG_U8A("\x89PNG\r\n\32\n", 8);
    SVPNG_BEGIN("IHDR", 13);
    SVPNG_U32C(w); SVPNG_U32C(h);
    SVPNG_U8C(8); SVPNG_U8C(alpha ? 6 : 2); SVPNG_U8AC("\0\0\0", 3);
    SVPNG_END();
    SVPNG_BEGIN("IDAT", 2 + h * (5 + p) + 4);
    SVPNG_U8AC("\x78\1", 2);
    for (y = 0; y < h; y++) {
        SVPNG_U8C(y == h - 1);
        SVPNG_U16LC(p); SVPNG_U16LC(~p & 0xffff);
        SVPNG_U8ADLER(0);
        for (x = 0; x < w * (alpha ? 4 : 3); x++, img++)
            SVPNG_U8ADLER(*img);
    }
    SVPNG_U32C((b << 16) | a);
    SVPNG_END();
    SVPNG_BEGIN("IEND", 0);
    SVPNG_END();
}

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "demo_capture.png";
    const int W = 1280, H = 720;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("capture", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("cap_renderer");
    auto uIO = mgr.createInstance("cap_ui");
    auto gIO = mgr.createInstance("cap_game");

    auto renderer = std::make_unique<BgfxRendererModule>();
    { JsonDataNode c("config");
      c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
      c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setBool("vsync", false);
      renderer->setConfiguration(c, rIO.get(), nullptr); }

    auto ui = std::make_unique<UIModule>();
    { JsonDataNode c("config");
      c.setString("layoutFile", "assets/ui/demo_fleet_command.json");
      c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setInt("baseLayer", 1000);
      ui->setConfiguration(c, uIO.get(), nullptr); }

    auto push = [&](const std::string& topic, json j){ gIO->publish(topic, std::make_unique<JsonDataNode>("d", std::move(j))); };
    auto frame = [&]{
        { auto cam = std::make_unique<JsonDataNode>("camera");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.016);
        ui->process(in); renderer->process(in);
    };

    // Populate + select a ship so the detail window shows real content.
    { json fleet = json::array();
      const char* nm[] = {"Aurora","Borealis","Cygnus","Draco","Equinox","Falcon","Gemini","Helios","Icarus","Juno","Kestrel","Lyra","Mistral","Nova"};
      const char* cl[] = {"Frigate","Hauler","Scout","Cruiser","Corvette"};
      for (int i=0;i<14;i++) fleet.push_back({ {"id","s"+std::to_string(i)},{"name",nm[i]},{"cls",cl[i%5]},{"hp",0.35+0.045*i} });
      push("ui:data", json{ {"credits",1240},{"turn",3},{"fleetCount",14},{"hasSelection",true},{"noSelection",false},
                            {"selected",{{"id","s0"},{"name","Aurora"},{"cls","Frigate"},{"hp",0.9}}},{"fleet",fleet} }); }
    for (int i=0;i<8;i++) frame();   // settle: layout, virtualized list window, bindings resolved

    // Capture: redirect both views into an offscreen framebuffer, render, read back.
    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
    dev->setViewFramebuffer(0, fb);
    dev->setViewFramebuffer(1, fb);
    frame(); frame();   // render into the fb (twice — bgfx readback is a frame behind)

    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
    if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
        std::fprintf(stderr, "readback failed\n"); return 3;
    }

    // RGBA -> RGB (opaque), write the PNG.
    std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3);
    for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
        rgb[i*3+0] = rgba[i*4+0]; rgb[i*3+1] = rgba[i*4+1]; rgb[i*3+2] = rgba[i*4+2];
    }
    FILE* fp = std::fopen(outPath.c_str(), "wb");
    if (!fp) { std::fprintf(stderr, "cannot open %s\n", outPath.c_str()); return 4; }
    svpng(fp, W, H, rgb.data(), 0);
    std::fclose(fp);
    std::fprintf(stdout, "wrote %s (%dx%d)\n", outPath.c_str(), W, H);

    ui->shutdown(); renderer->shutdown();
    mgr.removeInstance("cap_renderer"); mgr.removeInstance("cap_ui"); mgr.removeInstance("cap_game");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
