#pragma once

/**
 * grove::anim::Easing — easing curves (animation system, slice 2a).
 *
 * WHAT  : The single, standalone abstraction for ALL interpolation shaping. applyEasing(curve,
 *         t) reshapes a normalized t in [0,1]; ease(curve, a, b, t) is the generic value
 *         interpolation that tracks use. A curve is just a [0,1]->[0,1] function.
 *
 * WHY   : Decoupling the CURVE from the track is what keeps the animation system modular and
 *         clean: Track/Clip do a generic lerp and know NOTHING about curves; adding a curve is
 *         one enum value + one case HERE, never a switch edit elsewhere. It is also reusable
 *         outside animation (UI/camera tweens) — the same role grove::camera::damp plays for
 *         exponential smoothing.
 *
 * HOW   : Pure std-only, no allocation, value-type enum (serializable). All curves are
 *         normalized — every curve maps 0->0 and 1->1, and t is clamped to [0,1]. Cubic/quad
 *         forms are written without pow() to stay dependency-free and cheap.
 *
 * EXTEND: add a value to Easing + a case in applyEasing(). Nothing else changes. (A custom
 *         Bezier-with-control-points variant can be added the same way when needed.)
 */

namespace grove {
namespace anim {

enum class Easing {
    Step,        // hold the start value, jump to the end at the very end of the segment
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
};

// Reshape a normalized progress t in [0,1] according to the curve. t is clamped.
inline float applyEasing(Easing curve, float t) {
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;

    switch (curve) {
        case Easing::Step:
            return (t >= 1.0f) ? 1.0f : 0.0f;
        case Easing::Linear:
            return t;
        case Easing::InQuad:
            return t * t;
        case Easing::OutQuad: {
            const float f = 1.0f - t;
            return 1.0f - f * f;
        }
        case Easing::InOutQuad:
            if (t < 0.5f) return 2.0f * t * t;
            else { const float f = -2.0f * t + 2.0f; return 1.0f - f * f * 0.5f; }
        case Easing::InCubic:
            return t * t * t;
        case Easing::OutCubic: {
            const float f = 1.0f - t;
            return 1.0f - f * f * f;
        }
        case Easing::InOutCubic:
            if (t < 0.5f) return 4.0f * t * t * t;
            else { const float f = -2.0f * t + 2.0f; return 1.0f - f * f * f * 0.5f; }
    }
    return t;  // unreachable; keeps the compiler happy
}

// Interpolate from a to b by a normalized t reshaped through the curve. The single call a
// track makes — it stays oblivious to which curve it's using.
inline float ease(Easing curve, float a, float b, float t) {
    return a + (b - a) * applyEasing(curve, t);
}

} // namespace anim
} // namespace grove
