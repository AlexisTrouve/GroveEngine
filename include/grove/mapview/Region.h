#pragma once

/**
 * grove::mapview::Region — circular region overlays + their layer/emit (S1i, the §5 "regionSet × style").
 *
 * WHAT  : A Region is a world-space circle (a tectonic plate, a biome zone, an area of interest) carrying a
 *         category `type` and an optional scalar `value`. A RegionLayer styles a whole set (colour by type or
 *         by value, filled disc or ring, opacity, filter). It compiles to a neutral RegionDraw (a ring-sector,
 *         the renderer's render:sector).
 *
 * WHY   : Per-cell colour shows a FIELD; a worldgen's structure (plate boundaries, zones, drift) lives in
 *         vector overlays. Regions are few and global — not chunked/streamed — so the host hands the set to
 *         MapView and the core culls + styles them by viewport each frame. This is what makes a "lens" able
 *         to show the interesting parts of a generated world, not just a gradient.
 *
 * HOW   : Header-only, std-only, renderer-neutral. Styling reuses Palette/Filter (the same recipe bricks):
 *         the styling KEY is the region's `type` (categorical) or `value` (ramp/banded), chosen per layer.
 *         RegionDraw is world-space (the renderer's camera maps it); a full disc is a0=0, a1=2π, r0=0.
 */

#include <cstdint>

#include "grove/mapview/Color.h"
#include "grove/mapview/Filter.h"
#include "grove/mapview/Overlays.h"  // the Region data struct
#include "grove/mapview/Palette.h"

namespace grove {
namespace mapview {

inline constexpr double kTwoPi = 6.283185307179586;

// How a set of regions is drawn.
struct RegionLayer {
    Palette palette;                     // styling key -> colour
    bool    byValue{false};              // false: key = type (categorical); true: key = value
    double  innerRatio{0.0};             // 0 = filled disc; in (0,1) = ring from innerRatio*radius to radius
    int32_t layerZ{0};
    float   opacity{1.0f};
    Filter  filter{Filter::always()};    // filter on the styling key
};

// Neutral emit unit: a filled ring-sector / disc (maps to render:sector).
struct RegionDraw {
    double  cx{0.0};
    double  cy{0.0};
    double  r0{0.0};   // inner radius (0 => filled disc)
    double  r1{0.0};   // outer radius
    double  a0{0.0};   // start angle (radians)
    double  a1{0.0};   // end angle (radians); a full circle is a0=0, a1=2π
    int32_t layer{0};
    Rgba    color{};
};

} // namespace mapview
} // namespace grove
