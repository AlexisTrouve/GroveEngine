#pragma once

/**
 * grove::camera::DragPan — turn a mouse (or touch) press → move → release into per-frame SCREEN deltas
 * for "click-drag to pan the camera".
 *
 * WHAT : a tiny state machine. begin() on button-press (anchors the cursor), update() each frame the
 *        button is held (returns the on-screen delta SINCE THE LAST update — {0,0} on the first call so
 *        there's no jump), end() on release. active() reports whether a drag is in progress.
 *
 * WHY  : every consumer that wants click-drag panning reinvents the same "remember the last cursor pos,
 *        diff it, watch the button" glue (and trips on the first-frame jump). Centralising it here —
 *        header-only, button-AGNOSTIC (the consumer binds whatever button: in Drifterra the RIGHT one,
 *        left stays for selection), and renderer/IIO/SDL-free — makes drag-pan a 3-liner: feed the
 *        delta straight into ZoneNavigator::panScreen (or any pan). It returns the RAW screen delta so
 *        the caller picks the convention: pass it as-is for "look" pan (RTS edge-scroll feel) or NEGATED
 *        for "grab" pan (the world follows the cursor — the Google-Maps feel).
 *
 * HOW  : holds the active flag + the last cursor position. update() computes (x-last, y-last), stores
 *        the new last, and returns the delta. The first update() after begin() returns {0,0} because
 *        last == the anchor. Pure std, no deps — unit-tested headless (no window). Composes with the
 *        other header-only camera helpers (Camera.h, ZoomLadder.h, ZoneNavigator.h).
 */

namespace grove {
namespace camera {

// On-screen movement in pixels (top-left origin, y-down — same convention as the cursor).
struct ScreenDelta {
    float dx = 0.0f;
    float dy = 0.0f;
};

class DragPan {
public:
    // Button pressed at (sx,sy): start a drag and anchor the cursor. Re-begin just re-anchors.
    void begin(float sx, float sy) { m_active = true; m_lastX = sx; m_lastY = sy; }

    // Button released: stop dragging. update() returns {0,0} until the next begin().
    void end() { m_active = false; }

    bool active() const { return m_active; }

    // Cursor moved to (sx,sy) this frame. Returns the screen delta since the last sample: {0,0} if not
    // dragging, and {0,0} on the first call after begin() (last == anchor) so there's no first-frame
    // jump. Updates the stored position so the next call diffs against this one.
    ScreenDelta update(float sx, float sy) {
        if (!m_active) return ScreenDelta{};               // not dragging -> nothing to pan
        ScreenDelta d{ sx - m_lastX, sy - m_lastY };       // movement since the last sample
        m_lastX = sx; m_lastY = sy;                        // next call diffs against this one
        return d;
    }

private:
    bool  m_active = false;
    float m_lastX = 0.0f, m_lastY = 0.0f;
};

} // namespace camera
} // namespace grove
