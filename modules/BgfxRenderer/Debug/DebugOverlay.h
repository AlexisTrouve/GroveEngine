#pragma once

#include <cstdint>
#include <string>

namespace grove {

/**
 * @brief Debug overlay for displaying runtime stats
 *
 * Uses bgfx debug text to display FPS, frame time, sprite count, etc.
 * Can be toggled on/off at runtime.
 */
class DebugOverlay {
public:
    DebugOverlay() = default;

    // Enable/disable overlay
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    void toggle() { m_enabled = !m_enabled; }

    // Update stats (call each frame)
    void update(float deltaTime, uint32_t spriteCount, uint32_t drawCalls);

    // Render the overlay (call after bgfx::frame setup, before submit)
    void render(uint16_t screenWidth, uint16_t screenHeight);

private:
    bool m_enabled = false;

    // Stats tracking
    float m_deltaTime = 0.0f;
    float m_fps = 0.0f;
    float m_frameTimeMs = 0.0f;
    uint32_t m_spriteCount = 0;
    uint32_t m_drawCalls = 0;

    // FPS smoothing
    float m_fpsAccum = 0.0f;
    int m_fpsFrameCount = 0;
    float m_smoothedFps = 0.0f;
    static constexpr float FPS_UPDATE_INTERVAL = 0.25f; // Update FPS display every 250ms
};

} // namespace grove
