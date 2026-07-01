#pragma once

/**
 * grove::mapview::Hillshade — fake-3D relief shading from a field's gradient (S1e, the §5 modifier).
 *
 * WHAT  : Given the local gradient (dz/dx, dz/dy) of a scalar field (typically elevation), returns an
 *         illumination factor in [0,1] for a directional light. A Layer multiplies its palette colour by
 *         this factor → relief "pops" in 2D, no shader, no 3D (mapview.md §5).
 *
 * WHY   : Relief is the single most legible cue on a top-down map — it turns a flat elevation ramp into a
 *         readable landscape. Keeping it a pure function of the gradient (the orchestrator samples the
 *         neighbours and computes the gradient) makes the shading model itself trivially testable.
 *
 * HOW   : Header-only, std-only. Lambertian-lit normal N = normalize(-zFactor·dz/dx, -zFactor·dz/dy, 1); the
 *         raw shading is the dot d = N·L with a unit light L (d ∈ [-1,1]). We then map d → brightness with a
 *         SOFT wrap, not a hard clamp:
 *             factor = ambient + (1-ambient) · (0.5 + 0.5·d)²         ("half-Lambert", floored by ambient)
 *         WHY soft: the old model did `max(0, d)` — a hard clamp that (a) put a crease at the terminator
 *         (d=0) and (b) crushed EVERY away-slope to identical pure black, so a whole hillside read as one flat
 *         black blob (Alexi: "des shades SUPER chelou"). That's the doctrine's "borner dur écrase l'info". The
 *         half-Lambert wrap is smooth and MONOTONIC across the full [-1,1], so away-slopes stay distinct
 *         (steeper-away = darker, but still readable), and the ambient floor means nothing is ever pure black.
 *         Flat ground under overhead light still returns 1; a directional light still brightens the slope
 *         facing it. Construct from a light direction directly, or from azimuth/altitude. zFactor exaggerates
 *         the relief; ambient sets the darkest shadow (0 = allow pure black, ~0.3 = legible relief default).
 */

#include <cmath>

namespace grove {
namespace mapview {

class Hillshade {
public:
    // Default darkest-shadow floor: relief stays legible (no pure black) while keeping decent contrast.
    static constexpr double kDefaultAmbient = 0.30;

    // Light direction (need not be normalized) + vertical exaggeration of the relief + ambient shadow floor.
    Hillshade(double lx, double ly, double lz, double zFactor = 1.0, double ambient = kDefaultAmbient)
        : zf_(zFactor), ambient_(ambient < 0.0 ? 0.0 : (ambient > 1.0 ? 1.0 : ambient)) {
        const double n = std::sqrt(lx * lx + ly * ly + lz * lz);
        const double inv = n > 0.0 ? 1.0 / n : 0.0;
        lx_ = lx * inv;
        ly_ = ly * inv;
        lz_ = lz * inv;
    }

    // Light from a compass-like azimuth (radians; math frame: 0 = +x, CCW) at an altitude above the horizon.
    static Hillshade fromAzimuthAltitude(double azimuthRad, double altitudeRad, double zFactor = 1.0,
                                         double ambient = kDefaultAmbient) {
        const double cz = std::cos(altitudeRad);
        return Hillshade(cz * std::cos(azimuthRad), cz * std::sin(azimuthRad), std::sin(altitudeRad),
                         zFactor, ambient);
    }

    // Illumination factor in [ambient, 1] for the surface gradient (dz/dx, dz/dy). Smooth + monotonic in the
    // slope: no terminator crease, no crushed-to-black away-slopes.
    double factor(double dzdx, double dzdy) const {
        const double nx = -zf_ * dzdx;
        const double ny = -zf_ * dzdy;
        const double nz = 1.0;
        const double inv = 1.0 / std::sqrt(nx * nx + ny * ny + nz * nz);
        double d = (nx * lx_ + ny * ly_ + nz * lz_) * inv;
        // A non-finite gradient (NaN nodata sentinel, or Inf from a zero cell pitch) would poison the colour.
        // Fold it to the ambient floor (a degenerate cell reads as flat shadow, never NaN-garbage or pure black).
        if (!std::isfinite(d)) return ambient_;
        if (d < -1.0) d = -1.0;                 // mathematical bounds of a normalized dot (guards FP drift only)
        if (d > 1.0) d = 1.0;
        const double lit = 0.5 + 0.5 * d;        // half-Lambert wrap: [-1,1] -> [0,1], smooth + monotonic
        return ambient_ + (1.0 - ambient_) * lit * lit;   // squared for contrast; ambient floor -> never black
    }

    // The darkest-shadow floor this instance uses (tests/host introspection).
    double ambient() const { return ambient_; }

private:
    double lx_{0.0};
    double ly_{0.0};
    double lz_{1.0};
    double zf_{1.0};
    double ambient_{kDefaultAmbient};
};

} // namespace mapview
} // namespace grove
