#pragma once

/**
 * grove::anim::Flipbook — frame-by-frame (spritesheet) playback timing (flipbook slice F-b).
 *
 * WHAT  : An ordered list of sheet cell indices with a PER-FRAME duration and a loop flag.
 *         frameAt(time) returns the sheet cell shown at a given time; uvAt(time, sheet)
 *         resolves it to UVs via SpriteSheet. setFps() is the uniform-rate convenience.
 *
 * WHY   : Per-frame durations is the general model (uniform fps is just all-equal durations),
 *         and frame-by-frame animation routinely holds frames unequally (impact, idle,
 *         anticipation). The common case stays one call (setFps). Keeping it pure/standalone
 *         mirrors SpriteSheet/Easing: headless-testable, reusable, no atlas geometry here.
 *
 * HOW   : Pure, std-only. The game advances time in its own dt loop (a small FlipbookPlayer for
 *         play/pause/speed can wrap this later, like AnimationPlayer). Loop wraps via fmod;
 *         one-shot clamps to the last frame. The usable length is min(frames, durations).
 */

#include "SpriteSheet.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace grove {
namespace anim {

struct Flipbook {
    std::vector<int> frames;        // sheet cell indices, in play order
    std::vector<float> durations;   // seconds per frame (parallel to frames)
    bool loop = true;

    // Uniform frame rate helper: every frame gets duration 1/fps (sized to frames).
    void setFps(float fps) {
        const float d = (fps > 0.0f) ? (1.0f / fps) : 0.0f;
        durations.assign(frames.size(), d);
    }

    // Total play length (sum of usable per-frame durations).
    float totalDuration() const {
        const size_t n = usableCount();
        float total = 0.0f;
        for (size_t i = 0; i < n; ++i) total += durations[i];
        return total;
    }

    // Which ENTRY (0..usableCount-1) is shown at the given time.
    int entryAt(float time) const {
        const size_t n = usableCount();
        if (n == 0) return 0;

        float total = 0.0f;
        for (size_t i = 0; i < n; ++i) total += durations[i];
        if (total <= 0.0f) return 0;

        float t = time;
        if (loop) {
            t = std::fmod(t, total);
            if (t < 0.0f) t += total;
        } else {
            if (t < 0.0f) return 0;                       // before start -> first
            if (t >= total) return static_cast<int>(n - 1); // past end -> last
        }

        float acc = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            acc += durations[i];
            if (t < acc) return static_cast<int>(i);
        }
        return static_cast<int>(n - 1);
    }

    // The sheet cell index shown at the given time.
    int frameAt(float time) const {
        if (frames.empty()) return 0;
        int idx = entryAt(time);
        if (idx < 0) idx = 0;
        if (idx >= static_cast<int>(frames.size())) idx = static_cast<int>(frames.size()) - 1;
        return frames[static_cast<size_t>(idx)];
    }

    // Resolve the current frame to UVs through a sheet.
    void uvAt(float time, const SpriteSheet& sheet,
              float& u0, float& v0, float& u1, float& v1) const {
        sheet.frameUV(frameAt(time), u0, v0, u1, v1);
    }

private:
    // Frames are only playable while both a cell index AND a duration exist for them.
    size_t usableCount() const {
        return frames.size() < durations.size() ? frames.size() : durations.size();
    }
};

} // namespace anim
} // namespace grove
