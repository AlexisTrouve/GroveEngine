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

// Upper bound on chunks a single cull may emit. A real frame spans far fewer; a query that would exceed
// this is degenerate (an effectively unbounded viewport — the caller should clamp it to the world bounds),
// and returns EMPTY rather than OOM/hang. Generous (~4M) so no realistic viewport is ever affected.
inline constexpr long long kMaxViewportChunks = 1LL << 22;

// All chunk coords whose world box intersects the rectangle [minX,maxX] × [minY,maxY], on layer chunkZ.
// cellW/cellH = world units per cell; chunkW/chunkH = cells per chunk on each axis.
// Robust to degenerate input: a non-positive cell/chunk size returns empty, and an extreme (zoom-out)
// rect is clamped/refused rather than overflowing the reserve or invoking float->int UB.
inline std::vector<ChunkCoord> chunksInViewport(double minX, double minY, double maxX, double maxY,
                                                double cellW, double cellH, int chunkW, int chunkH, int chunkZ) {
    std::vector<ChunkCoord> out;
    if (maxX < minX || maxY < minY || chunkW <= 0 || chunkH <= 0 || cellW <= 0.0 || cellH <= 0.0) {
        return out;  // degenerate viewport / cell / chunk size -> empty (no UB, no phantom coords)
    }

    const double spanX = cellW * static_cast<double>(chunkW);  // world width of one chunk
    const double spanY = cellH * static_cast<double>(chunkH);

    // Clamp the floored chunk index to the int32 range BEFORE casting — float->int out of range is UB, and
    // the `!(v >= lo)` form also folds NaN to the low bound.
    auto clampIdx = [](double v) -> long long {
        constexpr double lo = -2147483648.0;
        constexpr double hi = 2147483647.0;
        if (!(v >= lo)) return -2147483648LL;
        if (v > hi) return 2147483647LL;
        return static_cast<long long>(v);
    };
    const long long cx0 = clampIdx(std::floor(minX / spanX));
    const long long cx1 = clampIdx(std::floor(maxX / spanX));
    const long long cy0 = clampIdx(std::floor(minY / spanY));
    const long long cy1 = clampIdx(std::floor(maxY / spanY));

    const long long w = cx1 - cx0 + 1;
    const long long h = cy1 - cy0 + 1;
    if (w <= 0 || h <= 0) return out;
    if (w > kMaxViewportChunks || h > kMaxViewportChunks || w * h > kMaxViewportChunks) {
        return out;  // unbounded enumeration -> refuse rather than OOM/hang
    }

    out.reserve(static_cast<size_t>(w * h));
    for (long long cy = cy0; cy <= cy1; ++cy) {
        for (long long cx = cx0; cx <= cx1; ++cx) {
            out.push_back(ChunkCoord{static_cast<int32_t>(cx), static_cast<int32_t>(cy),
                                     static_cast<int16_t>(chunkZ)});
        }
    }
    return out;
}

} // namespace mapview
} // namespace grove
