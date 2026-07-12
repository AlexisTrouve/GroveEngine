/**
 * Unit Tests: grove::anim::AnimationPlayer (animation system, slice 3).
 *
 * WHAT  : Locks the clip playback driver — play/pause/resume/stop, loop, speed, and dt-based
 *         time advance. update(dt, hierarchy) advances the clock and writes the sampled clip
 *         values into the node LOCAL transforms (the caller composes world once via
 *         Hierarchy::update()).
 *
 * WHY    : Clip::apply is a pure function of absolute time; the Player adds the time model that
 *          makes a clip actually "play". Perf posture: the player holds a const Clip* (shared,
 *          never copied) so many animated instances reuse one clip's keyframe data with zero
 *          per-instance copy and zero per-frame allocation.
 *
 * HOW    : Pure, headless. The player writes locals only; world composition stays the
 *          hierarchy's job so one update() serves many players.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/anim/AnimationPlayer.h"

using namespace grove::anim;
using Catch::Matchers::WithinAbs;

// A clip that drives node 0's TranslationX from 0 to 100 over 1 second (linear).
static Clip makeXClip() {
    Clip c;
    c.duration = 1.0f;
    Track t;
    t.nodeId = 0;
    t.property = Property::TranslationX;
    t.keys = { {0.0f, 0.0f, Easing::Linear}, {1.0f, 100.0f, Easing::Linear} };
    c.tracks.push_back(t);
    return c;
}

TEST_CASE("AnimationPlayer - one-shot plays, applies, and finishes at the end", "[anim][player][unit]") {
    Clip clip = makeXClip();
    Hierarchy h; h.addNode(-1, Transform2D{});

    AnimationPlayer p;
    p.play(&clip, /*loop*/ false);
    REQUIRE(p.isPlaying());

    p.update(0.5f, h);
    REQUIRE_THAT(p.time(), WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(h.local(0).x, WithinAbs(50.0f, 0.001f));

    p.update(0.5f, h);                       // reaches the end
    REQUIRE_THAT(h.local(0).x, WithinAbs(100.0f, 0.001f));
    REQUIRE_FALSE(p.isPlaying());            // one-shot finished

    p.update(1.0f, h);                       // no-op once finished
    REQUIRE_THAT(h.local(0).x, WithinAbs(100.0f, 0.001f));
}

TEST_CASE("AnimationPlayer - setSpeed / setLoop / clip take effect", "[anim][player][unit]") {
    Clip clip = makeXClip();                 // duration 1.0, X: 0 -> 100
    Hierarchy h; h.addNode(-1, Transform2D{});

    AnimationPlayer p;
    p.play(&clip, /*loop*/ true, /*speed*/ 1.0f);
    REQUIRE(p.clip() == &clip);              // clip() returns the currently-playing clip

    // setSpeed doubles the advance rate: 0.25 dt * speed 2 = 0.5 time.
    p.setSpeed(2.0f);
    p.update(0.25f, h);
    REQUIRE_THAT(p.time(), WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(h.local(0).x, WithinAbs(50.0f, 0.001f));

    // setLoop(false): overrunning the end now clamps + stops instead of wrapping.
    p.setLoop(false);
    p.update(1.0f, h);                       // 0.5 + 1.0*2 overruns duration -> clamp to end, stop
    REQUIRE_THAT(h.local(0).x, WithinAbs(100.0f, 0.001f));
    REQUIRE_FALSE(p.isPlaying());
}

TEST_CASE("AnimationPlayer - loop wraps the time", "[anim][player][unit]") {
    Clip clip = makeXClip();
    Hierarchy h; h.addNode(-1, Transform2D{});

    AnimationPlayer p;
    p.play(&clip, /*loop*/ true);

    p.update(0.75f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(75.0f, 0.001f));

    p.update(0.5f, h);                       // 1.25 -> wraps to 0.25
    REQUIRE(p.isPlaying());
    REQUIRE_THAT(p.time(), WithinAbs(0.25f, 0.001f));
    REQUIRE_THAT(h.local(0).x, WithinAbs(25.0f, 0.001f));
}

TEST_CASE("AnimationPlayer - speed scales the advance", "[anim][player][unit]") {
    Clip clip = makeXClip();
    Hierarchy h; h.addNode(-1, Transform2D{});

    AnimationPlayer p;
    p.play(&clip, /*loop*/ false, /*speed*/ 2.0f);
    p.update(0.25f, h);                      // 0.25 * 2 = 0.5
    REQUIRE_THAT(p.time(), WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(h.local(0).x, WithinAbs(50.0f, 0.001f));
}

TEST_CASE("AnimationPlayer - pause freezes, resume continues", "[anim][player][unit]") {
    Clip clip = makeXClip();
    Hierarchy h; h.addNode(-1, Transform2D{});

    AnimationPlayer p;
    p.play(&clip, false);
    p.update(0.3f, h);
    REQUIRE_THAT(h.local(0).x, WithinAbs(30.0f, 0.001f));

    p.pause();
    REQUIRE_FALSE(p.isPlaying());
    p.update(1.0f, h);                       // frozen
    REQUIRE_THAT(p.time(), WithinAbs(0.3f, 0.001f));
    REQUIRE_THAT(h.local(0).x, WithinAbs(30.0f, 0.001f));

    p.resume();
    REQUIRE(p.isPlaying());
    p.update(0.2f, h);                       // 0.3 + 0.2 = 0.5
    REQUIRE_THAT(h.local(0).x, WithinAbs(50.0f, 0.001f));
}

TEST_CASE("AnimationPlayer - stop resets and goes idle", "[anim][player][unit]") {
    Clip clip = makeXClip();
    Hierarchy h; h.addNode(-1, Transform2D{});

    AnimationPlayer p;
    p.play(&clip, true);
    p.update(0.5f, h);
    p.stop();
    REQUIRE_FALSE(p.isPlaying());
    REQUIRE_THAT(p.time(), WithinAbs(0.0f, 0.001f));

    p.update(1.0f, h);                       // idle -> no-op
    REQUIRE_THAT(p.time(), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("AnimationPlayer - many players share one const clip with independent state", "[anim][player][unit]") {
    // High-perf shape: the clip is immutable shared data; players are tiny per-instance cursors.
    Clip clip = makeXClip();
    Hierarchy h1; h1.addNode(-1, Transform2D{});
    Hierarchy h2; h2.addNode(-1, Transform2D{});

    AnimationPlayer p1, p2;
    p1.play(&clip, false);
    p2.play(&clip, false);

    p1.update(0.25f, h1);
    p2.update(0.75f, h2);

    REQUIRE_THAT(h1.local(0).x, WithinAbs(25.0f, 0.001f));
    REQUIRE_THAT(h2.local(0).x, WithinAbs(75.0f, 0.001f));   // same clip, independent times
}
