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

#include "MapViewDemoScene.h"     // demoSchema (Int16 elevation @0.25m), makeTerrainLens, makeTileLens, demoRegions
#include "MapViewViewerApp.h"
#include "MapViewHud.h"           // resources/core HUD (Input+UI on the engine) — no-op unless GROVE_MAPVIEW_HUD
#include "MapViewPoster.h"        // renderPoster (the --poster tiled+stitched whole-map export)
#include "PngCapture.h"
#include "TerrainTileset.h"       // writeTerrainTileset (for the 'T' tiling mode)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    // Two synthetic resource-density fields (Unorm8 0..1) so the HUD's resource discovery + heatmap lens are
    // exercised end to end: res_iron_ore -> "Métaux", res_ice -> "Glaces&volatils" (per the HUD's category table).
    m.fields.push_back(mapview::FieldDecl{"res_iron_ore", mapview::Encoding::Unorm8, 8, 1.0, 0.0});
    m.fields.push_back(mapview::FieldDecl{"res_ice",      mapview::Encoding::Unorm8, 8, 1.0, 0.0});
    m.chunksDir            = "chunks";

    std::vector<mapview::ChunkData> chunks;
    for (int cy = 0; cy < CY; ++cy) {
        for (int cx = 0; cx < CX; ++cx) {
            mapview::ChunkData d;
            d.coord = {cx, cy, 0};
            d.cellCount = static_cast<uint32_t>(CW * CH);
            std::vector<uint32_t> elev(static_cast<size_t>(CW) * CH);
            std::vector<uint32_t> iron(static_cast<size_t>(CW) * CH);   // res_iron_ore density, raw 0..255
            std::vector<uint32_t> ice(static_cast<size_t>(CW) * CH);    // res_ice density, raw 0..255
            for (int ly = 0; ly < CH; ++ly)
                for (int lx = 0; lx < CW; ++lx) {
                    const int gx = cx * CW + lx, gy = cy * CH + ly;
                    const size_t i = static_cast<size_t>(ly) * CW + lx;
                    elev[i] = static_cast<uint32_t>(gentleElev(gx, gy) * 4.0 + 0.5);
                    // Smooth 0..255 blobs so the heatmap has variation (and the fields are non-empty per chunk).
                    iron[i] = static_cast<uint32_t>(127.5 + 127.0 * std::sin(gx * 0.02) * std::cos(gy * 0.017));
                    ice[i]  = static_cast<uint32_t>(127.5 + 127.0 * std::cos(gx * 0.013) * std::sin(gy * 0.021));
                }
            d.fields.emplace_back("elevation", std::move(elev));
            d.fields.emplace_back("res_iron_ore", std::move(iron));
            d.fields.emplace_back("res_ice", std::move(ice));
            chunks.push_back(std::move(d));
        }
    }
    mapview::disk::writeWorldDocument(dir, m, chunks, &z);

    // A planet-core side-car next to the doc, so the E2E exercises the REAL core.json path (not the mock):
    // distinctive temperature 3333 + a composition with `fraction` (the HUD precomputes fractionPct on load).
    const nlohmann::json core = {
        {"temperature_c", 3333.0}, {"max_capacity", 2.0e21}, {"total_mass", 1.0e21}, {"fill_ratio", 0.5},
        {"composition", nlohmann::json::array({
            {{"material", "iron_ore"},    {"quantity", 6.0e20}, {"fraction", 0.6}},
            {{"material", "uranium_ore"}, {"quantity", 4.0e20}, {"fraction", 0.4}},
        })}};
    std::ofstream(std::filesystem::path(dir) / "core.json") << core.dump(2);
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

    // Region overlays (render:sector) + the 'T' tiling mode, so the E2E exercises both new paths for real.
    const int tilesetTexId = 7;
    {
        const std::string tsPath = (std::filesystem::temp_directory_path() / "mapview_e2e_tileset.png").string();
        mvdemo::writeTerrainTileset(tsPath);
        auto ts = std::make_unique<JsonDataNode>("tileset");
        ts->setInt("textureId", tilesetTexId); ts->setString("path", tsPath);
        ts->setInt("tileW", mvdemo::kTerrainTileW); ts->setInt("tileH", mvdemo::kTerrainTileW);
        gIO->publish("render:tilemap:tileset", std::move(ts));
    }
    app.setRegions(mvdemo::demoRegions());
    app.enableTiling(mvdemo::makeTileLens(), tilesetTexId);

    // HUD: host Input + UI on the engine and drive events through feedAndPump() (feeds InputModule so the
    // clicks reach the UI, then the viewer's camera). Without the HUD build, pump() is the plain camera pump.
#ifdef GROVE_MAPVIEW_HUD
    mvdemo::MapViewHud hud(engine, gIO.get(), app, provider.schema(), W, H);
    auto pump = [&]{ hud.feedAndPump(); };
#else
    auto pump = [&]{ app.pumpEvents(); };
