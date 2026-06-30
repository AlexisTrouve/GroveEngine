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
 * HOW   : Header-only, std-only. Lambertian model — unambiguous, no azimuth/aspect convention traps: the
 *         surface normal is N = normalize(-zFactor·dz/dx, -zFactor·dz/dy, 1); the factor is the clamped dot
 *         product of N with a unit light direction L. Flat ground (gradient 0) therefore returns L.z
 *         (= sin(altitude)); overhead light (0,0,1) returns 1 on the flat and < 1 on any slope. Construct
 *         from a light direction directly, or from azimuth/altitude. zFactor exaggerates the relief.
 */

#include <cmath>

namespace grove {
namespace mapview {

class Hillshade {
public:
    // Light direction (need not be normalized) + vertical exaggeration of the relief.
    Hillshade(double lx, double ly, double lz, double zFactor = 1.0) : zf_(zFactor) {
        const double n = std::sqrt(lx * lx + ly * ly + lz * lz);
        const double inv = n > 0.0 ? 1.0 / n : 0.0;
        lx_ = lx * inv;
        ly_ = ly * inv;
        lz_ = lz * inv;
    }

    // Light from a compass-like azimuth (radians; math frame: 0 = +x, CCW) at an altitude above the horizon.
    static Hillshade fromAzimuthAltitude(double azimuthRad, double altitudeRad, double zFactor = 1.0) {
        const double cz = std::cos(altitudeRad);
        return Hillshade(cz * std::cos(azimuthRad), cz * std::sin(azimuthRad), std::sin(altitudeRad), zFactor);
    }

    // Illumination factor in [0,1] for the surface gradient (dz/dx, dz/dy).
    double factor(double dzdx, double dzdy) const {
        const double nx = -zf_ * dzdx;
        const double ny = -zf_ * dzdy;
        const double nz = 1.0;
        const double inv = 1.0 / std::sqrt(nx * nx + ny * ny + nz * nz);
        double d = (nx * lx_ + ny * ly_ + nz * lz_) * inv;
        if (d < 0.0) d = 0.0;
        if (d > 1.0) d = 1.0;
        return d;
    }

private:
    double lx_{0.0};
    double ly_{0.0};
    double lz_{1.0};
    double zf_{1.0};
};

} // namespace mapview
} // namespace grove
