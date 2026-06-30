#pragma once

/**
 * grove::mapview::GridLayout — the topology axis ① (S1a).
 *
 * WHAT  : IGridLayout is the cell-shape abstraction: cell<->world transforms, the "pick" (world->cell),
 *         a cell's world-space quad, and its neighbours. SquareLayout is the v1 concrete impl. Swapping in
 *         a HexLayout/RectLayout later changes ONLY these transforms — the renderer and the rest of the
 *         pipeline are untouched (that is the whole point of isolating topology).
 *
 * WHY   : Topology is one of the three orthogonal axes (mapview.md §2). Defining the interface day-one (per
 *         the locked decisions) means hex/iso/rect plug into an interface that already exists, with no
 *         rework. Pure transforms + neighbour structure are the entire difference between cell shapes.
 *
 * HOW   : Header-only, std-only, double precision. SquareLayout maps cell (cx,cy) to the axis-aligned world
 *         box [cx*sx,(cx+1)*sx) × [cy*sy,(cy+1)*sy); cellToWorld returns the cell CENTRE (natural sprite
 *         anchor), worldToCell floors (correct for negative coords too), cellQuad returns the 4 corners,
 *         neighbours are the 4 orthogonal cells on the same z-slice. Virtual dispatch is fine: the per-cell
 *         cost is a branch-predicted indirect call (~ns), dwarfed by the work it guards.
 */

#include <cmath>
#include <cstdint>
#include <vector>

#include "grove/mapview/Coord.h"
#include "grove/mapview/Geometry.h"

namespace grove {
namespace mapview {

// Topology ① — the shape of a cell, as a set of pure transforms.
struct IGridLayout {
    virtual ~IGridLayout() = default;

    // The cell's reference point in world space (its centre).
    virtual WorldPos cellToWorld(CellCoord c) const = 0;
    // The "pick": which cell contains this world point.
    virtual CellCoord worldToCell(WorldPos w) const = 0;
    // The cell's four world-space corners (shape-aware).
    virtual CellQuad cellQuad(CellCoord c) const = 0;
    // The cells adjacent to this one (same z-slice).
    virtual std::vector<CellCoord> neighbours(CellCoord c) const = 0;
};

// A regular square (or rectangular, via non-equal cellSize) grid.
class SquareLayout final : public IGridLayout {
public:
    // cellSize = world units per cell (sx, sy). Default 1×1.
    explicit SquareLayout(double cellSizeX = 1.0, double cellSizeY = 1.0)
        : sx_(cellSizeX), sy_(cellSizeY) {}

    WorldPos cellToWorld(CellCoord c) const override {
        // Centre of the cell: avoids edge ambiguity and is the natural anchor for a centred quad/sprite.
        return WorldPos{(static_cast<double>(c.x) + 0.5) * sx_,
                        (static_cast<double>(c.y) + 0.5) * sy_,
                        static_cast<double>(c.z)};
    }

    CellCoord worldToCell(WorldPos w) const override {
        // floor (not truncation) so the pick is correct on the negative side of the origin.
        return CellCoord{static_cast<int32_t>(std::floor(w.x / sx_)),
                         static_cast<int32_t>(std::floor(w.y / sy_)),
                         static_cast<int16_t>(std::lround(w.z))};
    }

    CellQuad cellQuad(CellCoord c) const override {
        const double x0 = static_cast<double>(c.x) * sx_;
        const double y0 = static_cast<double>(c.y) * sy_;
        const double x1 = x0 + sx_;
        const double y1 = y0 + sy_;
        const double z = static_cast<double>(c.z);
        // CCW from the min corner.
        return CellQuad{{{WorldPos{x0, y0, z}, WorldPos{x1, y0, z},
                          WorldPos{x1, y1, z}, WorldPos{x0, y1, z}}}};
    }

    std::vector<CellCoord> neighbours(CellCoord c) const override {
        // 4-connected (orthogonal) on the same slice. Diagonal/26-connected can be a future variant.
        return {CellCoord{c.x + 1, c.y, c.z}, CellCoord{c.x - 1, c.y, c.z},
                CellCoord{c.x, c.y + 1, c.z}, CellCoord{c.x, c.y - 1, c.z}};
    }

    double cellSizeX() const { return sx_; }
    double cellSizeY() const { return sy_; }

private:
    double sx_;
    double sy_;
};

} // namespace mapview
} // namespace grove
