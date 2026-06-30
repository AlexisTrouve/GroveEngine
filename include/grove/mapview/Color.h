#pragma once

/**
 * grove::mapview::Color — the RGBA value type used by the recipe system (S1c).
 *
 * WHAT  : Rgba — four normalized 0..1 channels — plus a linear lerp. The colour a cell is tinted with.
 *
 * WHY   : Matches the renderer's per-instance tint (SpriteInstance carries float r,g,b,a), so continuous
 *         colour is FREE on the bulk path — the palette computes a colour CPU-side and it becomes the tint,
 *         no shader, no banding (mapview.md §5/§6). Normalized 0..1 keeps palettes resolution-independent.
 *
 * HOW   : Header-only, std-only, float channels. lerp is component-wise; callers clamp t as needed.
 */

#include <cstdint>

namespace grove {
namespace mapview {

struct Rgba {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};
};

// Component-wise linear interpolation a->b at t (t is assumed already in [0,1]).
inline Rgba lerp(const Rgba& a, const Rgba& b, float t) {
    return Rgba{a.r + (b.r - a.r) * t,
                a.g + (b.g - a.g) * t,
                a.b + (b.b - a.b) * t,
                a.a + (b.a - a.a) * t};
}

// Scale the RGB channels by k (alpha untouched) — used to apply a hillshade illumination factor to a colour.
inline Rgba multiplyRgb(const Rgba& c, float k) {
    return Rgba{c.r * k, c.g * k, c.b * k, c.a};
}

} // namespace mapview
} // namespace grove
