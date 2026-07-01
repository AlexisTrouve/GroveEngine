/**
 * Headless E2E CAPTURE: the "file is the interface" thesis, proven pixels-out.
 *
 * Unlike the other mapview demos (which read an in-RAM procedural generator), this one goes THROUGH the
 * on-disk format: it generates an island world, WRITES it to a real .world directory (manifest.json + zlib
 * chunk blobs), then LOADS it back via WorldDocumentProvider and renders it — so every pixel traces to a
 * file on disk, not a live generator. Chain:
 *   islandElev -> ChunkData -> disk::writeWorldDocument (compressed)   [PRODUCER, on disk]
 *   WorldDocumentProvider(dir) -> MapView -> SpriteAdapter -> submitSpriteBatch -> offscreen PNG   [VIEWER]
 *
 * This is the missing E2E link: before WorldDocumentProvider, the viewer bypassed its own format with an
 * in-memory provider, so disk -> provider -> MapView was never exercised end to end.
 *
 * Usage: capture_mapview_from_disk [out.png]
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "Frame/FramePacket.h"
#include "MapView/SpriteAdapter.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITypes.h"
#include "Scene/Camera.h"

#include <grove/DebugEngine.h>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>

#include "grove/mapview/Compression.h"
#include "grove/mapview/Filter.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Hillshade.h"
#include "grove/mapview/Lens.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Palette.h"
#include "grove/mapview/Projection.h"
#include "grove/mapview/WorldDocument.h"
#include "grove/mapview/WorldDocumentDisk.h"
#include "grove/mapview/WorldDocumentProvider.h"

#include "MapViewDemoScene.h"   // demoSchema (Int16 elevation @0.25m), makeTerrainLens (palette + hillshade)
#include "PngCapture.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

// The finite world's extent: 4×2 chunks of 64×64 cells = 256×128 world units. Whole thing fits one frame.
static constexpr int kChunksX = 4, kChunksY = 2, kChunkW = 64, kChunkH = 64;
static constexpr int kWorldW = kChunksX * kChunkW;   // 256
static constexpr int kWorldH = kChunksY * kChunkH;   // 128

// Island elevation (metres) at a global cell: a high centre falling off to a sea rim, with gentle hills to
// break the rings. Encoded finite on disk (not generated at view time). Slopes are kept MODEST on purpose —
// hillshade darkens by surface tilt, and a steep synthetic cone would clamp the Lambertian dot to black on
// every away-slope. Low-frequency, low-amplitude hills keep the relief readable instead of blotchy.
static double islandElev(double gx, double gy) {
    const double dx = gx - kWorldW * 0.5, dy = gy - kWorldH * 0.5;
    const double r = std::sqrt(dx * dx + dy * dy);
    double v = 860.0
             - 5.0 * r                                              // radial falloff: high centre -> sea rim
             + 55.0 * std::sin(gx * 0.03) * std::cos(gy * 0.03)     // rolling hills (gentle)
             + 28.0 * std::sin((gx + gy) * 0.018);                  // fine ridges (gentle)
    return v < 0.0 ? 0.0 : (v > 1000.0 ? 1000.0 : v);
}

// PRODUCER: generate the island as a finite world-document and write it to `dir` (compressed chunk blobs).
static void writeIslandDoc(const std::string& dir, const mapview::Compressor& z) {
    mapview::Manifest m;
    m.coordinate.topology  = "square";
    m.coordinate.cellSize  = {{1.0, 1.0}};
    m.coordinate.boundsMin = {{0, 0, 0}};
    m.coordinate.boundsMax = {{kWorldW - 1, kWorldH - 1, 0}};
    m.coordinate.chunkDims = {{kChunkW, kChunkH, 1}};
    m.fields               = mvdemo::demoSchema();                  // one Int16 elevation field @ 0.25 m
    m.chunksDir            = "chunks";

    std::vector<mapview::ChunkData> chunks;
    for (int cy = 0; cy < kChunksY; ++cy) {
        for (int cx = 0; cx < kChunksX; ++cx) {
            mapview::ChunkData d;
            d.coord = {cx, cy, 0};
            d.cellCount = static_cast<uint32_t>(kChunkW * kChunkH);
            std::vector<uint32_t> elev(static_cast<size_t>(kChunkW) * kChunkH);
            for (int ly = 0; ly < kChunkH; ++ly) {
                for (int lx = 0; lx < kChunkW; ++lx) {
                    const double gx = static_cast<double>(cx) * kChunkW + lx;
                    const double gy = static_cast<double>(cy) * kChunkH + ly;
                    elev[static_cast<size_t>(ly) * kChunkW + lx] =
                        static_cast<uint32_t>(islandElev(gx, gy) * 4.0 + 0.5);  // 0.25 m steps
                }
            }
            d.fields.emplace_back("elevation", std::move(elev));
            chunks.push_back(std::move(d));
        }
    }
    mapview::disk::writeWorldDocument(dir, m, chunks, &z);
}

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "mapview_from_disk_capture.png";
    const int SW = 1280, SH = 720;

    // --- PRODUCER: write the world-document to a real directory on disk (compressed). ---
    const mapview::Compressor z = mapview::codec::zlibCompressor();
    const std::string docDir = (std::filesystem::temp_directory_path() / "mapview_island_doc").string();
    std::filesystem::remove_all(docDir);
    writeIslandDoc(docDir, z);

    // --- VIEWER: everything below reads the world back FROM DISK; nothing knows islandElev. ---
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("capture_mapview_from_disk", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SW, SH, SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto gIO = mgr.createInstance("mv_disk");

    DebugEngine engine;
    engine.initialize();

    auto rendererOwned = std::make_unique<BgfxRendererModule>();
    BgfxRendererModule* renderer = rendererOwned.get();
    {
        auto rCfg = std::make_unique<JsonDataNode>("config");
        rCfg->setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        rCfg->setInt("windowWidth", SW); rCfg->setInt("windowHeight", SH); rCfg->setBool("vsync", false);
        engine.registerStaticModule("renderer", std::move(rendererOwned), ModuleSystemType::SEQUENTIAL, std::move(rCfg));
    }

    // Open the world-document straight off disk. schema + grid geometry come from ITS manifest — the viewer
    // writes no JSON of its own and holds no generator. Pass the same compressor the document was written with.
    mapview::WorldDocumentProvider provider(docDir, z);
    mapview::SquareLayout layout(provider.gridSpec().cellW, provider.gridSpec().cellH);
    mapview::TopDownProjection proj;
    mapview::MapView mv(provider.schema(), provider.gridSpec(), layout, proj, provider, /*chunkBudget*/ 64);

    // Terrain lens: the shared colour ramp (sea -> snow) + GENTLE relief. Built inline rather than reusing
    // mvdemo::makeTerrainLens because its hillshade zFactor is tuned for the smoother ProceduralWorld; this
    // finite island packs the full elevation range into a small window (steeper slopes), so it needs a softer
    // exaggeration to stay readable instead of clamping to black on the away-slopes.
    {
        mapview::Layer layer{"elevation", mapview::Palette::ramp(mvdemo::terrainStops()),
                             mapview::Filter::always(), 0, 1.0f};
        layer.hillshadeField = "elevation";
        layer.hillshade = mapview::Hillshade::fromAzimuthAltitude(2.36, 0.95, /*zFactor*/ 0.08);  // NW sun, soft relief
        mv.setLens(mapview::Lens{"terrain", {layer}, {}, {}});
    }

    // Camera: frame the whole 256×128 world. zoom so the width fills 1280 px; centre the shorter axis.
    const float zoom = static_cast<float>(SW) / static_cast<float>(kWorldW);   // 5 px / world unit
    camera::CameraView cam;
    cam.x = 0.0f;
    cam.y = -0.5f * (static_cast<float>(SH) / zoom - static_cast<float>(kWorldH));  // vertical letterbox centre
    cam.zoom = zoom;
    cam.viewportW = static_cast<float>(SW); cam.viewportH = static_cast<float>(SH); cam.rotation = 0.0f;

    std::vector<SpriteInstance> sprites;
    auto renderFrame = [&] {
        const camera::WorldBounds wb = camera::visibleWorldBounds(cam);
        mv.setViewport(mapview::Viewport{wb.minX, wb.minY, wb.maxX, wb.maxY});
        mv.update();
        const auto& cells = mv.cells();
        sprites.resize(cells.size());
        if (!cells.empty()) mapview::render::toSpriteInstances(cells.data(), cells.size(), sprites.data());
        renderer->submitSpriteBatch(sprites.data(), sprites.size());

        auto camNode = std::make_unique<JsonDataNode>("camera");
        camNode->setDouble("x", cam.x); camNode->setDouble("y", cam.y); camNode->setDouble("zoom", cam.zoom);
        camNode->setInt("viewportX", 0); camNode->setInt("viewportY", 0); camNode->setInt("viewportW", SW); camNode->setInt("viewportH", SH);
        gIO->publish("render:camera", std::move(camNode));

        engine.step(1.0f / 60.0f);
    };
    for (int i = 0; i < 3; ++i) renderFrame();  // settle

    // Capture: redirect the world + HUD views to an offscreen framebuffer, render, read back.
    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(SW), static_cast<uint16_t>(SH));
    dev->setViewFramebuffer(0, fb);
    dev->setViewFramebuffer(1, fb);
    renderFrame(); renderFrame();

    std::vector<uint8_t> rgba(static_cast<size_t>(SW) * SH * 4, 0);
    if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
        std::fprintf(stderr, "readback failed\n"); return 3;
    }
    if (!mvdemo::writeRgbaAsPng(outPath, SW, SH, rgba)) { std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 4; }
    std::fprintf(stdout, "wrote %s — %zu cells rendered FROM the on-disk world-document (%s)\n",
                 outPath.c_str(), mv.cellCount(), docDir.c_str());

    engine.shutdown();
    mgr.removeInstance("mv_disk");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
