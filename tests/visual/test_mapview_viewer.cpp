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
 *           B = toggle banded/continuous palette, T = toggle tiling (retained textured tiles),
 *           R = reset camera, Esc = quit.
 *
 * Usage: test_mapview_viewer                          (interactive, synthetic world)
 *        test_mapview_viewer --load <dir>             (interactive, a world-document on disk)
 *        test_mapview_viewer --selftest [out.png]     (headless: scripted pan+zoom over N frames -> PNG)
 *        test_mapview_viewer --load <dir> --selftest [out.png]
 *        test_mapview_viewer --load <dir> --shot [out.png] [--size WxH]
 *                                                     (headless: ONE frame of the WHOLE world at the reset/fit
 *                                                      view — no pan/zoom, letterboxed for non-square worlds -> PNG)
 *        test_mapview_viewer --load <dir> --poster [out.png] [--ppc N]
 *                                                     (headless: the WHOLE map tiled+stitched to ONE PNG at N
 *                                                      pixels/cell — no cell ceiling, no size cap: big map -> big PNG)
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
// NOTE: <SDL_syswm.h> is intentionally NOT included here — on Linux it drags in X11 macros that collide with
// the renderer's RHITypes.h. The native window/display handle is fetched via SdlNativeHandle.h, whose .cpp is
// the only place SDL_syswm.h lives.

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
#include "MapViewHud.h"           // resources/core HUD (Input+UI on the engine) — no-op unless GROVE_MAPVIEW_HUD
#include "MapViewPoster.h"
#include "PngCapture.h"
#include "SdlNativeHandle.h"
#include "TerrainTileset.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

