/**
 * Unit Tests: grove::anim::SpriteSheet (animation system, flipbook slice F-a).
 *
 * WHAT  : Locks the uniform-grid atlas -> UV mapping. frameUV(index) returns the [0,1] UV
 *         rectangle of cell `index` (row-major) in a columns x rows sprite sheet. This is the
 *         pixel-free foundation under frame-by-frame (flipbook) animation: a flipbook picks a
 *         frame index over time; SpriteSheet turns it into the UVs a render:sprite needs.
 *
 * WHY    : Spritesheet animation = cycling the displayed cell. Keeping the atlas->UV math pure
 *          and standalone (header-only) keeps it testable headless and reusable (static sprites
 *          can pick a cell too), and the Flipbook layer stays oblivious to atlas geometry.
 *
 * HOW    : UV origin top-left, v increasing downward — same convention as TilemapPass.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/anim/SpriteSheet.h"

using namespace grove::anim;
using Catch::Matchers::WithinAbs;

TEST_CASE("SpriteSheet - 4x4 grid maps cells to UV rectangles row-major", "[anim][sheet][unit]") {
    SpriteSheet s;
    s.columns = 4; s.rows = 4;
    REQUIRE(s.frameCount() == 16);

    float u0, v0, u1, v1;

    s.frameUV(0, u0, v0, u1, v1);   // top-left cell
    REQUIRE_THAT(u0, WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(v0, WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(u1, WithinAbs(0.25f, 0.0001f));
    REQUIRE_THAT(v1, WithinAbs(0.25f, 0.0001f));

    s.frameUV(5, u0, v0, u1, v1);   // col 1, row 1
    REQUIRE_THAT(u0, WithinAbs(0.25f, 0.0001f));
    REQUIRE_THAT(v0, WithinAbs(0.25f, 0.0001f));
    REQUIRE_THAT(u1, WithinAbs(0.5f, 0.0001f));
    REQUIRE_THAT(v1, WithinAbs(0.5f, 0.0001f));

    s.frameUV(15, u0, v0, u1, v1);  // last cell (col 3, row 3)
    REQUIRE_THAT(u0, WithinAbs(0.75f, 0.0001f));
    REQUIRE_THAT(v0, WithinAbs(0.75f, 0.0001f));
    REQUIRE_THAT(u1, WithinAbs(1.0f, 0.0001f));
    REQUIRE_THAT(v1, WithinAbs(1.0f, 0.0001f));
}

TEST_CASE("SpriteSheet - 1x1 is the full texture", "[anim][sheet][unit]") {
    SpriteSheet s;  // defaults 1x1
    float u0, v0, u1, v1;
    s.frameUV(0, u0, v0, u1, v1);
    REQUIRE_THAT(u0, WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(v0, WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(u1, WithinAbs(1.0f, 0.0001f));
    REQUIRE_THAT(v1, WithinAbs(1.0f, 0.0001f));
}

TEST_CASE("SpriteSheet - non-square grid (8 cols x 2 rows)", "[anim][sheet][unit]") {
    SpriteSheet s;
    s.columns = 8; s.rows = 2;
    float u0, v0, u1, v1;
    s.frameUV(8, u0, v0, u1, v1);   // first cell of the second row
    REQUIRE_THAT(u0, WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(v0, WithinAbs(0.5f, 0.0001f));
    REQUIRE_THAT(u1, WithinAbs(0.125f, 0.0001f));
    REQUIRE_THAT(v1, WithinAbs(1.0f, 0.0001f));
}

TEST_CASE("SpriteSheet - explicit frameCount caps a partial last row", "[anim][sheet][unit]") {
    SpriteSheet s;
    s.columns = 4; s.rows = 4; s.count = 10;   // only 10 valid frames in a 16-cell grid
    REQUIRE(s.frameCount() == 10);

    float u0, v0, u1, v1;
    s.frameUV(9, u0, v0, u1, v1);   // col 1, row 2
    REQUIRE_THAT(u0, WithinAbs(0.25f, 0.0001f));
    REQUIRE_THAT(v0, WithinAbs(0.5f, 0.0001f));
}

TEST_CASE("SpriteSheet - out-of-range index is clamped, never out of bounds", "[anim][sheet][unit]") {
    SpriteSheet s;
    s.columns = 4; s.rows = 4; s.count = 10;
    float u0, v0, u1, v1;

    s.frameUV(999, u0, v0, u1, v1);   // clamps to last valid frame (9)
    float lu0, lv0, lu1, lv1;
    s.frameUV(9, lu0, lv0, lu1, lv1);
    REQUIRE_THAT(u0, WithinAbs(lu0, 0.0001f));
    REQUIRE_THAT(v0, WithinAbs(lv0, 0.0001f));

    s.frameUV(-5, u0, v0, u1, v1);    // clamps to frame 0
    REQUIRE_THAT(u0, WithinAbs(0.0f, 0.0001f));
    REQUIRE_THAT(v0, WithinAbs(0.0f, 0.0001f));
}