#endif

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
    // Region overlays: the demo regions fall inside the fit view -> they compile to draws (render:sector).
    CHECK(app.regionDrawCount() > 0, "regions compile to ring-sector draws in view (render:sector path)");

    // 1. Left-drag: press at (300,300), move 40 px left, release. Grab-pan -> camera x follows the cursor.
    pushButton(SDL_MOUSEBUTTONDOWN, 300, 300);
    pushMotion(260, 300);
    pushButton(SDL_MOUSEBUTTONUP, 260, 300);
    pump();
    app.renderFrame(dt);
    CHECK(app.camera().x > cam0.x, "left-drag pans the camera in x (grab-pan)");
    CHECK(app.camera().y == cam0.y, "a horizontal drag leaves camera y unchanged");

    // 2. Mouse wheel up: zoom in.
    const float zoomPre = app.camera().zoom;
    pushWheel(1);
    pump();
    CHECK(app.camera().zoom > zoomPre, "wheel-up zooms the camera in");

    // 3. Keep zooming in hard -> the culled chunk set (and thus the visible-cell count) shrinks.
    for (int i = 0; i < 10; ++i) pushWheel(1);
    pump();
    app.renderFrame(dt);
    CHECK(app.cellCount() < cellsFit, "zooming in shrinks the visible-cell set (cull responds to the camera)");

    // 4. 'H' toggles hillshade (a lens rebuild driven by a key).
    const bool hs0 = app.hillshade();
    pushKey(SDLK_h);
    pump();
    CHECK(app.hillshade() != hs0, "H toggles the hillshade lens");

    // 4b. 'T' switches to LIVE TILING: the retained-tilemap path streams chunks in/out as the camera moves.
    pushKey(SDLK_t);
    pump();
    app.renderFrame(dt);
    CHECK(app.tiling(), "T switches the viewer to the tiling path");
    CHECK(app.residentTileChunks() > 0, "tiling streams in the visible tile chunks");
    CHECK(app.residentTileChunks() == app.tileChunkCount(), "the resident set equals the visible tile chunks");

    // Zoom in so the viewport is smaller than the world (guarantees chunks churn on pan).
    for (int i = 0; i < 4; ++i) pushWheel(1);
    pump();
    app.renderFrame(dt);

    // Pan west several chunks then back east: chunks LEAVE the retained tilemap (remove) on the way out and
    // RE-ENTER (add) on the way back — the enter/leave lifecycle a static capture can never exercise.
    size_t removedSeen = 0, addedSeen = 0;
    auto drag = [&](int fromX, int toX) {
        pushButton(SDL_MOUSEBUTTONDOWN, fromX, 360);
        pushMotion(toX, 360);
        pushButton(SDL_MOUSEBUTTONUP, toX, 360);
        pump();
        app.renderFrame(dt);
        removedSeen += app.lastTileRemoved();
        addedSeen  += app.lastTileAdded();
    };
    for (int i = 0; i < 4; ++i) drag(300, 640);   // pan west
    for (int i = 0; i < 4; ++i) drag(640, 300);   // pan back east
    CHECK(removedSeen > 0, "panning removes tile chunks that left the viewport (retained lifecycle)");
    CHECK(addedSeen  > 0, "panning re-adds tile chunks that entered the viewport");

    // 'T' again -> back to the sprite path; every retained chunk is flushed (no leak).
    pushKey(SDLK_t);
    pump();
    app.renderFrame(dt);
    CHECK(!app.tiling(), "T toggles tiling back off");
    CHECK(app.residentTileChunks() == 0, "leaving tiling flushes all retained chunks (no leak)");

    // 5. 'R' restores the EXACT reset view (camera + the same cell set as the fit baseline).
    pushKey(SDLK_r);
    pump();
    app.renderFrame(dt);
    CHECK(app.camera().x == cam0.x && app.camera().y == cam0.y && app.camera().zoom == cam0.zoom,
          "R restores the exact reset camera");
    CHECK(app.cellCount() == cellsFit, "R restores the fit view's cell set");

    // 6. 'Esc' stops the app.
    CHECK(app.running(), "app is running before Esc");
    pushKey(SDLK_ESCAPE);
    pump();
    CHECK(!app.running(), "Esc stops the app");

