/**
 * Unit Tests: grove::anim::Flipbook (animation system, flipbook slice F-b).
 *
 * WHAT  : Locks frame-by-frame playback timing. A Flipbook is an ordered list of sheet cell
 *         indices with a per-frame duration and a loop flag. frameAt(time) returns the sheet
 *         cell shown at a given time; uvAt(time, sheet) resolves it to UVs via SpriteSheet.
 *
 * WHY    : Per-frame durations (with a uniform-fps helper) is the general model — uniform fps
 *          is just the case where all durations are equal — and frame-by-frame animation
 *          routinely holds some frames longer (impact/idle/anticipation). The simple case
 *          stays one call (setFps).
 *
 * HOW    : Pure, headless. The game advances time (its own dt loop); loop wraps via fmod,
 *          one-shot clamps to the last frame.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/anim/Flipbook.h"

using namespace grove::anim;
using Catch::Matchers::WithinAbs;

TEST_CASE("Flipbook - uniform fps cycles frames evenly", "[anim][flipbook][unit]") {
    Flipbook f;
    f.frames = { 10, 11, 12, 13 };
    f.setFps(10.0f);          // 0.1s per frame, total 0.4s
    f.loop = true;

    REQUIRE_THAT(f.totalDuration(), WithinAbs(0.4f, 0.0001f));
    REQUIRE(f.frameAt(0.00f) == 10);
    REQUIRE(f.frameAt(0.05f) == 10);
    REQUIRE(f.frameAt(0.10f) == 11);
    REQUIRE(f.frameAt(0.25f) == 12);
    REQUIRE(f.frameAt(0.39f) == 13);
    // loop: 0.45 -> 0.05 -> frame 0
    REQUIRE(f.frameAt(0.45f) == 10);
}

TEST_CASE("Flipbook - setFps sizes durations to the frame list", "[anim][flipbook][unit]") {
    Flipbook f;
    f.frames = { 0, 1, 2 };
    f.setFps(30.0f);
    REQUIRE(f.durations.size() == 3);
    REQUIRE_THAT(f.durations[0], WithinAbs(1.0f / 30.0f, 0.0001f));
}

TEST_CASE("Flipbook - per-frame durations hold a frame longer", "[anim][flipbook][unit]") {
    Flipbook f;
    f.frames = { 0, 1 };
    f.durations = { 0.3f, 0.1f };   // frame 0 held 3x longer
    f.loop = false;

    REQUIRE_THAT(f.totalDuration(), WithinAbs(0.4f, 0.0001f));
    REQUIRE(f.frameAt(0.00f) == 0);
    REQUIRE(f.frameAt(0.20f) == 0);   // still in the long frame
    REQUIRE(f.frameAt(0.31f) == 1);   // moved on
    REQUIRE(f.frameAt(0.39f) == 1);
}

TEST_CASE("Flipbook - one-shot clamps to the last frame", "[anim][flipbook][unit]") {
    Flipbook f;
    f.frames = { 5, 6, 7 };
    f.setFps(10.0f);
    f.loop = false;

    REQUIRE(f.frameAt(5.0f) == 7);    // past the end -> last frame
    REQUIRE(f.frameAt(-1.0f) == 5);   // before start -> first frame
}

TEST_CASE("Flipbook - uvAt resolves the current frame through the sheet", "[anim][flipbook][unit]") {
    SpriteSheet sheet; sheet.columns = 4; sheet.rows = 4;
    Flipbook f;
    f.frames = { 5 };          // a single frame: cell 5
    f.setFps(1.0f);

    float u0, v0, u1, v1;
    f.uvAt(0.0f, sheet, u0, v0, u1, v1);
    REQUIRE_THAT(u0, WithinAbs(0.25f, 0.0001f));   // cell 5 = col1,row1
    REQUIRE_THAT(v0, WithinAbs(0.25f, 0.0001f));
    REQUIRE_THAT(u1, WithinAbs(0.5f, 0.0001f));
    REQUIRE_THAT(v1, WithinAbs(0.5f, 0.0001f));
}

TEST_CASE("Flipbook - empty flipbook is harmless", "[anim][flipbook][unit]") {
    Flipbook f;
    REQUIRE(f.frameAt(1.0f) == 0);
    REQUIRE_THAT(f.totalDuration(), WithinAbs(0.0f, 0.0001f));
}
