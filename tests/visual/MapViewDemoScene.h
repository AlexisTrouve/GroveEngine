#pragma once

/**
 * tests/visual/MapViewDemoScene — the synthetic world + terrain lens shared by the map-view demos.
 *
 * WHAT : a procedural (infinite) elevation world standing in for a real producer (Theomen), the field
 *        schema / grid spec it uses, and a terrain lens builder (palette + optional hillshade, continuous
 *        ramp or discrete bands). Shared by capture_mapview (offscreen PNG) and test_mapview_viewer
 *        (interactive window) so the two demos can't drift apart.
 *
 * WHY  : keeps the demo content in ONE place (a change to the terrain or palette shows in both); pure
 *        grove::mapview, no SDL/renderer here.
 *
 * NOTE : elevation is encoded at 0.25 m (Int16, scale 0.25) — fine enough that the hillshade gradient is
 *        smooth (a coarse 1 m field stair-steps the gradient into visible quantization contours).
 */

#include <cmath>
#include <cstdint>
#include <vector>

#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/Field.h"
#include "grove/mapview/Hillshade.h"
#include "grove/mapview/Lens.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Palette.h"
#include "grove/mapview/WorldDocument.h"

namespace grove {
namespace mvdemo {

// A synthetic, INFINITE procedural world: each chunk's elevation is generated on demand from a few octaves
// of a smooth function of the global cell coords. has() is always true (the cull bounds what's generated).
struct ProceduralWorld final : mapview::IChunkProvider {
    int W{64}, H{64};
    bool has(mapview::ChunkCoord) const override { return true; }
    mapview::ChunkData load(mapview::ChunkCoord c) override {
        mapview::ChunkData d;
        d.coord = c;
        d.cellCount = static_cast<uint32_t>(W * H);
        std::vector<uint32_t> elev(static_cast<size_t>(W) * H);
        for (int ly = 0; ly < H; ++ly) {
            for (int lx = 0; lx < W; ++lx) {
                const double gx = static_cast<double>(c.x) * W + lx;
                const double gy = static_cast<double>(c.y) * H + ly;
                double v = 460.0
                         + 380.0 * std::sin(gx * 0.008) * std::cos(gy * 0.008)
                         + 120.0 * std::sin(gx * 0.025 + 0.5) * std::cos(gy * 0.022)
                         +  55.0 * std::sin((gx + gy) * 0.05);
                if (v < 0.0) v = 0.0;
                if (v > 1000.0) v = 1000.0;
                elev[static_cast<size_t>(ly) * W + lx] = static_cast<uint32_t>(v * 4.0 + 0.5);  // 0.25 m steps
            }
        }
        d.fields.emplace_back("elevation", std::move(elev));
        return d;
    }
};

// The schema + grid the procedural world is read with.
inline std::vector<mapview::FieldDecl> demoSchema() {
    return { mapview::FieldDecl{"elevation", mapview::Encoding::Int, 16, 0.25, 0.0} };
}
inline mapview::GridSpec demoGrid(const ProceduralWorld& w) {
    return mapview::GridSpec{w.W, w.H, 1, 1.0, 1.0};
}

// The terrain colour stops (physical metres -> colour): deep sea -> shallow -> sand -> green -> rock -> snow.
inline std::vector<std::pair<double, mapview::Rgba>> terrainStops() {
    return {
        {0.0,    mapview::Rgba{0.04f, 0.10f, 0.32f, 1}},
        {280.0,  mapview::Rgba{0.10f, 0.32f, 0.62f, 1}},
        {320.0,  mapview::Rgba{0.22f, 0.52f, 0.78f, 1}},
        {338.0,  mapview::Rgba{0.85f, 0.80f, 0.55f, 1}},
        {380.0,  mapview::Rgba{0.27f, 0.55f, 0.22f, 1}},
        {600.0,  mapview::Rgba{0.45f, 0.36f, 0.22f, 1}},
        {820.0,  mapview::Rgba{0.55f, 0.52f, 0.48f, 1}},
        {1000.0, mapview::Rgba{0.98f, 0.98f, 1.00f, 1}},
    };
}

// Build the terrain lens. `banded` switches the palette from a continuous ramp to discrete altitude bands;
// `hillshade` toggles the relief shading.
inline mapview::Lens makeTerrainLens(bool hillshade, bool banded) {
    const auto stops = terrainStops();
    mapview::Palette pal = banded ? mapview::Palette::banded(stops) : mapview::Palette::ramp(stops);
    mapview::Layer layer{"elevation", pal, mapview::Filter::always(), 0, 1.0f};
    if (hillshade) {
        layer.hillshadeField = "elevation";
        layer.hillshade = mapview::Hillshade::fromAzimuthAltitude(2.36, 0.95, 0.30);  // NW sun ~54deg, gentle
    }
    return mapview::Lens{"terrain", {layer}};
}

} // namespace mvdemo
} // namespace grove
