/**
 * VideoSyncUnit — pure oracle for the A/V synchronisation math (video slice 6c).
 *
 * No decoder / renderer: asserts the time->frame decision directly — index from the master clock,
 * "changed" when it moves, "dropped" when the clock jumps ahead (skipped frames are NOT rendered),
 * "ended" fired once past the last frame, and the unknown-length (streaming) case. The E2E (IT_058)
 * proves it end-to-end through the module; this locks the math.
 */

#include <catch2/catch_test_macros.hpp>
#include "VideoSync.h"

using namespace grove::video;

TEST_CASE("VideoSyncUnit: index tracks the clock; changed/dropped/ended are reported", "[video][unit]") {
    VideoSync s;
    s.configure(30.0, 90);   // 30 fps, 90 frames = 3.0 s
    s.reset();

    // t=0 -> frame 0, first tick so it changed.
    FrameTick t0 = s.update(0.0);
    REQUIRE(t0.index == 0);
    REQUIRE(t0.changed);
    REQUIRE(t0.dropped == 0);
    REQUIRE_FALSE(t0.ended);

    // Mid-frame-0: no change (holds the frame).
    FrameTick t0b = s.update(0.02);   // 0.02 * 30 = 0.6 -> floor 0
    REQUIRE(t0b.index == 0);
    REQUIRE_FALSE(t0b.changed);

    // Advance one frame.
    FrameTick t1 = s.update(1.0 / 30.0 + 0.001);
    REQUIRE(t1.index == 1);
    REQUIRE(t1.changed);
    REQUIRE(t1.dropped == 0);

    // Clock jumps to frame 3 -> frame 2 was skipped (dropped == 1), not rendered.
    FrameTick t3 = s.update(0.1);      // 0.1 * 30 = 3
    REQUIRE(t3.index == 3);
    REQUIRE(t3.changed);
    REQUIRE(t3.dropped == 1);

    // Past the end -> hold the last frame, ended fires ONCE.
    FrameTick te = s.update(3.0);      // 3.0 * 30 = 90 -> clamp 89
    REQUIRE(te.index == 89);
    REQUIRE(te.ended);

    FrameTick te2 = s.update(3.5);     // still past end
    REQUIRE(te2.index == 89);
    REQUIRE_FALSE(te2.ended);          // only once
    REQUIRE_FALSE(te2.changed);        // held
    REQUIRE(s.hasEnded());
}

TEST_CASE("VideoSyncUnit: unknown length (frameCount<=0) never ends", "[video][unit]") {
    VideoSync s;
    s.configure(24.0, 0);   // streaming: length unknown
    s.reset();

    FrameTick big = s.update(100.0);   // 100 * 24 = 2400
    REQUIRE(big.index == 2400);
    REQUIRE(big.changed);
    REQUIRE_FALSE(big.ended);
    REQUIRE_FALSE(s.hasEnded());
}

TEST_CASE("VideoSyncUnit: a bad fps falls back to 30", "[video][unit]") {
    VideoSync s;
    s.configure(0.0, 60);   // fps 0 -> 30
    s.reset();
    FrameTick t = s.update(1.0);   // 1.0 * 30 = 30
    REQUIRE(t.index == 30);
}
