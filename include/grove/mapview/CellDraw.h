#pragma once

/**
 * grove::mapview::CellDraw — the neutral, renderer-independent draw unit (S1d).
 *
 * WHAT  : One coloured quad to draw for a cell: a render-space centre, a size, a rotation, a z-order layer,
 *         and an Rgba tint. The MapView core fills these; nothing here knows about bgfx or SpriteInstance.
 *
 * WHY   : This is the chosen emit boundary (per "GO"): keeping the core's output neutral lets grove::mapview
 *         stay 100% renderer-independent (a headless analyzer / PNG dumper can consume CellDraw too). A thin
 *         adapter on the renderer side maps CellDraw -> SpriteInstance (x,y,scaleX,scaleY,rotation,layer,rgba
 *         + a white texture/default UV for solid colour) — a trivial 1:1 copy, negligible cost.
 *
 * HOW   : Header-only, std-only. Coordinates are in RENDER space (top-down: world XY; the renderer's camera
 *         does pan/zoom). Centre + size + rotation (sprite-style) is enough for square top-down; iso will
 *         later derive the quad from the projected corners.
 */

#include <cstdint>
#include <vector>

#include "grove/mapview/Color.h"

namespace grove {
namespace mapview {

struct CellDraw {
    double  x{0.0};        // render-space centre x
    double  y{0.0};        // render-space centre y
    double  w{1.0};        // render-space width
    double  h{1.0};        // render-space height
    double  rotation{0.0}; // radians (0 for top-down)
    int32_t layer{0};      // render z-order (from the Layer that produced it)
    Rgba    color{};       // per-cell tint
};

// A whole chunk's worth of TILE ids — the retained-tilemap counterpart of CellDraw (the tiling path).
//
// WHAT : one axis-aligned width×height tile grid at a world origin; `tiles` is row-major (gy*width + gx),
//        each a tileset id (0 = transparent). A TileLayer produces one of these per visible chunk; the host
//        turns each into a render:tilemap:add (retained), a whole chunk uploaded once.
// WHY  : the opposite trade-off from the per-cell bulk-sprite path — the tilemap uploads a chunk grid once and
//        the GPU draws millions of tiles from it, cheap for dense contiguous terrain. Keeping the emit neutral
//        (no bgfx here, like CellDraw) preserves the core's renderer-independence.
// HOW  : TopDown / axis-aligned ONLY — a rotated or iso projection can't feed a rectangular tilemap (it would
//        need per-tile sprites), so the world origin is simply the chunk's top-left CORNER and tileW/tileH the
//        world size of one tile (= the grid's cellW/cellH). This aligns tile (0,0)'s corner with the sprite
//        path's cell corner, so the two paths overlay exactly.
struct TileChunkDraw {
    int      chunkX{0};    // source chunk coord (x) — a stable id for retained add/update/remove
    int      chunkY{0};    // source chunk coord (y)
    int      width{0};     // tiles per row (= grid chunkW)
    int      height{0};    // rows (= grid chunkH)
    double   worldX{0.0};  // world-space X of the chunk's top-left corner
    double   worldY{0.0};  // world-space Y of the chunk's top-left corner
    double   tileW{1.0};   // world width of one tile (= grid cellW)
    double   tileH{1.0};   // world height of one tile (= grid cellH)
    int32_t  layer{0};     // render z-order (from the TileLayer that produced it)
    std::vector<uint16_t> tiles;  // row-major (gy*width + gx); tileset ids, 0 = transparent
};

} // namespace mapview
} // namespace grove
