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
};

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
    const float a  = attenuation(std::sqrt(dx * dx + dy * dy), l.radius) * l.intensity;
    outR = l.r * a;
    outG = l.g * a;
    outB = l.b * a;
}

} // namespace light
} // namespace grove
