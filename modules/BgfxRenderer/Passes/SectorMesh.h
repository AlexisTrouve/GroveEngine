#pragma once

// ============================================================================
// grove::geom — filled ring-sector (annulus sector / pie wedge) tessellation. PURE, header-only.
//
// WHAT : turn a sector {centre, inner radius r0, outer radius r1, angles a0..a1} into a TRIANGLE LIST
//        of (x,y) vertices, so the renderer (SectorPass) can draw it with the plain colour shader (no
//        new shape primitive on the GPU — it's just coloured triangles). A pie slice is r0 = 0.
//
// WHY  : the renderer only had rect / sprite / text — no arc. The action-wheel (UIRadial) wants real
//        pie wedges, and the same primitive serves cooldown rings, gauges, mini-radars. Keeping the
//        tessellation here, pure + GPU-free, lets an oracle lock it (every vertex sits on circle r0 or
//        r1, the wedge spans exactly [a0,a1]) — the GPU side is just "draw coloured triangles".
//
// HOW  : screen coords (y-down): a point at angle `a`, radius `r` = (cx + r·cos a, cy + r·sin a). The
//        wedge is split into `steps` angular quads; each quad = 2 triangles = 6 vertices (a triangle
//        LIST, so no degenerate-vertex bookkeeping between sectors when many are concatenated).
// ============================================================================

#include <cmath>
#include <vector>

namespace grove { namespace geom {

// How many angular quads to tessellate a wedge of `span` radians into (~0.15 rad/step ≈ smooth arc),
// minimum 2 so even a thin wedge is a real quad pair.
inline int sectorSteps(float span) {
    const int s = static_cast<int>(std::fabs(span) / 0.15f);
    return s < 2 ? 2 : s;
}

// Append the filled ring-sector to `out` as a triangle LIST of (x,y) pairs (2 floats/vertex,
// 6 vertices per step). `out` is appended to (not cleared) so many sectors share one buffer.
inline void appendSector(std::vector<float>& out, float cx, float cy, float r0, float r1,
                         float a0, float a1, int steps) {
    if (steps < 1) steps = 1;
    for (int i = 0; i < steps; ++i) {
        const float t0 = static_cast<float>(i) / steps;
        const float t1 = static_cast<float>(i + 1) / steps;
        const float b0 = a0 + (a1 - a0) * t0;     // this quad's two angular edges
        const float b1 = a0 + (a1 - a0) * t1;
        const float c0 = std::cos(b0), s0 = std::sin(b0);
        const float c1 = std::cos(b1), s1 = std::sin(b1);
        const float ix0 = cx + r0 * c0, iy0 = cy + r0 * s0;   // inner @ b0
        const float ox0 = cx + r1 * c0, oy0 = cy + r1 * s0;   // outer @ b0
        const float ox1 = cx + r1 * c1, oy1 = cy + r1 * s1;   // outer @ b1
        const float ix1 = cx + r0 * c1, iy1 = cy + r0 * s1;   // inner @ b1
        // triangle 1: inner0, outer0, outer1
        out.push_back(ix0); out.push_back(iy0);
        out.push_back(ox0); out.push_back(oy0);
        out.push_back(ox1); out.push_back(oy1);
        // triangle 2: inner0, outer1, inner1
        out.push_back(ix0); out.push_back(iy0);
        out.push_back(ox1); out.push_back(oy1);
        out.push_back(ix1); out.push_back(iy1);
    }
}

}} // namespace grove::geom
