/**
 * Unit Tests: grove::anim Keyframe / Track / Clip (animation system, slice 2b).
 *
 * WHAT  : Locks keyframe sampling and clip application. A Track animates ONE property of ONE
 *         node via keyframes; Track::sample(time) interpolates (clamping outside the range)
 *         using the per-key Easing curve. Clip::apply(time, hierarchy) writes every track's
 *         sampled value into the node LOCAL transforms — then the hierarchy composes world.
 *
 * WHY    : This is the data + sampling layer of the animation system, built on the decoupled
 *          Easing abstraction (slice 2a) and the Transform2D hierarchy (slice 1). Tracks do a
 *          generic ease()/property write and know nothing about curve internals — modular.
 *
 * HOW    : Pure, headless. Time is in seconds; the player (slice 3) owns loop/speed/dt. Here
 *          sampling is a pure function of absolute time.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grove/anim/Clip.h"

using namespace grove::anim;
using Catch::Matchers::WithinAbs;

static constexpr float PI = 3.14159265358979323846f;

TEST_CASE("Track - sample clamps before the first and after the last key", "[anim][clip][unit]") {
    Track t;
    t.nodeId = 0;
    t.property = Property::TranslationX;
    t.keys = { {1.0f, 100.0f, Easing::Linear}, {2.0f, 200.0f, Easing::Linear} };

    REQUIRE_THAT(t.sample(0.0f), WithinAbs(100.0f, 0.001f));  // before first -> first value
    REQUIRE_THAT(t.sample(1.0f), WithinAbs(100.0f, 0.001f));
    REQUIRE_THAT(t.sample(2.0f), WithinAbs(200.0f, 0.001f));
    REQUIRE_THAT(t.sample(5.0f), WithinAbs(200.0f, 0.001f));  // after last -> last value
}

TEST_CASE("Track - linear interpolation between two keys", "[anim][clip][unit]") {
    Track t;
    t.property = Property::Rotation;
    t.keys = { {0.0f, 0.0f, Easing::Linear}, {2.0f, 10.0f, Easing::Linear} };

    REQUIRE_THAT(t.sample(0.5f), WithinAbs(2.5f, 0.001f));
    REQUIRE_THAT(t.sample(1.0f), WithinAbs(5.0f, 0.001f));
    REQUIRE_THAT(t.sample(1.5f), WithinAbs(7.5f, 0.001f));
}

TEST_CASE("Track - the key's easing governs its outgoing segment", "[anim][clip][unit]") {
    Track t;
    t.property = Property::TranslationY;
    // Step on the first key: value holds at 0 across the whole segment, jumps at the end.
    t.keys = { {0.0f, 0.0f, Easing::Step}, {1.0f, 100.0f, Easing::Linear} };
    REQUIRE_THAT(t.sample(0.5f), WithinAbs(0.0f, 0.001f));   // step holds
    REQUIRE_THAT(t.sample(0.99f), WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(t.sample(1.0f), WithinAbs(100.0f, 0.001f)); // reaches the next key

    // InQuad on the first key: at half-time, 0.25 of the way.
    t.keys = { {0.0f, 0.0f, Easing::InQuad}, {1.0f, 100.0f, Easing::Linear} };
    REQUIRE_THAT(t.sample(0.5f), WithinAbs(25.0f, 0.001f));
}

TEST_CASE("Track - empty track is harmless", "[anim][clip][unit]") {
    Track t;
    REQUIRE_THAT(t.sample(1.0f), WithinAbs(0.0f, 0.001f));
}

TEST_CASE("propertyRef - writes the right Transform2D field", "[anim][clip][unit]") {
    Transform2D tr;
    propertyRef(tr, Property::TranslationX) = 11.0f;
    propertyRef(tr, Property::TranslationY) = 22.0f;
    propertyRef(tr, Property::Rotation)     = 0.5f;
    propertyRef(tr, Property::ScaleX)       = 3.0f;
    propertyRef(tr, Property::ScaleY)       = 4.0f;
    REQUIRE_THAT(tr.x, WithinAbs(11.0f, 0.001f));
    REQUIRE_THAT(tr.y, WithinAbs(22.0f, 0.001f));
    REQUIRE_THAT(tr.rotation, WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(tr.scaleX, WithinAbs(3.0f, 0.001f));
    REQUIRE_THAT(tr.scaleY, WithinAbs(4.0f, 0.001f));
}

TEST_CASE("Clip - apply writes node locals; hierarchy composes the world", "[anim][clip][unit]") {
    // Parent at (100,100) with a child 50 to its right.
    Hierarchy h;
    int parent = h.addNode(-1, Transform2D{100.0f, 100.0f, 0.0f, 1.0f, 1.0f});
    int child  = h.addNode(parent, Transform2D{50.0f, 0.0f, 0.0f, 1.0f, 1.0f});

    // Clip rotates the parent from 0 to +90deg over 1 second.
    Clip clip;
    clip.duration = 1.0f;
    Track rot;
    rot.nodeId = parent;
    rot.property = Property::Rotation;
    rot.keys = { {0.0f, 0.0f, Easing::Linear}, {1.0f, PI / 2.0f, Easing::Linear} };
    clip.tracks.push_back(rot);

    // t=0: no rotation -> child world at (150,100).
    clip.apply(0.0f, h);
    h.update();
    REQUIRE_THAT(h.world(child).x, WithinAbs(150.0f, 0.001f));
    REQUIRE_THAT(h.world(child).y, WithinAbs(100.0f, 0.001f));

    // t=1: parent rotated +90deg -> child swings to (100,150).
    clip.apply(1.0f, h);
    h.update();
    REQUIRE_THAT(h.world(child).x, WithinAbs(100.0f, 0.001f));
    REQUIRE_THAT(h.world(child).y, WithinAbs(150.0f, 0.001f));
    REQUIRE_THAT(h.local(parent).rotation, WithinAbs(PI / 2.0f, 0.001f));
}

TEST_CASE("Clip - multiple tracks animate independent properties", "[anim][clip][unit]") {
    Hierarchy h;
    int n = h.addNode(-1, Transform2D{});

    Clip clip;
    clip.duration = 2.0f;
    Track tx; tx.nodeId = n; tx.property = Property::TranslationX;
    tx.keys = { {0.0f, 0.0f, Easing::Linear}, {2.0f, 200.0f, Easing::Linear} };
    Track sy; sy.nodeId = n; sy.property = Property::ScaleY;
    sy.keys = { {0.0f, 1.0f, Easing::Linear}, {2.0f, 3.0f, Easing::Linear} };
    clip.tracks.push_back(tx);
    clip.tracks.push_back(sy);

    clip.apply(1.0f, h);   // halfway
    h.update();
    REQUIRE_THAT(h.local(n).x, WithinAbs(100.0f, 0.001f));      // 0->200 @ .5
    REQUIRE_THAT(h.local(n).scaleY, WithinAbs(2.0f, 0.001f));   // 1->3 @ .5
}
