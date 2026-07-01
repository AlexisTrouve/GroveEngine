#pragma once

/**
 * tests/visual/TerrainTileset — the shared 5-tile terrain tileset PNG generator (water/sand/grass/rock/snow).
 *
 * WHAT : writes a runtime-generated tileset PNG (5 tiles in a row, procedural per-tile texture) to a path, to
 *        be loaded via render:tilemap:tileset. Shared by the tiled-map capture AND the interactive viewer's
 *        tiling mode so the tile textures can't drift between the two.
 *
 * WHY  : the tileset is demo *content* (not mapview core), and it needs a PNG writer (PngCapture), so it lives
 *        here in tests/visual — NOT in MapViewDemoScene.h, which stays pure grove::mapview (no PNG/renderer).
 *
 * HOW  : header-only. Each 16x16 tile gets a cheap procedural texture (waves / grains / blades / cracks /
 *        sparkle) so at a tile-visible zoom the texture reads, not a flat colour. Tile ids 1..5 map to the
 *        array layers loaded by loadArrayFromFile; id 0 is transparent (no tile).
 */

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "PngCapture.h"  // mvdemo::writeRgbaAsPng

namespace grove {
namespace mvdemo {

inline constexpr int kTerrainTileW = 16;   // px per tile edge
inline constexpr int kTerrainTiles = 5;    // water/sand/grass/rock/snow -> tile ids 1..5

// Generate the 5-tile terrain tileset (16x16 each, in a row -> 80x16) with procedural texture, write a PNG.
// loadArrayFromFile(path,16,16) slices it into 5 array layers: tile ids 1..5 = water/sand/grass/rock/snow.
inline void writeTerrainTileset(const std::string& path) {
    const int TW = kTerrainTileW, N = kTerrainTiles, W = TW * N, H = TW;
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

} // namespace mvdemo
} // namespace grove
