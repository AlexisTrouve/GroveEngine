/**
 * Unit Tests: grove::mapview topology + projection (map-viewer engine, slice S1a).
 *
 * WHAT  : Locks the two pure-geometry axes — SquareLayout (① topology: cell<->world, pick, quad, neighbours)
 *         and TopDownProjection (② projection: world->render identity + depth key). Exercised through the
 *         IGridLayout/IProjection interfaces too, so the day-one polymorphism is real.
 *
 * WHY    : These transforms are the foundation the streaming + recipe + emit stages build on; a wrong floor
 *         or off-by-half quad would mis-place every cell. Locking them here means hex/iso/rect later plug
 *         into a verified interface.
 *
 * HOW    : Pure std-only + Catch2. Double comparisons use WithinAbs. Round-trips cover negative coords (the
 *         classic floor-vs-truncate trap) and non-square cell sizes.
 */

#include <cstdlib>  // std::abs(int)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/mapview/GridLayout.h"
#include "grove/mapview/Projection.h"

using namespace grove::mapview;
using Catch::Matchers::WithinAbs;

TEST_CASE("mapview S1a - SquareLayout cell<->world round-trips, incl. negatives", "[mapview][layout][unit]") {
    const SquareLayout layout(1.0, 1.0);

    // cellToWorld returns the cell centre.
    const WorldPos c = layout.cellToWorld(CellCoord{3, -2, 0});
    REQUIRE_THAT(c.x, WithinAbs(3.5, 1e-9));
    REQUIRE_THAT(c.y, WithinAbs(-1.5, 1e-9));

    // worldToCell(cellToWorld(c)) == c for a spread of coords, including the negative side of the origin.
    for (CellCoord cell : {CellCoord{0, 0, 0}, CellCoord{5, 7, 0}, CellCoord{-1, -1, 0},
                           CellCoord{-2, 3, 0}, CellCoord{-10, -10, 0}}) {
        REQUIRE(layout.worldToCell(layout.cellToWorld(cell)) == cell);
    }
}

TEST_CASE("mapview S1a - worldToCell picks the containing cell at boundaries", "[mapview][layout][unit]") {
    const SquareLayout layout(1.0, 1.0);
    // A point just inside cell (0,0) and one just inside (-1,-1).
    REQUIRE(layout.worldToCell(WorldPos{0.0, 0.0, 0.0}) == CellCoord{0, 0, 0});
    REQUIRE(layout.worldToCell(WorldPos{0.99, 0.99, 0.0}) == CellCoord{0, 0, 0});
    REQUIRE(layout.worldToCell(WorldPos{-0.01, -0.01, 0.0}) == CellCoord{-1, -1, 0});
    REQUIRE(layout.worldToCell(WorldPos{1.0, 1.0, 0.0}) == CellCoord{1, 1, 0});
}

TEST_CASE("mapview S1a - non-square cell size scales both axes", "[mapview][layout][unit]") {
    const SquareLayout layout(2.0, 3.0);  // 2 wide, 3 tall

    const WorldPos c = layout.cellToWorld(CellCoord{1, 1, 0});
    REQUIRE_THAT(c.x, WithinAbs(3.0, 1e-9));   // (1+0.5)*2
    REQUIRE_THAT(c.y, WithinAbs(4.5, 1e-9));   // (1+0.5)*3

    REQUIRE(layout.worldToCell(WorldPos{5.5, 7.0, 0.0}) == CellCoord{2, 2, 0}); // floor(5.5/2)=2, floor(7/3)=2
}

TEST_CASE("mapview S1a - cellQuad returns the 4 world corners (CCW from min)", "[mapview][layout][unit]") {
    const SquareLayout layout(1.0, 1.0);
    const CellQuad q = layout.cellQuad(CellCoord{2, 3, 0});
    REQUIRE_THAT(q.corners[0].x, WithinAbs(2.0, 1e-9)); REQUIRE_THAT(q.corners[0].y, WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(q.corners[1].x, WithinAbs(3.0, 1e-9)); REQUIRE_THAT(q.corners[1].y, WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(q.corners[2].x, WithinAbs(3.0, 1e-9)); REQUIRE_THAT(q.corners[2].y, WithinAbs(4.0, 1e-9));
    REQUIRE_THAT(q.corners[3].x, WithinAbs(2.0, 1e-9)); REQUIRE_THAT(q.corners[3].y, WithinAbs(4.0, 1e-9));
}

TEST_CASE("mapview S1a - neighbours are the 4 orthogonal cells on the same slice", "[mapview][layout][unit]") {
    const SquareLayout layout(1.0, 1.0);
    const std::vector<CellCoord> n = layout.neighbours(CellCoord{0, 0, 5});
    REQUIRE(n.size() == 4);
    // Every neighbour differs by exactly one step on x XOR y, z unchanged.
    for (const CellCoord& c : n) {
        REQUIRE(c.z == 5);
        const int manhattan = std::abs(c.x) + std::abs(c.y);
        REQUIRE(manhattan == 1);
    }
}

TEST_CASE("mapview S1a - TopDownProjection is identity in XY with constant depth", "[mapview][projection][unit]") {
    const TopDownProjection proj;
    const RenderPos r = proj.project(WorldPos{12.5, -7.25, 3.0});
    REQUIRE_THAT(r.x, WithinAbs(12.5, 1e-9));
    REQUIRE_THAT(r.y, WithinAbs(-7.25, 1e-9));
    // Top-down: depth is constant (ordering comes from the layer field, not the projection).
    REQUIRE_THAT(proj.depthKey(WorldPos{0, 0, 0}), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(proj.depthKey(WorldPos{100, 100, 9}), WithinAbs(0.0, 1e-9));
}

TEST_CASE("mapview S1a - usable through the IGridLayout/IProjection interfaces", "[mapview][layout][unit]") {
    const SquareLayout square(1.0, 1.0);
    const TopDownProjection topdown;
    const IGridLayout& layout = square;
    const IProjection& proj = topdown;

    // Polymorphic round-trip: cell -> world -> render, then world -> cell.
    const CellCoord cell{4, -6, 0};
    const WorldPos w = layout.cellToWorld(cell);
    const RenderPos r = proj.project(w);
    REQUIRE_THAT(r.x, WithinAbs(w.x, 1e-9));
    REQUIRE(layout.worldToCell(w) == cell);
}
