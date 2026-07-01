/**
 * Interactive VIEWER for grove::mapview, in groveEngine (slice S2a + disk-load).
 *
 * A real window you open and navigate. By default it shows the synthetic procedural world
 * (MapViewDemoScene); with `--load <dir>` it opens a real world-document ON DISK through
 * WorldDocumentProvider (the "file is the interface" path, live and navigable). The full chain
 * (MapView -> SpriteAdapter -> submitSpriteBatch) runs live, with grove::camera driving pan/zoom.
 *
 * The interaction logic lives in ViewerApp (MapViewViewerApp.h) so it can be E2E-tested by injecting real
 * SDL events (see test_mapview_viewer_e2e) — this main() is just the window + provider wiring around it.
 *
 * Controls: left-drag = pan (grab), mouse wheel = zoom toward cursor, H = toggle hillshade,
 *           B = toggle banded/continuous palette, R = reset camera, Esc = quit.
 *
 * Usage: test_mapview_viewer                          (interactive, synthetic world)
 *        test_mapview_viewer --load <dir>             (interactive, a world-document on disk)
 *        test_mapview_viewer --selftest [out.png]     (headless: scripted pan+zoom over N frames -> PNG)
 *        test_mapview_viewer --load <dir> --selftest [out.png]
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "Scene/Camera.h"

#include <grove/DebugEngine.h>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include "grove/mapview/Compression.h"
#include "grove/mapview/WorldDocumentProvider.h"

#include "MapViewDemoScene.h"
#include "MapViewViewerApp.h"
#include "PngCapture.h"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

int main(int argc, char** argv) {
    // --- args: --selftest [out.png], --load <dir> (any order) ---
    bool selftest = false;
    std::string outPath = "mapview_viewer_selftest.png";
    std::string loadDir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selftest") {
            selftest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
        } else if (a == "--load") {
            if (i + 1 < argc) loadDir = argv[++i];
        }
    }
    const int W = 1280, H = 720;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    const Uint32 flags = selftest ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    SDL_Window* win = SDL_CreateWindow("grove::mapview viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, flags);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto gIO = mgr.createInstance("mv_view");

    DebugEngine engine;
    engine.initialize();

    auto rendererOwned = std::make_unique<BgfxRendererModule>();
    BgfxRendererModule* renderer = rendererOwned.get();
    {
        auto rCfg = std::make_unique<JsonDataNode>("config");
        rCfg->setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        rCfg->setInt("windowWidth", W); rCfg->setInt("windowHeight", H); rCfg->setBool("vsync", !selftest);
        engine.registerStaticModule("renderer", std::move(rendererOwned), ModuleSystemType::SEQUENTIAL, std::move(rCfg));
    }

    // --- provider: a world-document on disk (--load) or the synthetic procedural world (default) ---
    std::unique_ptr<mapview::IChunkProvider> providerOwned;
    std::vector<mapview::FieldDecl> schema;
    mapview::GridSpec grid{};
    camera::CameraView resetCam;
    std::vector<mapview::Marker> markers;

    if (!loadDir.empty()) {
        // Open the .world dir. Pass a zlib compressor so both raw AND compressed documents load (readChunk
        // only uses it when a chunk is actually compressed). schema + grid + framing come from the manifest.
        auto p = std::make_unique<mapview::WorldDocumentProvider>(loadDir, mapview::codec::zlibCompressor());
        schema = p->schema();
        grid = p->gridSpec();
        const auto& coord = p->manifest().coordinate;
        const double worldW = static_cast<double>(coord.boundsMax[0] - coord.boundsMin[0] + 1) * coord.cellSize[0];
        const double worldH = static_cast<double>(coord.boundsMax[1] - coord.boundsMin[1] + 1) * coord.cellSize[1];
        resetCam = mvdemo::fitCamera(coord.boundsMin[0] * coord.cellSize[0], coord.boundsMin[1] * coord.cellSize[1],
                                     worldW, worldH, W, H);
        markers = p->manifest().markers;                       // overlays declared in the document (may be empty)
        providerOwned = std::move(p);
        std::fprintf(stdout, "loaded world-document from %s (%.0fx%.0f world units)\n", loadDir.c_str(), worldW, worldH);
    } else {
        auto p = std::make_unique<mvdemo::ProceduralWorld>();
        schema = mvdemo::demoSchema();
        grid = mvdemo::demoGrid(*p);
        resetCam.x = 0.0f; resetCam.y = 0.0f; resetCam.zoom = static_cast<float>(W) / 256.0f;
        resetCam.viewportW = static_cast<float>(W); resetCam.viewportH = static_cast<float>(H); resetCam.rotation = 0.0f;
        markers = mvdemo::demoMarkers();
        providerOwned = std::move(p);
    }

    // Register the PNG marker icon with the streaming AssetManager (resolved by render:sprite{asset}).
    {
        auto a = std::make_unique<JsonDataNode>("asset");
        a->setString("id", "mvicon");
        a->setString("path", "assets/textures/1f440.png");
        gIO->publish("asset:register", std::move(a));
    }

    // The interaction + render object (drives the same code the E2E test injects events into).
    mvdemo::ViewerApp app(&engine, renderer, gIO.get(), W, H, *providerOwned, schema, grid,
                          mvdemo::makeTerrainLens, resetCam);
    app.setMarkers(markers);

    if (selftest) {
        // Scripted pan + zoom-to-centre over N frames, captured to a PNG — proves the live pipeline responds
        // with no input. Offscreen so it's headless. (Input responsiveness is proven by test_mapview_viewer_e2e.)
        rhi::IRHIDevice* dev = renderer->getDevice();
        if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
        rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
        dev->setViewFramebuffer(0, fb);
        dev->setViewFramebuffer(1, fb);
        for (int i = 0; i < 45; ++i) {
            camera::CameraView c = app.camera();
            c.x += 0.4f;                                        // gentle pan east
            c = camera::zoomAt(c, camera::clampZoom(c.zoom * 1.006f, 0.5f, 64.0f),
                               static_cast<float>(W) * 0.5f, static_cast<float>(H) * 0.5f);  // zoom to centre
            app.setCamera(c);
            app.renderFrame(1.0f / 60.0f);
        }
        std::fprintf(stdout, "selftest: cells/frame=%zu final zoom=%.2f\n", app.cellCount(), app.camera().zoom);
        std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
            std::fprintf(stderr, "readback failed\n"); return 3;
        }
        if (!mvdemo::writeRgbaAsPng(outPath, W, H, rgba)) { std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 4; }
        std::fprintf(stdout, "wrote %s — scripted pan+zoom through the live viewer pipeline\n", outPath.c_str());
    } else {
        std::fprintf(stdout, "grove::mapview viewer — drag=pan, wheel=zoom, H=hillshade, B=banded, R=reset, Esc=quit\n");
        Uint32 last = SDL_GetTicks();
        while (app.running()) {
            app.pumpEvents();
            const Uint32 now = SDL_GetTicks();
            float dt = (now - last) / 1000.0f;
            last = now;
            if (dt > 0.1f) dt = 0.1f;
            app.renderFrame(dt);
        }
    }

    engine.shutdown();
    mgr.removeInstance("mv_view");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
