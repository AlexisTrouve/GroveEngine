#pragma once

// ============================================================================
// Light.h — pure 2D light math (grove::light), header-only.
//
// QUOI  : the attenuation curve of a radial light and the world-space box its quad covers. No
//         renderer, no IIO, no GPU — just the geometry, like grove::ui::computeNineSlice or
//         grove::text::fitLine.
//
// POURQUOI: the falloff is evaluated PER PIXEL in the fragment shader, where nothing can be unit
//         tested. Keeping the same formula here as a plain function does two things: it gives the
//         shader an oracle to be checked against, and it lets a GAME ask the question the renderer
//         answers visually — "how lit is this point?" — for gameplay (stealth, visibility, spawn
//         rules) without reading back a texture.
//
// COMMENT: the curve is a SQUARED linear falloff, `(1 - d/r)^2`. Plain linear reads as a hard-edged
//         disc because the eye is sensitive to the derivative discontinuity at the rim; inverse
//         square is physically right but never reaches zero, so a light would tint the whole screen
//         and every light would cost a full-screen quad. The squared-linear compromise is smooth at
//         the rim AND exactly zero at r — which is what makes the quad bound below correct.
// ============================================================================

#include <cmath>

namespace grove {
namespace light {

// A radial light in WORLD space. cx,cy is the CENTRE (engine anchor convention: the field name
// carries the anchor — see docs/design/render-anchor-convention.md).
struct Light2D {
    float cx = 0.0f;
    float cy = 0.0f;
    float radius = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;   // colour, 0..1
    float intensity = 1.0f;               // multiplies the colour; >1 is allowed (RGBA16F target)

    // CONE (L3). Convention borrowed from grove::fx::Emitter — degrees, 0 = +x, 90 = +y
    // (screen-down), 360 = omni — and NOT from render:sector's a0/a1 radians. The reason is
    // practical: a thruster's flame emitter and the light it casts then take THE SAME NUMBERS, so a
    // game authors the cone once instead of converting between two conventions.
    //
    // The default 360 IS the non-regression: every light written before L3 stays a full disc.
    float dirDeg = 0.0f;                  // cone axis
    float spreadDeg = 360.0f;             // FULL cone width (360 = omni)
};

// Fraction of the half-angle over which the rim fades. A hard cut reads as a cardboard pie slice —
// the eye catches the discontinuity exactly as it does on a linear radial falloff. Tuning knob, not
// dogma: exposing it per-light is a follow-on if anyone asks for a hard-edged spotlight.
constexpr float kConeSoftFraction = 0.35f;

/// Angular mask of a cone light, evaluated on a direction FROM the light.
///
/// Takes a direction vector rather than an angle on purpose: the whole thing reduces to a dot
/// product against the cone axis, so neither this nor the shader ever calls atan2. It also makes
/// omni fall out with no branch — cos(180°) = −1 passes every direction.
///
/// `dx, dy` need not be normalised; a zero vector (exactly on the light) yields 1.
inline float coneFactor(float dx, float dy, const Light2D& l) {
    if (l.spreadDeg >= 360.0f) return 1.0f;   // omni: EXACTLY 1, so nothing pre-L3 shifts by a bit
    if (l.spreadDeg <= 0.0f) return 0.0f;

    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f) return 1.0f;             // the light's own centre is always lit

    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    const float axisX = std::cos(l.dirDeg * kDeg2Rad);
    const float axisY = std::sin(l.dirDeg * kDeg2Rad);

    // Cosine of the angle between this direction and the cone axis.
    const float cosA = (dx * axisX + dy * axisY) / len;

    const float half      = 0.5f * l.spreadDeg * kDeg2Rad;
    const float cosOuter  = std::cos(half);                                  // rim: 0 here
    const float cosInner  = std::cos(half * (1.0f - kConeSoftFraction));     // full brightness inside

    if (cosA >= cosInner) return 1.0f;
    if (cosA <= cosOuter) return 0.0f;

    // smoothstep between the two cosines. Note cosine DECREASES as the angle grows, so the
    // interpolation runs from cosOuter (0) up to cosInner (1) — writing it the other way round
    // would invert the rim and light everything except the cone.
    const float t = (cosA - cosOuter) / (cosInner - cosOuter);
    return t * t * (3.0f - 2.0f * t);
}

/// Attenuation at distance `d` from a light of radius `r`. 1 at the centre, 0 at and beyond the rim.
/// A non-positive radius yields 0 — a light with no extent lights nothing, rather than dividing by zero.
inline float attenuation(float d, float r) {
    if (r <= 0.0f) return 0.0f;
    if (d >= r) return 0.0f;          // EXACTLY zero at the rim: what makes the quad bound sufficient
    const float t = 1.0f - (d / r);
    return t * t;
}

/// World-space box the light's quad must cover: a square of side 2r centred on the light.
/// Beyond it the attenuation is exactly zero, so nothing is lost by not drawing there.
struct Bounds { float x, y, w, h; };   // x,y = top-left CORNER (anchor convention)

inline Bounds bounds(const Light2D& l) {
    return Bounds{ l.cx - l.radius, l.cy - l.radius, 2.0f * l.radius, 2.0f * l.radius };
}

/// Light contribution of `l` at world point (px,py), colour pre-multiplied by intensity+attenuation.
/// This is the CPU mirror of what the fragment shader accumulates — the oracle, and the answer to a
/// game's "is this point lit?".
inline void contribution(const Light2D& l, float px, float py, float& outR, float& outG, float& outB) {
    const float dx = px - l.cx;
    const float dy = py - l.cy;
    // Radial falloff AND the cone mask — the gameplay answer must agree with what the screen shows,
    // or a stealth rule would disagree with the picture the player is looking at.
    const float a  = attenuation(std::sqrt(dx * dx + dy * dy), l.radius)
                   * coneFactor(dx, dy, l) * l.intensity;
    outR = l.r * a;
    outG = l.g * a;
    outB = l.b * a;
}

} // namespace light
} // namespace grove
