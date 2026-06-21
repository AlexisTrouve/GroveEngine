#pragma once

// ============================================================================
// grove::tilemap — animated-tile frame selection. PURE, header-only, the CPU MIRROR of the animation
// math in fs_tilemap.sc.
//
// WHAT : which atlas-array LAYER an animated tile shows at a given time. The index texture stores the
//        tile's BASE id (uploaded once); the fragment shader cycles the LAYER over time for the ids the
//        game declared animated — so water / lava flow with ZERO per-frame upload. The frame index is
//        floor(timeSec * fps) mod frameCount; the layer is the tile's base layer (id-1) PLUS that frame
//        (an animation's frames are CONSECUTIVE atlas layers from the base — the game arranges its
//        tileset that way).
//
// WHY  : keep the only custom bit (frame selection) pure + headless so an exact oracle locks it (at
//        t=0 -> frame 0, at t=1/fps -> frame 1, wrap at frameCount). The GPU side is then just the
//        already-proven texture2DArray sample at the computed layer.
// ============================================================================

#include <cmath>

namespace grove { namespace tilemap {

// Current frame (0..frameCount-1) of an animated tile at `timeSec`, cycling at `fps`. A non-animated
// tile (frameCount <= 1) or a stopped clock (fps <= 0) stays on frame 0. timeSec is assumed >= 0.
inline int animFrame(float timeSec, float fps, int frameCount) {
    if (frameCount <= 1 || fps <= 0.0f || timeSec < 0.0f) return 0;
    const long ticks = static_cast<long>(std::floor(static_cast<double>(timeSec) * static_cast<double>(fps)));
    int f = static_cast<int>(ticks % frameCount);
    if (f < 0) f += frameCount;   // defensive (timeSec >= 0 won't hit this)
    return f;
}

// Atlas layer for an animated tile whose base layer is `baseLayer` (= tile id - 1): base + current frame.
inline int animLayer(int baseLayer, float timeSec, float fps, int frameCount) {
    return baseLayer + animFrame(timeSec, fps, frameCount);
}

}} // namespace grove::tilemap
