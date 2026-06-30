/**
 * Unit Tests: grove::mapview region & marker overlays (map-viewer engine, slice S1i).
 *
 * WHAT  : Locks the vector-overlay half of the recipe (§5): RegionLayer (circles styled by type/value,
 *         filled disc or ring) and MarkerLayer (points styled by kind, scaled, rotated). Drives them through
 *         MapView.setRegions/setMarkers -> update() -> drainRegions/drainMarkers, asserting cull, style,
 *         filter, and the emitted RegionDraw/MarkerDraw geometry.
 *
 * WHY    : Per-cell colour shows a field; a worldgen's structure (plates, zones, drift) is vector overlays.
 *         This is what lets a lens show the interesting parts of a generated world. Overlays are global (not
 *         chunked), so the core only culls + styles them — locked here headless.
 *
 * HOW    : Catch2; an empty chunk provider (overlays need no field data), a valid GridSpec so update() runs.
 */

#include <cmath>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/mapview/ChunkCache.h"      // ChunkCoordHash
#include "grove/mapview/ChunkProvider.h"
#include "grove/mapview/GridLayout.h"
#include "grove/mapview/MapView.h"
#include "grove/mapview/Projection.h"

using namespace grove::mapview;
using Catch::Matchers::WithinAbs;

// Overlays don't need field data — an empty provider is enough.
struct EmptyProvider final : IChunkProvider {
    bool has(ChunkCoord) const override { return false; }
    ChunkData load(ChunkCoord c) override { ChunkData d; d.coord = c; return d; }
};

static const Rgba kRed{1, 0, 0, 1}, kGreen{0, 1, 0, 1}, kBlue{0, 0, 1, 1};

static MapView makeView(EmptyProvider& provider, SquareLayout& layout, TopDownProjection& proj) {
    const std::vector<FieldDecl> schema = {FieldDecl{"x", Encoding::Uint, 8}};
    MapView mv(schema, GridSpec{1, 1, 1, 1.0, 1.0}, layout, proj, provider, 8);
    mv.setViewport(Viewport{0.0, 0.0, 100.0, 100.0});
    return mv;
}

