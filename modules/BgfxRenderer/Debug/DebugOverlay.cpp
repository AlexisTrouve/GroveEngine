#include "DebugOverlay.h"
#include <bgfx/bgfx.h>

namespace grove {

void DebugOverlay::update(float deltaTime, uint32_t spriteCount, uint32_t drawCalls) {
    m_deltaTime = deltaTime;
    m_frameTimeMs = deltaTime * 1000.0f;
    m_fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
    m_spriteCount = spriteCount;
    m_drawCalls = drawCalls;

    // Smooth FPS over time
    m_fpsAccum += deltaTime;
    m_fpsFrameCount++;

    if (m_fpsAccum >= FPS_UPDATE_INTERVAL) {
        m_smoothedFps = static_cast<float>(m_fpsFrameCount) / m_fpsAccum;
        m_fpsAccum = 0.0f;
        m_fpsFrameCount = 0;
    }
}

void DebugOverlay::render(uint16_t screenWidth, uint16_t screenHeight) {
    if (!m_enabled) {
        return;
    }

    // Enable debug text rendering
    bgfx::setDebug(BGFX_DEBUG_TEXT);

    // Clear debug text buffer
    bgfx::dbgTextClear();

    // Calculate text columns based on screen width (8 pixels per char typically)
    // uint16_t cols = screenWidth / 8;
    (void)screenWidth;
    (void)screenHeight;

    // Header
    bgfx::dbgTextPrintf(1, 1, 0x0f, "GroveEngine Debug Overlay");
    bgfx::dbgTextPrintf(1, 2, 0x07, "========================");

    // FPS and frame time
    uint8_t fpsColor = 0x0a; // Green
    if (m_smoothedFps < 30.0f) {
        fpsColor = 0x0c; // Red
    } else if (m_smoothedFps < 55.0f) {
        fpsColor = 0x0e; // Yellow
    }
    bgfx::dbgTextPrintf(1, 4, fpsColor, "FPS: %.1f", m_smoothedFps);
    bgfx::dbgTextPrintf(1, 5, 0x07, "Frame: %.2f ms", m_frameTimeMs);

    // Rendering stats
    bgfx::dbgTextPrintf(1, 7, 0x07, "Sprites: %u", m_spriteCount);
    bgfx::dbgTextPrintf(1, 8, 0x07, "Draw calls: %u", m_drawCalls);

    // bgfx stats
    const bgfx::Stats* stats = bgfx::getStats();
    if (stats) {
        bgfx::dbgTextPrintf(1, 10, 0x07, "GPU time: %.2f ms",
            double(stats->gpuTimeEnd - stats->gpuTimeBegin) * 1000.0 / stats->gpuTimerFreq);
        bgfx::dbgTextPrintf(1, 11, 0x07, "CPU submit: %.2f ms",
            double(stats->cpuTimeEnd - stats->cpuTimeBegin) * 1000.0 / stats->cpuTimerFreq);
        bgfx::dbgTextPrintf(1, 12, 0x07, "Primitives: %u", stats->numPrims[bgfx::Topology::TriList]);
        bgfx::dbgTextPrintf(1, 13, 0x07, "Textures: %u / %u", stats->numTextures, stats->textureMemoryUsed / 1024);
    }

    // Instructions
    bgfx::dbgTextPrintf(1, 15, 0x08, "Press F3 to toggle overlay");
}

} // namespace grove
