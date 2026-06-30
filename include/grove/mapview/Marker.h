#pragma once

/**
 * grove::mapview::Marker — point markers + their layer/emit (S1i, the §5 "markerSet × icons").
 *
 * WHAT  : A Marker is a world-space point carrying a category `kind`, an `angle` (for directional markers —
 *         e.g. plate-drift arrows), and a `scale`. A MarkerLayer styles a whole set (colour by kind, base
 *         scale, opacity, filter) and compiles to a neutral MarkerDraw (the renderer's render:sprite).
 *
 * WHY   : The point-overlay half of the recipe vision — cities, resources, drift arrows, labels' anchors.
 *         Like regions, markers are few and global (not chunked); the host hands the set to MapView and the
 *         core culls + styles them by viewport each frame.
 *
 * HOW   : Header-only, std-only, renderer-neutral. Styling reuses Palette/Filter keyed by `kind`. MarkerDraw
 *         is world-space (the renderer's camera maps it); `rotation` carries the marker's angle so the
 *         renderer can orient an icon (e.g. an arrow along the drift direction).
 */

#include <cstdint>
#include <vector>

#include "grove/mapview/Color.h"
#include "grove/mapview/Filter.h"
#include "grove/mapview/Palette.h"

namespace grove {
namespace mapview {

// A point marker.
struct Marker {
    double   x{0.0};
    double   y{0.0};
    uint32_t kind{0};      // category, for categorical styling / icon selection
    double   angle{0.0};   // radians (directional markers, e.g. drift arrows)
    double   scale{1.0};   // per-marker scale
};

// How a set of markers is drawn.
struct MarkerLayer {
    Palette palette;                     // kind -> colour
    double  baseScale{1.0};              // multiplies each marker's own scale
    int32_t layerZ{0};
    float   opacity{1.0f};
    Filter  filter{Filter::always()};    // filter on `kind`
};

// Neutral emit unit: a placed, scaled, rotated, tinted sprite (maps to render:sprite).
struct MarkerDraw {
    double  x{0.0};
    double  y{0.0};
    double  scale{1.0};
    double  rotation{0.0};   // radians (from the marker's angle)
    int32_t layer{0};
    Rgba    color{};
};

} // namespace mapview
} // namespace grove