int main(int argc, char** argv) {
    // --- args: --selftest [out.png] | --shot [out.png] [--size WxH], --load <dir> (any order) ---
    const int W = 1280, H = 720;            // window / backbuffer size (fixed; headless shots render OFFSCREEN)
    bool selftest = false;
    bool shot = false;                      // --shot: one static frame of the WHOLE world at the reset/fit view
    bool poster = false;                    // --poster: the WHOLE map tiled+stitched to one PNG at N px/cell
    int  ppc = 4;                           // --ppc: pixels per cell for the poster (bigger map -> bigger PNG)
    std::string outPath = "mapview_viewer_selftest.png";
    std::string loadDir;
    int outW = W, outH = H;                 // --shot output resolution (default = window size)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selftest") {
            selftest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
        } else if (a == "--shot") {
            shot = true;
            outPath = "mapview_shot.png";
            if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
        } else if (a == "--poster") {
            poster = true;
            outPath = "mapview_poster.png";
            if (i + 1 < argc && argv[i + 1][0] != '-') outPath = argv[++i];
        } else if (a == "--ppc") {
            // Pixels per cell for --poster. No upper cap on the resulting image (a huge map -> a huge PNG).
            if (i + 1 < argc) { const int v = std::atoi(argv[++i]); if (v > 0) ppc = v; }
        } else if (a == "--size") {
            // Parse WxH (e.g. 1600x1600) -> the offscreen shot resolution. Bigger = cells no longer sub-pixel.
            if (i + 1 < argc) {
                int sw = 0, sh = 0;
                if (std::sscanf(argv[i + 1], "%dx%d", &sw, &sh) == 2 && sw > 0 && sh > 0) { outW = sw; outH = sh; }
                else std::fprintf(stderr, "bad --size '%s' (want WxH, e.g. 1600x1600) — keeping %dx%d\n",
                                  argv[i + 1], outW, outH);
                ++i;
            }
        } else if (a == "--load") {
            if (i + 1 < argc) loadDir = argv[++i];
        }
    }
    // Clamp the shot size to a safe range (uint16 framebuffer + GPU texture limits).
    outW = outW < 64 ? 64 : (outW > 8192 ? 8192 : outW);
    outH = outH < 64 ? 64 : (outH > 8192 ? 8192 : outH);
    const bool headless = selftest || shot || poster;
    // Poster tile geometry: tile the world so each tile stays UNDER the sprite ceiling (~131k cells/frame) AND
    // under the fb/texture size. Cap the tile at 256 cells/side (65k cells, safe) and at 8192 px/side.
    const int tileCells = poster ? std::min(256, std::max(1, 8192 / ppc)) : 0;
    const int tilePx    = tileCells * ppc;
    // The window/backbuffer + camera size. The shot renders at --size; the poster renders one TILE at a time
    // (tilePx); every other mode keeps the 1280x720 window (so --selftest and interactive are byte-for-byte
    // unchanged — vpW/vpH == W/H there). POURQUOI size the WINDOW too, not just the offscreen framebuffer: bgfx's
    // backbuffer is created at this resolution, and rendering into an offscreen framebuffer LARGER than the
    // backbuffer comes back blank — so for a shot/poster we grow the (hidden) window/backbuffer to match.
    const int vpW = poster ? tilePx : (shot ? outW : W);
    const int vpH = poster ? tilePx : (shot ? outH : H);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    const Uint32 flags = headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    SDL_Window* win = SDL_CreateWindow("grove::mapview viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, vpW, vpH, flags);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    // Native handles for bgfx, cross-platform: HWND on Windows, X11 Window + Display* on Linux.
    void* nwh = nullptr; void* ndt = nullptr;
    if (!mvdemo::getSdlNativeHandles(win, &nwh, &ndt)) {
        std::fprintf(stderr, "no native window handle (unsupported windowing subsystem)\n"); SDL_Quit(); return 1;
    }

    auto& mgr = IntraIOManager::getInstance();
    auto gIO = mgr.createInstance("mv_view");

    DebugEngine engine;
    engine.initialize();

    auto rendererOwned = std::make_unique<BgfxRendererModule>();
    BgfxRendererModule* renderer = rendererOwned.get();
    {
        auto rCfg = std::make_unique<JsonDataNode>("config");
        rCfg->setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(nwh)));
        rCfg->setDouble("nativeDisplayHandle", static_cast<double>(reinterpret_cast<uintptr_t>(ndt)));
        rCfg->setInt("windowWidth", vpW); rCfg->setInt("windowHeight", vpH); rCfg->setBool("vsync", !headless);
        engine.registerStaticModule("renderer", std::move(rendererOwned), ModuleSystemType::SEQUENTIAL, std::move(rCfg));
    }

    // --- provider: a world-document on disk (--load) or the synthetic procedural world (default) ---
    std::unique_ptr<mapview::IChunkProvider> providerOwned;
    std::vector<mapview::FieldDecl> schema;
    mapview::GridSpec grid{};
    camera::CameraView resetCam;
    std::vector<mapview::Marker> markers;
    std::vector<mapview::Region> regions;
    // World extent in CELLS, for --poster (filled from the manifest on the --load path; poster requires --load).
    int posterMinCellX = 0, posterMinCellY = 0, posterCellsX = 0, posterCellsY = 0;
    double posterCellSize = 1.0;

    if (!loadDir.empty()) {
        // Open the .world dir. Pass a zlib compressor so both raw AND compressed documents load (readChunk
        // only uses it when a chunk is actually compressed). schema + grid + framing come from the manifest.
        auto p = std::make_unique<mapview::WorldDocumentProvider>(loadDir, mapview::codec::zlibCompressor());
        schema = p->schema();
        grid = p->gridSpec();
        const auto& coord = p->manifest().coordinate;
        posterMinCellX = coord.boundsMin[0];  posterMinCellY = coord.boundsMin[1];
        posterCellsX = coord.boundsMax[0] - coord.boundsMin[0] + 1;
        posterCellsY = coord.boundsMax[1] - coord.boundsMin[1] + 1;
        posterCellSize = coord.cellSize[0];
        const double worldW = static_cast<double>(coord.boundsMax[0] - coord.boundsMin[0] + 1) * coord.cellSize[0];
        const double worldH = static_cast<double>(coord.boundsMax[1] - coord.boundsMin[1] + 1) * coord.cellSize[1];
        resetCam = mvdemo::fitCamera(coord.boundsMin[0] * coord.cellSize[0], coord.boundsMin[1] * coord.cellSize[1],
                                     worldW, worldH, vpW, vpH);
        markers = p->manifest().markers;                       // overlays declared in the document (may be empty)
        regions = p->manifest().regions;                       // circular region overlays declared in the doc
        providerOwned = std::move(p);
        std::fprintf(stdout, "loaded world-document from %s (%.0fx%.0f world units)\n", loadDir.c_str(), worldW, worldH);
    } else {
        auto p = std::make_unique<mvdemo::ProceduralWorld>();
        schema = mvdemo::demoSchema();
        grid = mvdemo::demoGrid(*p);
        resetCam.x = 0.0f; resetCam.y = 0.0f; resetCam.zoom = static_cast<float>(vpW) / 256.0f;
        resetCam.viewportW = static_cast<float>(vpW); resetCam.viewportH = static_cast<float>(vpH); resetCam.rotation = 0.0f;
        markers = mvdemo::demoMarkers();
        regions = mvdemo::demoRegions();
        providerOwned = std::move(p);
    }

    // Generate + load the terrain tileset so the 'T' tiling mode has textures (render:tilemap:tileset).
    const int tilesetTexId = 7;
    {
        const std::string tsPath = (std::filesystem::temp_directory_path() / "mapview_viewer_tileset.png").string();
        mvdemo::writeTerrainTileset(tsPath);
        auto ts = std::make_unique<JsonDataNode>("tileset");
        ts->setInt("textureId", tilesetTexId); ts->setString("path", tsPath);
        ts->setInt("tileW", mvdemo::kTerrainTileW); ts->setInt("tileH", mvdemo::kTerrainTileW);
        gIO->publish("render:tilemap:tileset", std::move(ts));
    }

    // Register the PNG marker icon with the streaming AssetManager (resolved by render:sprite{asset}).
    {
        auto a = std::make_unique<JsonDataNode>("asset");
        a->setString("id", "mvicon");
        a->setString("path", "assets/textures/1f440.png");
        gIO->publish("asset:register", std::move(a));
    }

    // The interaction + render object (drives the same code the E2E test injects events into).
    mvdemo::ViewerApp app(&engine, renderer, gIO.get(), vpW, vpH, *providerOwned, schema, grid,
                          mvdemo::makeTerrainLens, resetCam);
    app.setMarkers(markers);
    app.setRegions(regions);
    app.enableTiling(mvdemo::makeTileLens(), tilesetTexId);   // 'T' switches terrain to the retained-tile path

    if (poster) {
        // WHOLE-MAP poster: tile the world + stitch to ONE PNG at `ppc` px/cell — no per-frame cell ceiling,
        // no texture-size limit, no sub-pixel, no letterbox. A big map yields a big PNG (the point). Requires a
        // loaded .world (the cell extent comes from its manifest).
        if (loadDir.empty()) { std::fprintf(stderr, "--poster requires --load <dir>\n"); return 2; }
        const long long pxW = static_cast<long long>(posterCellsX) * ppc;
        const long long pxH = static_cast<long long>(posterCellsY) * ppc;
        std::fprintf(stdout, "poster: %dx%d cells @ %d px/cell -> %lldx%lld px (tiles %d cells = %d px)\n",
                     posterCellsX, posterCellsY, ppc, pxW, pxH, tileCells, tilePx);
        // HARD limit — the PNG encoder (svpng) tops out at 21844 px WIDTH (16-bit stored block) + a 32-bit IDAT
        // length. Refuse BEFORE rendering thousands of tiles / allocating the full image (past the limit the
        // writer would emit a CORRUPT file). Actionable: lower --ppc. (Bigger output needs a real PNG encoder.)
        if (!mvdemo::svpngCanEncode(pxW > INT32_MAX ? INT32_MAX : static_cast<int>(pxW),
                                    pxH > INT32_MAX ? INT32_MAX : static_cast<int>(pxH))) {
            const int maxPpc = posterCellsX > 0 ? mvdemo::kSvpngMaxWidth / posterCellsX : 0;
            std::fprintf(stderr,
                "--poster: %lldx%lld px exceeds the PNG encoder's limit (max width %d px). Lower --ppc to <= %d"
                " (bigger output needs a real PNG encoder like stb_image_write).\n",
                pxW, pxH, mvdemo::kSvpngMaxWidth, maxPpc);
            return 3;
        }
        const mvdemo::PosterResult pr = mvdemo::renderPoster(app, renderer, posterMinCellX, posterMinCellY,
                                                             posterCellsX, posterCellsY, posterCellSize, ppc, tileCells);
        if (!pr.ok) {
            std::fprintf(stderr, "poster failed (out of memory or readback) for %lldx%lld px\n", pxW, pxH);
            return 3;
        }
        if (!mvdemo::writeRgbaAsPng(outPath, pr.width, pr.height, pr.rgba)) {
            std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 4;
        }
        std::fprintf(stdout, "wrote %s — the WHOLE map, %dx%d px (%d px/cell), tiled+stitched\n",
                     outPath.c_str(), pr.width, pr.height, ppc);
    } else if (shot) {
        // WHOLE-WORLD shot: render ONE static frame at the reset/fit camera (the entire world framed,
        // letterboxed for non-square worlds — NO pan/zoom, so nothing is cropped) to an offscreen framebuffer
        // sized to --size, then PNG. Reuses the selftest's device/readback/writeRgbaAsPng path; the only
        // differences are that the camera is left at its fit (app.camera() == resetCam) and the framebuffer is
        // sized to the requested output. The camera never moves -> deterministic.
        rhi::IRHIDevice* dev = renderer->getDevice();
        if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
        rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(outW), static_cast<uint16_t>(outH));
        dev->setViewFramebuffer(0, fb);
        dev->setViewFramebuffer(1, fb);
        // A few identical frames so any async texture upload (marker icons) lands before readback; the camera
        // is untouched (the fit), so every frame is the same static shot.
        for (int i = 0; i < 3; ++i) app.renderFrame(1.0f / 60.0f);
        const camera::CameraView c = app.camera();
        std::fprintf(stdout, "shot: %dx%d cells=%zu zoom=%.4f cam=(%.1f,%.1f)\n",
                     outW, outH, app.cellCount(), c.zoom, c.x, c.y);
        std::vector<uint8_t> rgba(static_cast<size_t>(outW) * outH * 4, 0);
        if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
            std::fprintf(stderr, "readback failed\n"); return 3;
        }
        if (!mvdemo::writeRgbaAsPng(outPath, outW, outH, rgba)) {
            std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 4;
        }
        std::fprintf(stdout, "wrote %s — the whole world at the reset/fit view (%dx%d)\n", outPath.c_str(), outW, outH);
    } else if (selftest) {
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
        std::fprintf(stdout, "grove::mapview viewer — drag=pan, wheel=zoom, H=hillshade, B=banded, T=tiling, R=reset, Esc=quit\n");
#ifdef GROVE_MAPVIEW_HUD
        // Resources/core HUD over the map (Input + UI hosted on the engine; screen-fixed via the retained HUD
        // bucket). Category button -> resource sub-menu list -> density heatmap; the right panel shows the core.
        mvdemo::MapViewHud hud(engine, gIO.get(), app, schema, vpW, vpH);
        hud.setMockCore();   // TODO: read <loadDir>/core.json once Theomen ships the side-car
        std::fprintf(stdout, "  + resources/core HUD (%zu resource categories from the world schema)\n", hud.categoryCount());
#endif
        Uint32 last = SDL_GetTicks();
        while (app.running()) {
#ifdef GROVE_MAPVIEW_HUD
            hud.feedAndPump();   // feed InputModule (-> UI) + the viewer camera (mouse gated over UI)
#else
            app.pumpEvents();
#endif
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
