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

// A handful of abstract circular regions over the initial view — drawn as coloured RING overlays (the §5
// "regionSet × style"). Styled by `type` (categorical) so the three kinds read as three colours; rendered by
// a host that drains MapView::regionDraws() -> render:sector (world space, so they pan/zoom with the map).
inline std::vector<mapview::Region> demoRegions() {
    return {
        mapview::Region{60.0,  45.0,  28.0, 0, 0.0},
        mapview::Region{150.0, 80.0,  34.0, 1, 0.0},
        mapview::Region{100.0, 115.0, 22.0, 2, 0.0},
        mapview::Region{205.0, 52.0,  26.0, 1, 0.0},
    };
}

// How the demo regions are styled: colour by type (3 categories), drawn as rings (innerRatio 0.72) so the
// terrain shows through, on layerZ 900 (over terrain, under the marker icons at 1000).
inline mapview::RegionLayer demoRegionLayer() {
    mapview::Palette pal = mapview::Palette::categorical({
        mapview::Rgba{0.96f, 0.24f, 0.24f, 1.0f},   // type 0 = red
        mapview::Rgba{0.24f, 0.86f, 0.96f, 1.0f},   // type 1 = cyan
        mapview::Rgba{0.98f, 0.86f, 0.22f, 1.0f},   // type 2 = yellow
    });
    return mapview::RegionLayer{pal, /*byValue*/ false, /*innerRatio*/ 0.72, /*layerZ*/ 900, /*opacity*/ 0.9f};
}

// A TILING lens: draw the terrain as textured tiles (retained tilemap) instead of flat-colour cells. The
// TileMapper's metre thresholds match the terrain palette's sea/coast/land/rock/peak bands (water/sand/grass/
// rock/snow -> tile ids 1..5); the last band's huge upper bound sends everything >= 800 m to snow. Pairs with
// TerrainTileset.h's tileset. Used by the viewer's 'T' toggle (live tiling) and the tiled-map capture.
inline mapview::Lens makeTileLens() {
    mapview::Lens lens;
    lens.name = "tiles";
    lens.tileLayers.push_back(mapview::TileLayer{
        "elevation",
        mapview::TileMapper::banded({{300.0, 1}, {340.0, 2}, {520.0, 3}, {800.0, 4}, {1.0e12, 5}}),
        /*layerZ*/ 0});
    return lens;
}

// A handful of point markers scattered over the initial view — rendered as PNG icons by the viewer.
inline std::vector<mapview::Marker> demoMarkers() {
    return {
        mapview::Marker{40.0, 30.0, 0, 0.0, 1.0},
        mapview::Marker{110.0, 55.0, 0, 0.0, 1.0},
        mapview::Marker{190.0, 45.0, 0, 0.0, 1.0},
        mapview::Marker{70.0, 110.0, 0, 0.0, 1.0},
        mapview::Marker{150.0, 118.0, 0, 0.0, 1.0},
        mapview::Marker{220.0, 80.0, 0, 0.0, 1.0},
    };
}

// A RESOURCE lens: the terrain underneath (elevation ramp, optional hillshade) + a density HEATMAP on top of
// one `res_<type>` field (Unorm8 0..1, sparse per chunk). The heat palette is alpha=0 at density 0 so the
// terrain shows through where the resource is absent; MapView skips a field missing from a chunk (sparse res_*
// fields just work). Drives the map-viewer's resource HUD: clicking a resource swaps the active lens to this.
inline mapview::Lens makeResourceLens(const std::string& field, bool hillshade) {
    const auto stops = terrainStops();
    mapview::Layer base{"elevation", mapview::Palette::ramp(stops), mapview::Filter::always(), 0, 1.0f};
    if (hillshade) {
        base.hillshadeField = "elevation";
        base.hillshade = mapview::Hillshade::fromAzimuthAltitude(2.36, 0.95, 0.30);
    }
    mapview::Palette heat = mapview::Palette::ramp({
        {0.0,  mapview::Rgba{1.0f, 0.85f, 0.10f, 0.0f}},
        {0.12, mapview::Rgba{1.0f, 0.82f, 0.15f, 0.55f}},
        {0.5,  mapview::Rgba{1.0f, 0.50f, 0.05f, 0.8f}},
        {1.0,  mapview::Rgba{0.95f, 0.10f, 0.02f, 0.95f}},
    });
    mapview::Layer heatLayer{field, heat, mapview::Filter::always(), 10, 1.0f};
    return mapview::Lens{"resource:" + field, {base, heatLayer}, {}, {}};
}

// A BIOME lens: the terrain underneath (elevation ramp + optional hillshade) + a CATEGORICAL biome overlay.
// WHAT : field "biome" holds an integer index; Palette::categorical maps table[index] -> colour. Index 0
//        (ocean / unclassified) is TRANSPARENT so the terrain shows through for water; land biomes paint over.
// WHY  : gives the coloured biome map (rainforest/desert/tundra…) the ASCII/PNG worldscope views produce, but
//        interactive (pan/zoom). `table` is built from the .world's biomes.json side-car (id -> colour) so the
//        palette is data-driven — no biome name hardcoded in the viewer.
// HOW  : the biome layer ALSO carries the elevation hillshade, so relief reads on the coloured land (a flat
//        overlay would wash the shading out). Table entry 0 (and any gap) must be transparent for the water/base.
inline mapview::Lens makeBiomeLens(const std::vector<mapview::Rgba>& table, bool hillshade) {
    const auto stops = terrainStops();
    mapview::Layer base{"elevation", mapview::Palette::ramp(stops), mapview::Filter::always(), 0, 1.0f};
    if (hillshade) {
        base.hillshadeField = "elevation";
        base.hillshade = mapview::Hillshade::fromAzimuthAltitude(2.36, 0.95, 0.30);
    }
    mapview::Layer biome{"biome", mapview::Palette::categorical(table), mapview::Filter::always(), 10, 1.0f};
    if (hillshade) {
        biome.hillshadeField = "elevation";                                     // relief on the coloured land
        biome.hillshade = mapview::Hillshade::fromAzimuthAltitude(2.36, 0.95, 0.30);
    }
    return mapview::Lens{"biome", {base, biome}, {}, {}};
}

// Build the terrain lens. `banded` switches the palette from a continuous ramp to discrete altitude bands;
// `hillshade` toggles the relief shading. Includes a marker layer (drawn on top, layerZ 1000) so a host that
// setMarkers() + renders MarkerDraw as sprites shows point icons over the terrain.
inline mapview::Lens makeTerrainLens(bool hillshade, bool banded) {
    const auto stops = terrainStops();
    mapview::Palette pal = banded ? mapview::Palette::banded(stops) : mapview::Palette::ramp(stops);
    mapview::Layer layer{"elevation", pal, mapview::Filter::always(), 0, 1.0f};
    if (hillshade) {
        layer.hillshadeField = "elevation";
        layer.hillshade = mapview::Hillshade::fromAzimuthAltitude(2.36, 0.95, 0.30);  // NW sun ~54deg, gentle
    }
    mapview::MarkerLayer markers{mapview::Palette::categorical({mapview::Rgba{1, 1, 1, 1}}), /*baseScale*/ 8.0, /*layerZ*/ 1000, 1.0f};
    // Includes the demo region-ring layer; harmless when no regions are set (a host must setRegions to see any).
    return mapview::Lens{"terrain", {layer}, {demoRegionLayer()}, {markers}};
}

} // namespace mvdemo
} // namespace grove