TEST_CASE("mapview S1i - region layer emits styled discs, culled to the viewport", "[mapview][overlays][unit]") {
    EmptyProvider provider; SquareLayout layout(1, 1); TopDownProjection proj;
    MapView mv = makeView(provider, layout, proj);

    mv.setRegions({
        Region{10.0, 10.0, 5.0, 1, 0.0},     // inside, type 1 -> green
        Region{50.0, 50.0, 8.0, 0, 0.0},     // inside, type 0 -> red
        Region{500.0, 500.0, 3.0, 2, 0.0},   // far outside -> culled
        Region{-2.0, 50.0, 4.0, 2, 0.0},     // centre off-screen but circle overlaps x=0 -> kept
    });
    mv.setLens(Lens{"plates", {}, {RegionLayer{Palette::categorical({kRed, kGreen, kBlue}), false, 0.0, 7, 1.0f}}, {}});
    mv.update();

    REQUIRE(mv.regionDraws().size() == 3);                 // far one culled, partial one kept
    // First region: filled green disc at (10,10), r0=0, r1=5, full circle, layer 7.
    const RegionDraw& d = mv.regionDraws()[0];
    REQUIRE_THAT(d.cx, WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(d.cy, WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(d.r0, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(d.r1, WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(d.a1, WithinAbs(kTwoPi, 1e-9));
    REQUIRE(d.layer == 7);
    REQUIRE(d.color.g == 1.0f);                            // type 1 -> green
}

TEST_CASE("mapview S1i - region ring (innerRatio) and value styling", "[mapview][overlays][unit]") {
    EmptyProvider provider; SquareLayout layout(1, 1); TopDownProjection proj;
    MapView mv = makeView(provider, layout, proj);

    mv.setRegions({Region{50.0, 50.0, 10.0, 0, 50.0}});   // value 50
    // Ring (inner 60% of radius) coloured by VALUE on a black->white ramp.
    mv.setLens(Lens{"v", {}, {RegionLayer{Palette::ramp({{0.0, Rgba{0, 0, 0, 1}}, {100.0, Rgba{1, 1, 1, 1}}}),
                                          true, 0.6, 0, 1.0f}}, {}});
    mv.update();

    REQUIRE(mv.regionDraws().size() == 1);
    const RegionDraw& d = mv.regionDraws()[0];
    REQUIRE_THAT(d.r0, WithinAbs(6.0, 1e-9));              // innerRatio 0.6 * radius 10
    REQUIRE_THAT(d.r1, WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(d.color.r, WithinAbs(0.5f, 1e-5));        // value 50 -> grey 0.5
}

TEST_CASE("mapview S1i - region filter drops non-matching types", "[mapview][overlays][unit]") {
    EmptyProvider provider; SquareLayout layout(1, 1); TopDownProjection proj;
    MapView mv = makeView(provider, layout, proj);

    mv.setRegions({Region{10, 10, 2, 0, 0}, Region{20, 20, 2, 1, 0}, Region{30, 30, 2, 1, 0}});
    // Only type == 1.
    mv.setLens(Lens{"oceanic", {}, {RegionLayer{Palette::categorical({kRed, kGreen}), false, 0.0, 0, 1.0f,
                                                Filter::cmp(Filter::Op::Eq, 1.0)}}, {}});
    mv.update();
    REQUIRE(mv.regionDraws().size() == 2);                // the two type-1 regions
}

TEST_CASE("mapview S1i - marker layer emits placed, scaled, rotated, tinted sprites", "[mapview][overlays][unit]") {
    EmptyProvider provider; SquareLayout layout(1, 1); TopDownProjection proj;
    MapView mv = makeView(provider, layout, proj);

    mv.setMarkers({
        Marker{25.0, 25.0, 1, 0.5, 2.0},     // inside, kind 1 -> green, angle 0.5, scale 2
        Marker{200.0, 25.0, 0, 0.0, 1.0},    // outside -> culled
    });
    // baseScale 3 -> emitted scale = marker.scale * 3.
    mv.setLens(Lens{"cities", {}, {}, {MarkerLayer{Palette::categorical({kRed, kGreen}), 3.0, 9, 1.0f}}});
    mv.update();

    REQUIRE(mv.markerDraws().size() == 1);
    const MarkerDraw& m = mv.markerDraws()[0];
    REQUIRE_THAT(m.x, WithinAbs(25.0, 1e-9));
    REQUIRE_THAT(m.y, WithinAbs(25.0, 1e-9));
    REQUIRE_THAT(m.scale, WithinAbs(6.0, 1e-9));          // 2.0 * baseScale 3.0
    REQUIRE_THAT(m.rotation, WithinAbs(0.5, 1e-9));       // the marker's angle
    REQUIRE(m.layer == 9);
    REQUIRE(m.color.g == 1.0f);                           // kind 1 -> green
}

TEST_CASE("mapview S1i - marker filter by kind", "[mapview][overlays][unit]") {
    EmptyProvider provider; SquareLayout layout(1, 1); TopDownProjection proj;
    MapView mv = makeView(provider, layout, proj);

    mv.setMarkers({Marker{10, 10, 0, 0, 1}, Marker{20, 20, 5, 0, 1}, Marker{30, 30, 5, 0, 1}});
    mv.setLens(Lens{"k", {}, {}, {MarkerLayer{Palette::categorical({kRed}), 1.0, 0, 1.0f,
                                              Filter::cmp(Filter::Op::Eq, 5.0)}}});
    mv.update();
    REQUIRE(mv.markerDraws().size() == 2);               // the two kind-5 markers
}

TEST_CASE("mapview S1i - a lens with no overlay layers emits no overlays (backward compatible)", "[mapview][overlays][unit]") {
    EmptyProvider provider; SquareLayout layout(1, 1); TopDownProjection proj;
    MapView mv = makeView(provider, layout, proj);
    mv.setRegions({Region{10, 10, 5, 0, 0}});
    mv.setMarkers({Marker{10, 10, 0, 0, 1}});
    mv.setLens(Lens{"fields-only", {}});                 // no region/marker layers
    mv.update();
    REQUIRE(mv.regionDraws().empty());
    REQUIRE(mv.markerDraws().empty());
}
