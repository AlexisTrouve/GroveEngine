#pragma once

/**
 * grove::video::VideoSync — pure A/V synchronisation math (video slice 6c).
 *
 * WHAT  : Given a MASTER CLOCK time (seconds) + the video's frame rate + frame count, decides which
 *         video frame should be on screen right now, and whether that is a CHANGE (upload a new
 *         frame), a DROP (the clock jumped ahead — we skipped frames, so we don't try to render the
 *         skipped ones), or the END (past the last frame).
 *
 * WHY   : A/V sync is "audio is the master clock, video follows it" — music plays at wall-clock and
 *         the picture is held/advanced/dropped to stay locked to it (drifting the audio to match the
 *         video would glitch the sound). That decision is PURE MATH (time → frame index), so it lives
 *         here, headless-testable, decoupled from any decoder / renderer — exactly like BeatClock is
 *         to SoundManager. The VideoModule feeds it the audio clock (sound:music:position, slice 6b)
 *         or its own dt clock when the clip is silent, and acts on the FrameTick it returns.
 *
 * HOW   : index = floor(clock · fps), clamped to [0, frameCount-1]. `changed` = the index moved off the
 *         last one (→ decode+upload it). `dropped` = how many frames were skipped when the clock jumped
 *         (a lagging renderer catches up by jumping, never by rendering every skipped frame). `ended`
 *         fires ONCE when the clock passes the last frame. Constant-fps model (image-sequence + most
 *         video); variable-PTS is a follow-on. std-only, no deps.
 */

#include <cmath>

namespace grove {
namespace video {

// The result of one sync step — what the module should do this frame.
struct FrameTick {
    int  index = 0;        // the frame to show now (clamped valid)
    bool changed = false;  // index moved off the last shown frame → upload this frame
    int  dropped = 0;      // frames skipped since the last tick (clock jumped) → we did NOT render them
    bool ended = false;    // the clock just passed the final frame (fires once)
};

class VideoSync {
public:
    // fps must be > 0 (defaults to 30 on a bad value); frameCount ≤ 0 = unknown length (never ends).
    void configure(double fps, int frameCount) {
        m_fps = (fps > 0.0) ? fps : 30.0;
        m_frameCount = frameCount;
    }

    // Rewind to the start (a fresh play / loop).
    void reset() { m_lastIndex = -1; m_endedFired = false; }

    // Advance the picture to `clockSec` (the master clock). Returns what changed.
    FrameTick update(double clockSec) {
        int idx = static_cast<int>(std::floor(clockSec * m_fps));
        if (idx < 0) idx = 0;

        bool pastEnd = false;
        if (m_frameCount > 0 && idx >= m_frameCount) {
            idx = m_frameCount - 1;   // hold the last frame
            pastEnd = true;
        }

        FrameTick t;
        t.index   = idx;
        t.changed = (idx != m_lastIndex);
        t.dropped = (m_lastIndex >= 0 && idx > m_lastIndex + 1) ? (idx - m_lastIndex - 1) : 0;
        t.ended   = pastEnd && !m_endedFired;   // once
        if (pastEnd) m_endedFired = true;

        m_lastIndex = idx;
        return t;
    }

    int  lastIndex() const { return m_lastIndex; }
    bool hasEnded() const { return m_endedFired; }

private:
    double m_fps = 30.0;
    int    m_frameCount = 0;
    int    m_lastIndex = -1;
    bool   m_endedFired = false;
};

} // namespace video
} // namespace grove
