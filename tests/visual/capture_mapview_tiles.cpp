/**
 * Headless CAPTURE of grove::mapview rendered as a TILED MAP (PNG tileset) — to a PNG.
 *
 * The "tiling" path (vs the flat-colour bulk-sprite cells): the terrain is drawn by the engine's retained
 * TilemapPass from a PNG tileset (water/sand/grass/rock/snow), one tile per elevation band. Chain (now through
 * the mapview CORE, not a demo-local mapper):
 *   IChunkProvider (island elevation) -> MapView + a Lens with a TileLayer(TileMapper: elevation -> tile id)
 *   -> MapView::tileChunks() (one TileChunkDraw per visible chunk) -> render:tilemap:add {tileData, tileset}
 *   -> TilemapPass -> GPU. The tileset PNG is generated at runtime and loaded via the render:tilemap:tileset
 *   topic. This proves the productized tiling path end-to-end: the exact same tile ids the unit test asserts
 *   (MapViewTileMapperUnit) become pixels here.
 *
 * Usage: capture_mapview_tiles [out.png]
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

#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Lens.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Projection.h"
#include "grove/mapview/TileMapper.h"
#include "grove/mapview/WorldDocument.h"

#include "PngCapture.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

// ------------------------------------------------------------------------------------------------
// Generate a 5-tile terrain tileset (16x16 each, in a row -> 80x16) with procedural texture, write a PNG.
// loadArrayFromFile(path,16,16) then slices it into 5 array layers: tile ids 1..5 = water/sand/grass/rock/snow.
// ------------------------------------------------------------------------------------------------
static void writeTileset(const std::string& path) {
    const int TW = 16, N = 5, W = TW * N, H = TW;
    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4, 255);
    auto put = [&](int x, int y, int r, int g, int b) {
        const size_t i = (static_cast<size_t>(y) * W + x) * 4;
        rgba[i] = static_cast<uint8_t>(r < 0 ? 0 : r > 255 ? 255 : r);
        rgba[i + 1] = static_cast<uint8_t>(g < 0 ? 0 : g > 255 ? 255 : g);
        rgba[i + 2] = static_cast<uint8_t>(b < 0 ? 0 : b > 255 ? 255 : b);
        rgba[i + 3] = 255;
    };
    for (int t = 0; t < N; ++t) {
        for (int ly = 0; ly < TW; ++ly) {
            for (int lx = 0; lx < TW; ++lx) {
                const uint32_t h = (static_cast<uint32_t>(lx) * 73856093u) ^ (static_cast<uint32_t>(ly) * 19349663u) ^ (static_cast<uint32_t>(t) * 83492791u);
                int r, g, b;
                switch (t) {
                    case 0: {  // water: horizontal wave bands
                        const int wave = ((ly + static_cast<int>(3.0 * std::sin(lx * 0.6))) % 4 < 2) ? 45 : 0;
                        r = 35 + wave; g = 95 + wave; b = 185 + wave; break;
                    }
                    case 1: {  // sand: scattered darker grains
                        const int grain = (h & 7u) == 0 ? -35 : 0;
                        r = 216 + grain; g = 200 + grain; b = 150 + grain; break;
                    }
                    case 2: {  // grass: bright blades + dark speckle
                        const int d = (h & 3u) == 0 ? 55 : ((h & 7u) == 1 ? -35 : 0);
                        r = 60 + d; g = 135 + d; b = 50 + d; break;
                    }
                    case 3: {  // rock: cracks + grain
                        const bool crack = ((lx + ly) % 6 == 0) || ((lx - ly + 16) % 7 == 0);
                        const int d = crack ? -48 : static_cast<int>(h & 15u) - 8;
                        r = 128 + d; g = 122 + d; b = 116 + d; break;
                    }
                    default: {  // snow: mostly white with sparkle
                        const int s = (h & 31u) == 0 ? 255 : 236 + static_cast<int>(h & 7u);
                        r = s; g = s; b = (s > 250 ? 255 : s + 6); break;
                    }
                }
                put(t * TW + lx, ly, r, g, b);
            }
        }
    }
    mvdemo::writeRgbaAsPng(path, W, H, rgba);
}

// Demo terrain tuned for TILE variety: an ISLAND field centred on the visible window (~64x36 world units
// at this zoom), so a single frame sweeps the whole water->snow range as concentric bands = all five
// textured tiles on screen at once. Own function (leaves the shared continental sprite scene untouched).
//   snow peak at the centre -> radial falloff to a sea rim, with hills/ridges to break the rings.
static double tileElevation(double gx, double gy) {
    const double cx = 32.0, cy = 18.0;                            // centre of the visible window
    const double dx = gx - cx, dy = gy - cy;
    const double r = std::sqrt(dx * dx + dy * dy);
    double v = 920.0
             - 19.0 * r                                           // radial falloff: snow centre -> sea rim
             + 120.0 * std::sin(gx * 0.16) * std::cos(gy * 0.16)  // rolling hills break the concentric rings
             + 70.0  * std::sin((gx + gy) * 0.09);                // fine ridges
    return v < 0.0 ? 0.0 : (v > 1000.0 ? 1000.0 : v);
}

// A provider that serves the island `tileElevation` as an "elevation" field, so the mapview core (not this
// demo) does the value->tile-id mapping. Encodes metres as a 16-bit int (scale 1) — decode is a no-op, so the
// TileMapper's metre thresholds read the raw value directly. has() is always true; the MapView cull bounds it.
struct TileIslandWorld final : mapview::IChunkProvider {
    int W{64}, H{64};
    bool has(mapview::ChunkCoord) const override { return true; }
    mapview::ChunkData load(mapview::ChunkCoord c) override {
        mapview::ChunkData d;
        d.coord = c;
        d.cellCount = static_cast<uint32_t>(W * H);
        std::vector<uint32_t> elev(static_cast<size_t>(W) * H);
        for (int ly = 0; ly < H; ++ly) {
            for (int lx = 0; lx < W; ++lx) {
                const double e = tileElevation(static_cast<double>(c.x * W + lx), static_cast<double>(c.y * H + ly));
                elev[static_cast<size_t>(ly) * W + lx] = static_cast<uint32_t>(e + 0.5);  // metres, rounded
            }
        }
        d.fields.emplace_back("elevation", std::move(elev));
        return d;
    }
};

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "mapview_tiles_capture.png";
    const int SW = 1280, SH = 720;

    const std::string tilesetPath = (std::filesystem::temp_directory_path() / "mapview_tileset.png").string();
    writeTileset(tilesetPath);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "no SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("capture_mapview_tiles", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SW, SH, SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "no window\n"); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto gIO = mgr.createInstance("mv_tiles");

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

    // Load the tileset (id 7) via the new renderer topic.
    {
        auto ts = std::make_unique<JsonDataNode>("tileset");
        ts->setInt("textureId", 7); ts->setString("path", tilesetPath);
        ts->setInt("tileW", 16); ts->setInt("tileH", 16);
        gIO->publish("render:tilemap:tileset", std::move(ts));
    }

    // Camera framing: zoomed in (~64 world units across the width) so each 1-unit tile is ~20 px and the
    // 16x16 tile TEXTURE is visible (at continental zoom a tile is a few px and reads as a flat colour).
    const double camX = 0.0, camY = 0.0, zoom = static_cast<double>(SW) / 64.0;
    auto publishCamera = [&] {
        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x", camX); cam->setDouble("y", camY); cam->setDouble("zoom", zoom);
        cam->setInt("viewportX", 0); cam->setInt("viewportY", 0); cam->setInt("viewportW", SW); cam->setInt("viewportH", SH);
        gIO->publish("render:camera", std::move(cam));
    };

    // Build the tiled map THROUGH THE MAPVIEW CORE: a provider serves the island elevation, a lens declares a
    // single TileLayer (elevation -> tile id via a TileMapper whose bands match the old elevToTile), and MapView
    // emits one TileChunkDraw per visible chunk. The host just turns each into a render:tilemap:add. The
    // TileChunkDraw carries the chunk's world corner + tile size, so tiles align 1:1 with cells (cellSize 1).
    TileIslandWorld world;
    mapview::SquareLayout layout(1.0, 1.0);
    mapview::TopDownProjection proj;
    std::vector<mapview::FieldDecl> schema{ mapview::FieldDecl{"elevation", mapview::Encoding::Int, 16, 1.0, 0.0} };
    mapview::MapView mv(schema, mapview::GridSpec{world.W, world.H, 1, 1.0, 1.0}, layout, proj, world, /*budget*/ 64);

    mapview::Lens lens;
    lens.name = "tiles";
    // Elevation (metres) -> tile id: water/sand/grass/rock/snow. Last band's upper bound is a huge sentinel so
    // everything >= 800 m maps to snow (5). Same thresholds the demo-local elevToTile used.
    lens.tileLayers.push_back(mapview::TileLayer{
        "elevation",
        mapview::TileMapper::banded({{300.0, 1}, {340.0, 2}, {520.0, 3}, {800.0, 4}, {1.0e12, 5}}),
        /*layerZ*/ 0});
    mv.setLens(lens);

    mv.setViewport(mapview::Viewport{camX, camY, camX + SW / zoom, camY + SH / zoom});
    mv.update();

    for (const mapview::TileChunkDraw& tc : mv.tileChunks()) {
        std::string tileData;
        tileData.reserve(tc.tiles.size() * 3);
        for (size_t i = 0; i < tc.tiles.size(); ++i) {
            if (i) tileData.push_back(',');
            tileData += std::to_string(tc.tiles[i]);
        }
        auto tm = std::make_unique<JsonDataNode>("tilemap");
        tm->setInt("id", (tc.chunkX + 500) * 1000 + (tc.chunkY + 500) + 1);  // non-zero, unique per chunk
        tm->setDouble("x", tc.worldX);
        tm->setDouble("y", tc.worldY);
        tm->setInt("width", tc.width); tm->setInt("height", tc.height);
        tm->setInt("tileW", static_cast<int>(tc.tileW)); tm->setInt("tileH", static_cast<int>(tc.tileH));
        tm->setInt("textureId", 7);
        tm->setString("tileData", tileData);
        gIO->publish("render:tilemap:add", std::move(tm));
    }

    auto frame = [&] { publishCamera(); engine.step(1.0f / 60.0f); };
    for (int i = 0; i < 4; ++i) frame();  // settle: tileset load + tilemap bake

    // Capture: redirect the world + HUD views to an offscreen framebuffer, render, read back.
    rhi::IRHIDevice* dev = renderer->getDevice();
    if (!dev) { std::fprintf(stderr, "no device\n"); return 2; }
    rhi::FramebufferHandle fb = dev->createFramebuffer(static_cast<uint16_t>(SW), static_cast<uint16_t>(SH));
    dev->setViewFramebuffer(0, fb);
    dev->setViewFramebuffer(1, fb);
    frame(); frame();

    std::vector<uint8_t> rgba(static_cast<size_t>(SW) * SH * 4, 0);
    if (!dev->readFramebuffer(fb, rgba.data(), static_cast<uint32_t>(rgba.size()))) {
        std::fprintf(stderr, "readback failed\n"); return 3;
    }
    if (!mvdemo::writeRgbaAsPng(outPath, SW, SH, rgba)) { std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 4; }
    std::fprintf(stdout, "wrote %s — %zu tiled chunks from a PNG tileset (via mapview core)\n", outPath.c_str(), mv.tileChunkCount());

    engine.shutdown();
    mgr.removeInstance("mv_tiles");
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
