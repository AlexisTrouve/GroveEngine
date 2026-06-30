/**
 * Headless CAPTURE of grove::mapview rendered through the engine — to a PNG (slice P2).
 *
 * The end-to-end proof of the map viewer ("no E2E render = it doesn't exist"): a SYNTHETIC procedural
 * world (a generated elevation field — no dependency on Theomen's data yet) driven through the FULL chain:
 *   procedural ChunkProvider -> MapView (terrain palette + hillshade relief) -> drainCells
 *   -> SpriteAdapter (CellDraw -> SpriteInstance) -> BgfxRendererModule::submitSpriteBatch -> GPU.
 * The renderer is hosted on the engine; for the capture frame its views are redirected to an offscreen
 * framebuffer, rendered, read back, and written as a PNG so we can SEE exactly what the viewer draws.
 *
 * Usage: capture_mapview [out.png]   (run from the project root)
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Frame/FramePacket.h"
#include "MapView/SpriteAdapter.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"

#include <grove/DebugEngine.h>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Hillshade.h"
#include "grove/mapview/Lens.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Palette.h"
#include "grove/mapview/Projection.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

// --- svpng: minimal public-domain PNG writer (Milo Yip). Stored DEFLATE, no deps. ---
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

// A synthetic, INFINITE procedural world: every chunk is generated on demand from a smooth elevation
// function of the global cell coords. Stands in for a real producer (Theomen) so we can prove the render
// chain without its data. has() is always true (the cull bounds what's actually generated/visible).
struct ProceduralWorld final : mapview::IChunkProvider {
    int W{64}, H{64};
    bool has(mapview::ChunkCoord) const override { return true; }
    mapview::ChunkData load(mapview::ChunkCoord c) override {
        mapview::ChunkData d;
        d.coord = c;
        d.cellCount = static_cast<uint32_t>(W * H);
        std::vector<uint32_t> elev(static_cast<size_t>(W) * H);
        for (int ly = 0; ly < H; ++ly) {
            for (int lx = 0; lx < W; ++lx) {
                const double gx = static_cast<double>(c.x) * W + lx;
                const double gy = static_cast<double>(c.y) * H + ly;
                // A few octaves of relief spanning sea -> snow: broad continents + medium hills + fine ridges.
                double v = 460.0
                         + 380.0 * std::sin(gx * 0.008) * std::cos(gy * 0.008)
                         + 120.0 * std::sin(gx * 0.025 + 0.5) * std::cos(gy * 0.022)
                         +  55.0 * std::sin((gx + gy) * 0.05);
                if (v < 0.0) v = 0.0;
                if (v > 1000.0) v = 1000.0;
                // Encode at 0.25 m resolution (Int16, scale 0.25): a fine field so the hillshade gradient is
                // smooth — at 1 m steps the gradient stair-steps and the relief shows quantization contours.
                elev[static_cast<size_t>(ly) * W + lx] = static_cast<uint32_t>(v * 4.0 + 0.5);
            }
        }
        d.fields.emplace_back("elevation", std::move(elev));
        return d;
    }
};

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "mapview_capture.png";
    const int W = 1280, H = 720;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("capture_mapview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto gIO = mgr.createInstance("mv_game");

    DebugEngine engine;
    engine.initialize();

    auto rendererOwned = std::make_unique<BgfxRendererModule>();
    BgfxRendererModule* renderer = rendererOwned.get();
    {
        auto rCfg = std::make_unique<JsonDataNode>("config");
        rCfg->setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        rCfg->setInt("windowWidth", W); rCfg->setInt("windowHeight", H); rCfg->setBool("vsync", false);
        engine.registerStaticModule("renderer", std::move(rendererOwned), ModuleSystemType::SEQUENTIAL, std::move(rCfg));
    }

    // --- map viewer setup ---
    ProceduralWorld world;
    mapview::SquareLayout layout(1.0, 1.0);
    mapview::TopDownProjection proj;
    const std::vector<mapview::FieldDecl> schema = { mapview::FieldDecl{"elevation", mapview::Encoding::Int, 16, 0.25, 0.0} };
    mapview::MapView mv(schema, mapview::GridSpec{world.W, world.H, 1, 1.0, 1.0}, layout, proj, world, 64);

    // Terrain ramp: deep sea -> shallow -> sand -> green -> rock -> snow, with hillshade relief on elevation.
    mapview::Layer terrain{
        "elevation",
        mapview::Palette::ramp({
            {0.0,   mapview::Rgba{0.04f, 0.10f, 0.32f, 1}},
            {280.0, mapview::Rgba{0.10f, 0.32f, 0.62f, 1}},
            {320.0, mapview::Rgba{0.22f, 0.52f, 0.78f, 1}},
            {338.0, mapview::Rgba{0.85f, 0.80f, 0.55f, 1}},
            {380.0, mapview::Rgba{0.27f, 0.55f, 0.22f, 1}},
            {600.0, mapview::Rgba{0.45f, 0.36f, 0.22f, 1}},
            {820.0, mapview::Rgba{0.55f, 0.52f, 0.48f, 1}},
            {1000.0, mapview::Rgba{0.98f, 0.98f, 1.00f, 1}},
        }),
        mapview::Filter::always(), 0, 1.0f,
    };
    terrain.hillshadeField = "elevation";
    terrain.hillshade = mapview::Hillshade::fromAzimuthAltitude(2.36, 0.95, 0.30);  // NW sun ~54deg, gentle
    mv.setLens(mapview::Lens{"terrain", {terrain}});

    // Camera: world (0,0) at the viewport top-left, ~256 world units across the 1280px width.
    const double camX = 0.0, camY = 0.0, zoom = static_cast<double>(W) / 256.0;
    mv.setViewport(mapview::Viewport{camX, camY, camX + W / zoom, camY + H / zoom});

    std::vector<SpriteInstance> sprites;
    auto frame = [&] {
        mv.update();
        const auto& cells = mv.cells();
        sprites.resize(cells.size());
        if (!cells.empty()) mapview::render::toSpriteInstances(cells.data(), cells.size(), sprites.data());
        renderer->submitSpriteBatch(sprites.data(), sprites.size());

        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x", camX); cam->setDouble("y", camY); cam->setDouble("zoom", zoom);
        cam->setInt("viewportX", 0); cam->setInt("viewportY", 0); cam->setInt("viewportW", W); cam->setInt("viewportH", H);
        gIO->publish("render:camera", std::move(cam));

        engine.step(1.0f / 60.0f);
    };

    for (int i = 0; i < 4; ++i) frame();  // settle
    std::fprintf(stdout, "map cells/frame: %zu\n", mv.cellCount());

    // Capture: redirect the world + HUD views to an offscreen framebuffer, render, read back.
    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
    dev->setViewFramebuffer(0, fb);
    dev->setViewFramebuffer(1, fb);
    frame(); frame();  // render into fb (twice — readback is a frame behind)

    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
    if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
        std::fprintf(stderr, "readback failed\n"); return 3;
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3);
    for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
        rgb[i*3+0] = rgba[i*4+0]; rgb[i*3+1] = rgba[i*4+1]; rgb[i*3+2] = rgba[i*4+2];
    }
    FILE* fp = std::fopen(outPath.c_str(), "wb");
    if (!fp) { std::fprintf(stderr, "cannot open %s\n", outPath.c_str()); return 4; }
    svpng(fp, W, H, rgb.data(), 0);
    std::fclose(fp);
    std::fprintf(stdout, "wrote %s (%dx%d) — grove::mapview rendered through the engine\n", outPath.c_str(), W, H);

    engine.shutdown();
    mgr.removeInstance("mv_game");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
