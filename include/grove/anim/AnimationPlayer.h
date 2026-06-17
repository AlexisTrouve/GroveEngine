#pragma once

/**
 * grove::anim::AnimationPlayer — clip playback driver (animation system, slice 3).
 *
 * WHAT  : The time model that makes a Clip "play": play/pause/resume/stop, loop, speed, and
 *         dt-based advance. update(dt, hierarchy) advances the clock and writes the sampled
 *         clip values into the node LOCAL transforms.
 *
 * WHY   : Clip::apply is a pure function of absolute time; the Player owns the clock. PERF: the
 *         player keeps a const Clip* (shared, NEVER copied) so thousands of animated instances
 *         reuse one clip's keyframe data — tiny per-instance state (a few floats + flags), zero
 *         per-frame allocation. It writes locals only; world composition stays the hierarchy's
 *         job so ONE Hierarchy::update() serves many players over the same hierarchy.
 *
 * HOW   : Forward or reverse (negative speed); loop wraps with fmod (kept in [0,duration)),
 *         one-shot clamps to the end and goes idle. Pure, headless, std-only.
 */

#include "Clip.h"
#include "Transform2D.h"

#include <cmath>

namespace grove {
namespace anim {

class AnimationPlayer {
public:
    // Start playing a clip (NOT owned — must outlive the player). Resets the clock to 0.
    void play(const Clip* clip, bool loop = true, float speed = 1.0f) {
        m_clip = clip;
        m_loop = loop;
        m_speed = speed;
        m_time = 0.0f;
        m_playing = (clip != nullptr);
    }

    void pause()  { m_playing = false; }
    void resume() { if (m_clip) m_playing = true; }

    // Stop and rewind to the start (idle).
    void stop() {
        m_playing = false;
        m_time = 0.0f;
    }

    bool isPlaying() const { return m_playing; }
    float time() const { return m_time; }
    void setSpeed(float speed) { m_speed = speed; }
    void setLoop(bool loop) { m_loop = loop; }
    const Clip* clip() const { return m_clip; }

    // Advance by dt (seconds) and apply the sampled clip to the hierarchy locals. No-op when
    // idle or clip-less. The caller runs Hierarchy::update() once after all players this frame.
    void update(float dt, Hierarchy& hierarchy) {
        if (!m_playing || !m_clip) return;

        m_time += dt * m_speed;

        const float duration = m_clip->duration;
        if (duration > 0.0f) {
            if (m_loop) {
                // Keep time in [0, duration) for both forward and reverse playback.
                m_time = std::fmod(m_time, duration);
                if (m_time < 0.0f) m_time += duration;
            } else if (m_time >= duration) {
                m_time = duration;     // clamp to the last frame
                m_playing = false;     // one-shot finished
            } else if (m_time < 0.0f) {
                m_time = 0.0f;
                m_playing = false;
            }
        }

        m_clip->apply(m_time, hierarchy);
    }

private:
    const Clip* m_clip = nullptr;   // shared immutable data — not owned (perf: zero copy)
    float m_time = 0.0f;
    float m_speed = 1.0f;
    bool m_loop = true;
    bool m_playing = false;
};

} // namespace anim
} // namespace grove
