#pragma once

// ============================================================================
// grove::snap — generic DIRECTIONAL detent snapping (header-only, pure).
//
// WHAT : settle a continuous value onto one of a set of DETENT values, in the DIRECTION you were
//   last moving (never reversing), and only when within `range` of it (free movement between detents).
// WHY  : a reusable primitive with no engine/render coupling — zoom levels (log space), rotation
//   cardinals, scroll stops, timeline markers… anything with discrete "notches" you want to ease onto
//   without losing free movement in between. The ZoneNavigator uses it for zoom; other systems can too.
// HOW  : compare in LINEAR or LOG space (log for multiplicative quantities like zoom). dir +1 = moving
//   up (consider only detents ABOVE the value), -1 = down (only below), 0 = nearest on either side.
//   Returns the detent to ease toward (the caller damps to it), or the value itself if none is in range.
// ============================================================================

#include <cmath>

namespace grove {
namespace snap {

// The detent to ease toward, or `value` itself if none is within `range` in the requested direction.
// `detents` may be unsorted. `logSpace`: compare log(value) vs log(detent) (values must be > 0).
inline float directionalDetent(float value, const float* detents, int count,
                               int dir, float range, bool logSpace) {
    if (count <= 0 || range <= 0.0f) return value;
    const float vc = logSpace ? std::log(value > 0.0f ? value : 1e-6f) : value;
    float best = value;
    float bestDist = range;   // a detent must be strictly closer than `range` to win
    for (int i = 0; i < count; ++i) {
        const float dc = logSpace ? std::log(detents[i] > 0.0f ? detents[i] : 1e-6f) : detents[i];
        const float delta = dc - vc;                 // > 0: detent is above; < 0: below
        if (dir > 0 && delta <= 0.0f) continue;      // moving up   -> only detents above
        if (dir < 0 && delta >= 0.0f) continue;      // moving down -> only detents below
        const float dist = std::fabs(delta);
        if (dist < bestDist) { bestDist = dist; best = detents[i]; }
    }
    return best;
}

} // namespace snap
} // namespace grove
