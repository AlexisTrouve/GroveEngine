#pragma once

/**
 * grove::mapview::Geometry — the small value types the viewer's geometry pipeline passes around (S1a).
 *
 * WHAT  : WorldPos (a point in the world plane, Z-aware), RenderPos (a point in the space handed to the
 *         renderer), and CellQuad (a cell's four world-space corners, shape-aware).
 *
 * WHY   : These are renderer-NEUTRAL — no bgfx, no SpriteInstance. The map-view core computes geometry in
 *         these types and emits a neutral CellDraw (Layer.h); a thin adapter on the renderer side turns
 *         CellDraw into the engine's SpriteInstance. Keeping the core's vocabulary free of the renderer is
 *         what lets a headless tool (an analyzer, a PNG dumper) reuse the exact same pipeline.
 *
 * HOW   : Pure std-only, header-only, double precision in the core (large worlds need more than float can
 *         hold for a floor-based cell pick); the cast to float happens only at the SpriteInstance boundary.
 *         RenderPos is the output of a Projection: for top-down it equals the world XY (the renderer's
 *         camera does pan/zoom/rotation); for isometric it will be the skewed coordinates.
 */

#include <array>
#include <cstdint>

namespace grove {
namespace mapview {

// A point in the world plane. z is the vertical level (top-down v1 renders one z-slice).
struct WorldPos {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

// A point in the space submitted to the renderer (the output of a Projection). For top-down this is the
// world XY unchanged; the renderer's camera maps it to the screen.
struct RenderPos {
    double x{0.0};
    double y{0.0};
};

// A cell's four corners in WORLD space, counter-clockwise from the min corner. Shape-aware: a SquareLayout
// returns an axis-aligned quad, a future HexLayout returns the hexagon's bounding polygon, etc.
struct CellQuad {
    std::array<WorldPos, 4> corners{};
};

} // namespace mapview
} // namespace grove