#ifdef GROVE_MAPVIEW_HUD
    // 6b. HUD over the map: a click on a category BUTTON must reach the UI (real input->ui, injected) and fire
    //     its declarative event; and it must STILL fire at the same screen spot after the camera pans/zooms —
    //     proving the HUD is screen-fixed (the retained-HUD-bucket fix, through the full input->ui->render stack).
    auto hudClick = [&](int x, int y){
        // Press and release on SEPARATE frames (a click = press frame N, release frame N+1) — one input.process
        // per renderFrame, matching the live loop's cadence.
        pushMotion(x, y); pushButton(SDL_MOUSEBUTTONDOWN, x, y);
        for (int i = 0; i < 2; ++i) { pump(); app.renderFrame(dt); }
        pushButton(SDL_MOUSEBUTTONUP, x, y);
        for (int i = 0; i < 3; ++i) { pump(); app.renderFrame(dt); }   // release -> click fires -> gIO drains
    };
    for (int i = 0; i < 3; ++i) { pump(); app.renderFrame(dt); }        // settle: HUD layout up
    const int actionsBefore = hud.actionCount();
    hudClick(330, 20);                                                   // "Métaux" button (x 274..384, y 6..34)
    CHECK(hud.actionCount() > actionsBefore, "a click on a HUD category button reaches the UI (input->ui->declarative event)");
    CHECK(hud.lastCategory() == std::string("Métaux"), "the category button published its cat:select {id} arg");

    // Data-driven discovery: the resource fields come from the world's res_* schema; only res_->category is a table.
    CHECK(hud.categoryCount() >= 2, "resource categories discovered data-driven from the world's res_* schema");
    CHECK(hud.listItemCount() > 0, "the category click populated the resource sub-menu list (res_iron_ore in Métaux)");

    // Click the first resource row in the (now-open) left drawer -> the map swaps to its density heatmap lens.
    for (int i = 0; i < 4; ++i) { pump(); app.renderFrame(dt); }         // let the drawer open + the list render
    hudClick(120, 92);                                                   // list row 0 (drawer x~8, y~76, rowHeight 30)
    CHECK(hud.activeLens().rfind("resource:", 0) == 0,
          "clicking a resource row swaps the map to its density heatmap lens (ui:list:selected -> makeResourceLens)");
    // "Élévation" returns to the terrain lens (right end, clear of the open left drawer).
    hudClick(1190, 20);                                                  // catElev button (x 1130..1250)
    CHECK(hud.activeLens() == std::string("terrain"), "the Élévation button returns to the terrain lens");
    // The REAL planet-core side-car (<docDir>/core.json) reaches the core panel — NOT the mock. The HUD reads
    // it, precomputes fractionPct per composition row, and pushes the real temperature to the {{core.*}} bindings.
    CHECK(hud.loadCoreFromDir(docDir), "the real core.json side-car loads into the core panel (not the mock)");
    CHECK(std::fabs(hud.coreTemperatureC() - 3333.0) < 1e-9, "the real core.json temperature reaches the panel binding");
    for (int i = 0; i < 2; ++i) { pump(); app.renderFrame(dt); }

    // Pan + zoom the camera hard, then click the SAME screen position -> it must still hit the button.
    camera::CameraView moved = app.camera(); moved.x += 500.0f; moved.y += 300.0f; moved.zoom *= 3.0f;
    app.setCamera(moved);
    app.renderFrame(dt);
    const int actionsAfterPan = hud.actionCount();
    hudClick(330, 20);
    CHECK(hud.actionCount() > actionsAfterPan,
          "the HUD button still fires at the same screen spot after a camera pan/zoom (HUD is screen-fixed)");
    app.setCamera(cam0);                                                 // restore the reset view for the capture
#endif

    // Capture a PNG artifact of the reset view after the full input sequence.
    app.renderFrame(dt);
    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 0);
    if (dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
        mvdemo::writeRgbaAsPng(outPath, W, H, rgba);
        std::fprintf(stdout, "wrote %s\n", outPath.c_str());
    }

    // 7. POSTER export: tile the WHOLE map + stitch to one buffer (the --poster path). A poster-sized ViewerApp
    //    (so its render:camera viewport == the tile fb) drives renderPoster over the 384x256-cell world in 3x2
    //    tiles at 2 px/cell. Locks: it succeeds, dims == cells*ppc with NO cap, and the STITCHED image is real
    //    varied terrain (not blank/uniform) — i.e. the tiles composited actual content, not one gray frame.
    {
        const int ppc = 2, tileCells = 128, tilePx = tileCells * ppc;    // 256px tiles <= the 720 backbuffer
        const int cellsX = coord.boundsMax[0] - coord.boundsMin[0] + 1;   // 384
        const int cellsY = coord.boundsMax[1] - coord.boundsMin[1] + 1;   // 256
        mvdemo::ViewerApp posterApp(&engine, renderer, gIO.get(), tilePx, tilePx, provider,
                                    provider.schema(), provider.gridSpec(), mvdemo::makeTerrainLens, resetCam);
        const mvdemo::PosterResult pr = mvdemo::renderPoster(posterApp, renderer, coord.boundsMin[0], coord.boundsMin[1],
                                                             cellsX, cellsY, coord.cellSize[0], ppc, tileCells);
        CHECK(pr.ok, "poster render succeeds");
        CHECK(pr.width == cellsX * ppc && pr.height == cellsY * ppc, "poster dims == cells * px-per-cell (no size cap)");
        uint8_t lo = 255, hi = 0;                                         // spread on the green channel = varied terrain
        for (size_t i = 0; i + 4 <= pr.rgba.size(); i += 4) {
            const uint8_t g = pr.rgba[i + 1];
            if (g < lo) lo = g;
            if (g > hi) hi = g;
        }
        CHECK(hi - lo > 40, "poster is not blank/uniform — real terrain rendered across the stitched tiles");
    }

    engine.shutdown();
    mgr.removeInstance("mv_e2e");
    SDL_DestroyWindow(win); SDL_Quit();

    std::fprintf(stdout, "%s — %d check(s) failed\n", g_fails == 0 ? "PASS" : "FAIL", g_fails);
    return g_fails == 0 ? 0 : 1;
}
