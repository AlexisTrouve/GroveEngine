#pragma once

/**
 * grove::anim — Keyframe / Track / Clip (animation system, slice 2b).
 *
 * WHAT  : The animation DATA + sampling layer. A Track animates one PROPERTY of one node via
 *         time-sorted Keyframes; Track::sample(time) interpolates between the bracketing keys
 *         (clamping outside the range) through the per-key Easing curve. A Clip is a set of
 *         tracks + a duration; Clip::apply(time, hierarchy) writes every sampled value into the
 *         node LOCAL transforms, after which Hierarchy::update() composes world transforms.
 *
 * WHY   : This is where keyframed motion lives, built on two decoupled pieces — the Easing
 *         abstraction (slice 2a) and the Transform2D hierarchy (slice 1). Tracks call ease()
 *         and write through propertyRef(); they contain no curve logic and no transform math,
 *         which keeps the layer small and the parts independently testable.
 *
 * HOW   : Pure, headless, std-only. Time is absolute seconds; loop / speed / dt belong to the
 *         AnimationPlayer (slice 3). A key's Easing governs its OUTGOING segment (key i -> i+1).
 */

#include "Easing.h"
#include "Transform2D.h"

#include <cstddef>
#include <vector>

namespace grove {
namespace anim {

// Which scalar field of a node's Transform2D a track drives.
enum class Property { TranslationX, TranslationY, Rotation, ScaleX, ScaleY };

// Resolve a writable reference to the field a Property names. Single place that maps the enum
// to the struct — adding a property is one enum value + one case here.
inline float& propertyRef(Transform2D& t, Property p) {
    switch (p) {
        case Property::TranslationX: return t.x;
        case Property::TranslationY: return t.y;
        case Property::Rotation:     return t.rotation;
        case Property::ScaleX:       return t.scaleX;
        case Property::ScaleY:       return t.scaleY;
    }
    return t.x;  // unreachable
}

struct Keyframe {
    float time = 0.0f;                 // seconds
    float value = 0.0f;
    Easing easing = Easing::Linear;    // governs the segment STARTING at this key
};

// Animates one property of one node. Keys must be sorted by time (ascending).
struct Track {
    int nodeId = 0;
    Property property = Property::TranslationX;
    std::vector<Keyframe> keys;

    // Value at an absolute time. Empty -> 0; before first / after last -> clamped to the end
    // value; otherwise ease() across the bracketing segment using the left key's curve.
    float sample(float time) const {
        if (keys.empty()) return 0.0f;
        if (time <= keys.front().time) return keys.front().value;
        if (time >= keys.back().time)  return keys.back().value;

        for (size_t i = 0; i + 1 < keys.size(); ++i) {
            const Keyframe& k0 = keys[i];
            const Keyframe& k1 = keys[i + 1];
            if (time >= k0.time && time < k1.time) {
                const float span = k1.time - k0.time;
                const float u = (span > 0.0f) ? (time - k0.time) / span : 0.0f;
                return ease(k0.easing, k0.value, k1.value, u);
            }
        }
        return keys.back().value;  // unreachable given the clamps above
    }
};

// A set of tracks with a nominal duration. apply() samples every track and writes the result
// into the node locals; the caller then runs Hierarchy::update() to get world transforms.
class Clip {
public:
    float duration = 0.0f;
    std::vector<Track> tracks;

    void apply(float time, Hierarchy& hierarchy) const {
        for (const Track& track : tracks) {
            if (track.keys.empty()) continue;
            propertyRef(hierarchy.local(track.nodeId), track.property) = track.sample(time);
        }
    }
};

} // namespace anim
} // namespace grove
