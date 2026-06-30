/**
 * Unit Tests: grove::mapview::render::SpriteAdapter (map-viewer engine, slice P1).
 *
 * WHAT  : Locks the CellDraw -> SpriteInstance mapping — the one renderer-coupled bridge of the map viewer.
 *         Verifies every field is carried correctly (centre, size, rotation, layer, tint) and that a solid
 *         cell uses the built-in white texture (textureId 0, full UV).
 *
 * WHY    : A wrong field here mis-places, mis-sizes, or mis-colours every cell on screen — and it would only
 *          show up at the GPU. Locking it headless (FramePacket.h is a plain struct) means the visual demo
 *          only has to verify the GPU path, not the mapping.
 *
 * HOW    : Catch2, headless (no bgfx). Float fields checked with WithinAbs.
 */

#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "MapView/SpriteAdapter.h"

using namespace grove::mapview;
using namespace grove::mapview::render;
using Catch::Matchers::WithinAbs;

TEST_CASE("mapview P1 - CellDraw maps 1:1 to a solid-colour SpriteInstance", "[mapview][adapter][unit]") {
    const CellDraw c{12.5, -7.25, 2.0, 3.0, 0.5, 1000, Rgba{0.1f, 0.2f, 0.3f, 0.4f}};
    const grove::SpriteInstance s = toSpriteInstance(c);

    REQUIRE_THAT(s.x, WithinAbs(12.5f, 1e-5f));        // world-space centre, carried straight through
    REQUIRE_THAT(s.y, WithinAbs(-7.25f, 1e-5f));
    REQUIRE_THAT(s.scaleX, WithinAbs(2.0f, 1e-5f));    // size multipliers = cell size
    REQUIRE_THAT(s.scaleY, WithinAbs(3.0f, 1e-5f));
    REQUIRE_THAT(s.rotation, WithinAbs(0.5f, 1e-5f));
    REQUIRE(s.layer == 1000.0f);

    // Solid colour: built-in white texture (id 0) + full texel.
    REQUIRE(s.textureId == 0.0f);
    REQUIRE(s.u0 == 0.0f); REQUIRE(s.v0 == 0.0f);
    REQUIRE(s.u1 == 1.0f); REQUIRE(s.v1 == 1.0f);

    // Tint (0..1) carried verbatim.
    REQUIRE(s.r == 0.1f); REQUIRE(s.g == 0.2f); REQUIRE(s.b == 0.3f); REQUIRE(s.a == 0.4f);

    // Clip slot zeroed (no UI clipping for map cells).
    REQUIRE(s.reserved[0] == 0.0f);
    REQUIRE(s.reserved[2] == 0.0f);
}

TEST_CASE("mapview P1 - bulk fill writes the caller buffer and returns the count", "[mapview][adapter][unit]") {
    const std::vector<CellDraw> cells = {
        CellDraw{0.0, 0.0, 1.0, 1.0, 0.0, 0, Rgba{1, 0, 0, 1}},
        CellDraw{5.0, 5.0, 1.0, 1.0, 0.0, 2, Rgba{0, 1, 0, 1}},
        CellDraw{-3.0, 4.0, 1.0, 1.0, 0.0, 4, Rgba{0, 0, 1, 1}},
    };
    std::vector<grove::SpriteInstance> out(cells.size());
    const size_t n = toSpriteInstances(cells.data(), cells.size(), out.data());

    REQUIRE(n == 3);
    REQUIRE_THAT(out[1].x, WithinAbs(5.0f, 1e-5f));
    REQUIRE(out[1].g == 1.0f);
    REQUIRE(out[2].layer == 4.0f);
    REQUIRE_THAT(out[2].x, WithinAbs(-3.0f, 1e-5f));
}
