#pragma once

/**
 * grove::mapview::Overlays — the vector-overlay DATA structs (S1i format, recipe-independent).
 *
 * WHAT  : The plain Region (circle) and Marker (point) records — the *content* of a world-document's vector
 *         overlays. Pure data: no Palette/Filter, no renderer types.
 *
 * WHY   : These are format-level (they live in the world-document, carried in the manifest as small JSON
 *         lists — overlays are low-cardinality, so no chunked/bit-packed format is warranted). Keeping the
 *         DATA here, separate from the styling (RegionLayer/MarkerLayer in Region.h/Marker.h), lets Manifest.h
 *         hold them without pulling in the recipe — the format core stays recipe-independent.
 *
 * NOTE  : Areas (tectonic plates, biome zones) are NOT circles — they belong in a per-cell categorical FIELD
 *         (e.g. `plate_id`), which the existing chunked field system already renders (categorical palette).
 *         Region (circle) is for abstract zones / radii of influence; Marker is for genuine sub-cell points
 *         with attributes (cities, resources, drift arrows) that a per-cell raster cannot represent.
 *
 * HOW   : Header-only, std-only (cstdint). Plain aggregates with sane defaults.
 */

#include <cstdint>

namespace grove {
namespace mapview {

// A circular region overlay (abstract zone / radius of influence — areas should be a categorical field).
struct Region {
    double   cx{0.0};
    double   cy{0.0};
    double   radius{0.0};
    uint32_t type{0};      // category, for categorical styling
    double   value{0.0};   // optional scalar, for ramp/banded styling
};

// A point marker (city, resource, drift arrow…) at a sub-cell position with attributes.
struct Marker {
    double   x{0.0};
    double   y{0.0};
    uint32_t kind{0};      // category, for categorical styling / icon selection
    double   angle{0.0};   // radians (directional markers, e.g. drift arrows)
    double   scale{1.0};   // per-marker scale
};

} // namespace mapview
} // namespace grove
