#pragma once

/**
 * grove::mapview::Lens — Layer + Lens, the composition bricks of the recipe system (S1d, §5).
 *
 * WHAT  : A Layer is one renderable: a field bound to a Palette and a Filter, with a z-order and an opacity.
 *         A Lens is an ordered stack of Layers with a name — one named "view" of the world (e.g. the tectonic
 *         lens = greyed elevation base + region overlays).
 *
 * WHY   : This is what makes "looking at a phase = verifying it" data-driven (mapview.md §5): the producer
 *         names lenses, the viewer is a dumb generic layer-renderer. Layer/Lens carry no logic — the MapView
 *         orchestrator walks them and applies each layer's Filter + Palette to each visible cell.
 *
 * HOW   : Header-only, std-only, plain aggregates. opacity multiplies the palette colour's alpha; layerZ is
 *         the render z-order (later layers/higher z draw on top). The filter defaults to "always draw".
 */

#include <cstdint>
#include <string>
#include <vector>

#include "grove/mapview/Filter.h"
#include "grove/mapview/Palette.h"

namespace grove {
namespace mapview {

struct Layer {
    std::string field;                      // which field this layer colours by
    Palette     palette;                    // value -> colour
    Filter      filter{Filter::always()};   // which cells this layer draws (default: all)
    int32_t     layerZ{0};                  // render z-order
    float       opacity{1.0f};              // multiplies the palette colour's alpha
};

struct Lens {
    std::string        name;
    std::vector<Layer> layers;              // drawn in order (later = on top)
};

} // namespace mapview
} // namespace grove
