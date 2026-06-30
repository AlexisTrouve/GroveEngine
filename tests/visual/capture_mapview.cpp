/**
 * Headless CAPTURE of grove::mapview rendered through the engine — to a PNG (slice P2).
 *
 * The end-to-end proof of the map viewer ("no E2E render = it doesn't exist"): the SYNTHETIC procedural
 * world (MapViewDemoScene) driven through the FULL chain — ChunkProvider -> MapView (terrain palette +
 * hillshade) -> drainCells -> SpriteAdapter (CellDraw -> SpriteInstance) -> submitSpriteBatch -> GPU. The
 * renderer is hosted on the engine; for the capture frame its views are redirected to an offscreen
 * framebuffer, rendered, read back, and written as a PNG.
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

#include "MapViewDemoScene.h"
#include "PngCapture.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

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

    // --- map viewer setup (shared synthetic scene) ---
    mvdemo::ProceduralWorld world;
    mapview::SquareLayout layout(1.0, 1.0);
    mapview::TopDownProjection proj;
    const auto schema = mvdemo::demoSchema();
    mapview::MapView mv(schema, mvdemo::demoGrid(world), layout, proj, world, 64);
    mv.setLens(mvdemo::makeTerrainLens(/*hillshade*/ true, /*banded*/ false));

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
    if (!mvdemo::writeRgbaAsPng(outPath, W, H, rgba)) {
        std::fprintf(stderr, "cannot open %s\n", outPath.c_str()); return 4;
    }
    std::fprintf(stdout, "wrote %s (%dx%d) — grove::mapview rendered through the engine\n", outPath.c_str(), W, H);

    engine.shutdown();
    mgr.removeInstance("mv_game");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
