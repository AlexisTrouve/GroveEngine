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

} // namespace mapview
} // namespace grove
