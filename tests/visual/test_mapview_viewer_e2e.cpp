/**
 * E2E: the interactive map-viewer, DRIVEN BY REAL SDL INPUT. The proof its interactivity exists.
 *
 * Per doctrine, a UI is "non vérifié" until a test launches the app and clicks it for real. This does exactly
 * that for the viewer: it loads a world-document FROM DISK (the --load path), then injects real SDL events
 * (SDL_PushEvent — a left-drag, a mouse wheel, key presses) through the app's OWN event pump (ViewerApp::
 * pumpEvents, the same loop the live window runs) and asserts the camera + the visible-cell set respond:
 *   - the disk world renders cells live (—load works end to end),
 *   - a left-drag pans the camera (grab-pan),
 *   - the wheel zooms in, and zooming in shrinks the culled chunk set (cull responds to the camera),
 *   - H toggles hillshade, R restores the exact reset view, Esc stops the app.
 *
 * It is a real GPU E2E (hidden window + offscreen framebuffer + a captured PNG), registered as a ctest so the
 * viewer's input wiring stays locked. Plain main() with a CHECK macro (exit non-zero on any failure).
 *
 * Usage: test_mapview_viewer_e2e [out.png]
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"

#include <grove/DebugEngine.h>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include "grove/mapview/Compression.h"
#include "grove/mapview/Manifest.h"
#include "grove/mapview/WorldDocument.h"
#include "grove/mapview/WorldDocumentDisk.h"
#include "grove/mapview/WorldDocumentProvider.h"

#include "MapViewDemoScene.h"     // demoSchema (Int16 elevation @0.25m), makeTerrainLens
#include "MapViewViewerApp.h"
#include "PngCapture.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

// --- tiny check harness: count failures, print each, exit non-zero if any failed. ---
static int g_fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) { std::fprintf(stdout, "ok   : %s\n", msg); } \
    else      { std::fprintf(stderr, "FAIL : %s\n", msg); ++g_fails; } \
} while (0)

// --- SDL event injectors (real events onto the queue; drained by ViewerApp::pumpEvents). ---
static void pushKey(SDL_Keycode k)          { SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = k; SDL_PushEvent(&e); }
static void pushWheel(int y)                { SDL_Event e{}; e.type = SDL_MOUSEWHEEL; e.wheel.y = y; SDL_PushEvent(&e); }
static void pushButton(Uint32 t, int x, int y) { SDL_Event e{}; e.type = t; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y; SDL_PushEvent(&e); }
static void pushMotion(int x, int y)        { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = x; e.motion.y = y; SDL_PushEvent(&e); }

// A small, GENTLE island so makeTerrainLens's default relief stays readable (steep terrain would darken).
static double gentleElev(double gx, double gy) {
    double v = 500.0
             + 200.0 * std::sin(gx * 0.010) * std::cos(gy * 0.011)
             + 90.0  * std::sin((gx + gy) * 0.007);
    return v < 0.0 ? 0.0 : (v > 1000.0 ? 1000.0 : v);
}

// Write a finite 6x4-chunk world-document to disk (compressed). Big enough that zooming in shrinks the
// culled chunk set (so "cull responds" is observable), small enough to bake fast.
static void writeTestDoc(const std::string& dir, const mapview::Compressor& z) {
    constexpr int CX = 6, CY = 4, CW = 64, CH = 64;
    mapview::Manifest m;
    m.coordinate.topology  = "square";
    m.coordinate.cellSize  = {{1.0, 1.0}};
    m.coordinate.boundsMin = {{0, 0, 0}};
    m.coordinate.boundsMax = {{CX * CW - 1, CY * CH - 1, 0}};
    m.coordinate.chunkDims = {{CW, CH, 1}};
    m.fields               = mvdemo::demoSchema();
    m.chunksDir            = "chunks";

    std::vector<mapview::ChunkData> chunks;
    for (int cy = 0; cy < CY; ++cy) {
        for (int cx = 0; cx < CX; ++cx) {
            mapview::ChunkData d;
            d.coord = {cx, cy, 0};
            d.cellCount = static_cast<uint32_t>(CW * CH);
            std::vector<uint32_t> elev(static_cast<size_t>(CW) * CH);
            for (int ly = 0; ly < CH; ++ly)
                for (int lx = 0; lx < CW; ++lx)
                    elev[static_cast<size_t>(ly) * CW + lx] =
                        static_cast<uint32_t>(gentleElev(cx * CW + lx, cy * CH + ly) * 4.0 + 0.5);
            d.fields.emplace_back("elevation", std::move(elev));
            chunks.push_back(std::move(d));
        }
    }
    mapview::disk::writeWorldDocument(dir, m, chunks, &z);
}

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "mapview_viewer_e2e.png";
    const int W = 1280, H = 720;

    // Produce a world-document on disk to drive the viewer through (proves the --load path in the test itself).
    const mapview::Compressor z = mapview::codec::zlibCompressor();
    const std::string docDir = (std::filesystem::temp_directory_path() / "mapview_viewer_e2e_doc").string();
    std::filesystem::remove_all(docDir);
    writeTestDoc(docDir, z);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 2; }
    SDL_Window* win = SDL_CreateWindow("mapview_viewer_e2e", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 2; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto gIO = mgr.createInstance("mv_e2e");

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

    // --load path, in code: open the on-disk document and frame it.
    mapview::WorldDocumentProvider provider(docDir, z);
    const auto& coord = provider.manifest().coordinate;
    const double worldW = static_cast<double>(coord.boundsMax[0] - coord.boundsMin[0] + 1) * coord.cellSize[0];
    const double worldH = static_cast<double>(coord.boundsMax[1] - coord.boundsMin[1] + 1) * coord.cellSize[1];
    const camera::CameraView resetCam = mvdemo::fitCamera(0.0, 0.0, worldW, worldH, W, H);

    mvdemo::ViewerApp app(&engine, renderer, gIO.get(), W, H, provider, provider.schema(), provider.gridSpec(),
                          mvdemo::makeTerrainLens, resetCam);

    // Offscreen: redirect the world + HUD views so the render is captured headless.
    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(W), static_cast<uint16_t>(H));
    dev->setViewFramebuffer(0, fb);
    dev->setViewFramebuffer(1, fb);

    const float dt = 1.0f / 60.0f;

    // 0. Baseline: the disk world renders cells at the fit framing (—load works, live).
    app.renderFrame(dt);
    const size_t cellsFit = app.cellCount();
    const camera::CameraView cam0 = app.camera();
    CHECK(cellsFit > 0, "disk world renders cells at fit (—load path live)");

    // 1. Left-drag: press at (300,300), move 40 px left, release. Grab-pan -> camera x follows the cursor.
    pushButton(SDL_MOUSEBUTTONDOWN, 300, 300);
    pushMotion(260, 300);
    pushButton(SDL_MOUSEBUTTONUP, 260, 300);
    app.pumpEvents();
    app.renderFrame(dt);
    CHECK(app.camera().x > cam0.x, "left-drag pans the camera in x (grab-pan)");
    CHECK(app.camera().y == cam0.y, "a horizontal drag leaves camera y unchanged");

    // 2. Mouse wheel up: zoom in.
    const float zoomPre = app.camera().zoom;
    pushWheel(1);
    app.pumpEvents();
    CHECK(app.camera().zoom > zoomPre, "wheel-up zooms the camera in");

    // 3. Keep zooming in hard -> the culled chunk set (and thus the visible-cell count) shrinks.
    for (int i = 0; i < 10; ++i) pushWheel(1);
    app.pumpEvents();
    app.renderFrame(dt);
    CHECK(app.cellCount() < cellsFit, "zooming in shrinks the visible-cell set (cull responds to the camera)");

    // 4. 'H' toggles hillshade (a lens rebuild driven by a key).
    const bool hs0 = app.hillshade();
    pushKey(SDLK_h);
    app.pumpEvents();
    CHECK(app.hillshade() != hs0, "H toggles the hillshade lens");

    // 5. 'R' restores the EXACT reset view (camera + the same cell set as the fit baseline).
    pushKey(SDLK_r);
    app.pumpEvents();
    app.renderFrame(dt);
    CHECK(app.camera().x == cam0.x && app.camera().y == cam0.y && app.camera().zoom == cam0.zoom,
          "R restores the exact reset camera");
    CHECK(app.cellCount() == cellsFit, "R restores the fit view's cell set");

    // 6. 'Esc' stops the app.
    CHECK(app.running(), "app is running before Esc");
    pushKey(SDLK_ESCAPE);
    app.pumpEvents();
    CHECK(!app.running(), "Esc stops the app");

    // Capture a PNG artifact of the reset view after the full input sequence.
    app.renderFrame(dt);
    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
    if (dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
        mvdemo::writeRgbaAsPng(outPath, W, H, rgba);
        std::fprintf(stdout, "wrote %s\n", outPath.c_str());
    }

    engine.shutdown();
    mgr.removeInstance("mv_e2e");
    SDL_DestroyWindow(win); SDL_Quit();

    std::fprintf(stdout, "%s — %d check(s) failed\n", g_fails == 0 ? "PASS" : "FAIL", g_fails);
    return g_fails == 0 ? 0 : 1;
}
