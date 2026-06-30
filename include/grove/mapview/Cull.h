#pragma once

/**
 * grove::mapview::Cull — viewport -> visible chunk set (S1b).
 *
 * WHAT  : chunksInViewport() returns every chunk coord whose world box intersects a world-space rectangle,
 *         at one chunk-layer. This is step 1 of the per-frame pipeline (mapview.md §4): cull to the visible
 *         chunks (the viewport ⊕ a margin ring) before streaming.
 *
 * WHY   : Because the world is chunked and we cull, the number of visible chunks is bounded by screen × zoom,
 *         NEVER by world size — that is what makes an infinite world free at render time. The caller expands
 *         the rectangle by a prefetch margin before calling; this function stays pure (no margin policy).
 *
 * HOW   : Header-only, std-only. A chunk spans cellSize × chunkDims world units on each axis; the chunk
 *         index of a world coordinate is floor(coord / chunkSpan). We floor the min and max corners and emit
 *         the inclusive rectangle of chunk coords between them (works for negative coordinates too).
 */

#include <cmath>
#include <vector>

#include "grove/mapview/Coord.h"

namespace grove {
namespace mapview {

// All chunk coords whose world box intersects the rectangle [minX,maxX] × [minY,maxY], on layer chunkZ.
// cellW/cellH = world units per cell; chunkW/chunkH = cells per chunk on each axis.
inline std::vector<ChunkCoord> chunksInViewport(double minX, double minY, double maxX, double maxY,
                                                double cellW, double cellH, int chunkW, int chunkH, int chunkZ) {
    std::vector<ChunkCoord> out;
    if (maxX < minX || maxY < minY || chunkW <= 0 || chunkH <= 0) return out;

    const double spanX = cellW * static_cast<double>(chunkW);  // world width of one chunk
    const double spanY = cellH * static_cast<double>(chunkH);

    const int cx0 = static_cast<int>(std::floor(minX / spanX));
    const int cx1 = static_cast<int>(std::floor(maxX / spanX));
    const int cy0 = static_cast<int>(std::floor(minY / spanY));
    const int cy1 = static_cast<int>(std::floor(maxY / spanY));

    out.reserve(static_cast<size_t>(cx1 - cx0 + 1) * static_cast<size_t>(cy1 - cy0 + 1));
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            out.push_back(ChunkCoord{cx, cy, static_cast<int16_t>(chunkZ)});
        }
    }
    return out;
}

} // namespace mapview
} // namespace grove
