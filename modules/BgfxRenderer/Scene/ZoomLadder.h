#pragma once

/**
 * grove::camera::ZoomLadder — discrete "strata" over a continuous zoom (header-only).
 *
 * WHAT : maps a continuous zoom to (a) the nearest readable PLATEAU to snap toward,
 *        (b) which two strata the zoom sits between + a crossfade factor `t`, and
 *        (c) the dominant strata. Strata = galaxy / system / ship / interior...
 *
 * WHY  : the zoom continuum needs "readable plateaus" (the scale POSES and reads, not
 *        infinite mush — INTERFACE.md / grove_integration.md item 1) AND a seamless
 *        inter-strata transition (item 3). This is the SAME shape as the tilemap LOD
 *        crossfade: the engine owns the MATH (a smoothstep factor), the consumer owns
 *        the CONTENT. Semantic zoom — *what* is rendered/simulated per strata — stays
 *        GAME-SIDE by design (item 2); the ladder only hands the game the seam it needs:
 *        the active strata + the blend `t`. The engine never decides content here.
 *
 * HOW  : work in LOG-zoom space — zoom is multiplicative, so equal ratios are equal
 *        perceptual steps (like octaves); a linear blend in log space feels uniform.
 *        Between two plateaus, `t` ramps 0->1 via smoothstep over a centered window
 *        (transitionWidth); outside that window `t` is pinned (a FLAT plateau = the
 *        scale "poses"). Pure, zero deps beyond <cmath>/<vector>; a static-link host
 *        (Drifterra) just  #include "Scene/ZoomLadder.h"  and unit-tests it headless.
 */

#include <cmath>
#include <vector>

namespace grove {
namespace camera {

// Smoothstep (Hermite) clamped to [0,1]. Local copy to keep this header self-contained.
inline float smoothstepLadder(float e0, float e1, float x) {
    if (e0 == e1) return x < e0 ? 0.0f : 1.0f;
    float u = (x - e0) / (e1 - e0);
    u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
    return u * u * (3.0f - 2.0f * u);
}

// Where a continuous zoom sits on the ladder.
struct ZoomBlend {
    int   lower  = 0;     // strata index at/below the current zoom
    int   upper  = 0;     // strata index above (== lower at the ends / fully on a plateau)
    float t      = 0.0f;  // 0 = fully `lower`, 1 = fully `upper` — the content crossfade factor
    int   active = 0;     // dominant strata (t < 0.5 ? lower : upper) — what to simulate/render
};

class ZoomLadder {
public:
    ZoomLadder() = default;

    // plateaus MUST be ascending (galaxy = small zoom ... interior = large zoom).
    // transitionWidth in (0,1]: fraction of each inter-plateau gap (in log space) over which `t`
    // ramps 0->1, centered between the two plateaus; the remainder is flat plateau (t pinned).
    // 0.5 = the middle half ramps, the outer quarters are locked to a single strata.
    explicit ZoomLadder(std::vector<float> plateaus, float transitionWidth = 0.5f)
        : m_plateaus(std::move(plateaus)), m_width(clampWidth(transitionWidth)) {}

    int   count() const { return static_cast<int>(m_plateaus.size()); }
    float plateau(int i) const { return m_plateaus[i]; }

    // Nearest plateau zoom (in log space) — the game `damp`s the camera zoom toward this to get
    // readable snapping ("the scale poses"). Identity if the ladder is empty.
    float snap(float zoom) const {
        const int n = count();
        if (n == 0) return zoom;
        const float L = logZoom(zoom);
        int best = 0;
        float bestD = std::fabs(L - std::log(m_plateaus[0]));
        for (int i = 1; i < n; ++i) {
            const float d = std::fabs(L - std::log(m_plateaus[i]));
            if (d < bestD) { bestD = d; best = i; }
        }
        return m_plateaus[best];
    }

    // Locate a zoom: the bracketing strata, the crossfade `t`, and the dominant strata.
    ZoomBlend blend(float zoom) const {
        ZoomBlend r;
        const int n = count();
        if (n == 0) return r;
        if (n == 1) { r.lower = r.upper = r.active = 0; r.t = 0.0f; return r; }

        const float L = logZoom(zoom);
        // Below the first / above the last plateau -> locked to the end strata (no transition).
        if (L <= std::log(m_plateaus[0]))     { r.lower = r.upper = r.active = 0;     return r; }
        if (L >= std::log(m_plateaus[n - 1])) { r.lower = r.upper = r.active = n - 1; return r; }

        // Find the bracket [i, i+1] that contains L (log space).
        int i = 0;
        while (i + 1 < n && std::log(m_plateaus[i + 1]) <= L) ++i;
        const float Li  = std::log(m_plateaus[i]);
        const float Li1 = std::log(m_plateaus[i + 1]);
        const float u   = (L - Li) / (Li1 - Li);            // 0..1 across the gap

        const float e0 = 0.5f - m_width * 0.5f;
        const float e1 = 0.5f + m_width * 0.5f;
        r.lower  = i;
        r.upper  = i + 1;
        r.t      = smoothstepLadder(e0, e1, u);
        r.active = (r.t < 0.5f) ? i : (i + 1);
        return r;
    }

private:
    static float clampWidth(float w) { return w < 0.01f ? 0.01f : (w > 1.0f ? 1.0f : w); }
    // Guard non-positive zoom (log undefined) with a tiny floor — degenerate input only.
    static float logZoom(float zoom) { return std::log(zoom > 0.0f ? zoom : 1e-6f); }

    std::vector<float> m_plateaus;
    float m_width = 0.5f;
};

} // namespace camera
} // namespace grove
