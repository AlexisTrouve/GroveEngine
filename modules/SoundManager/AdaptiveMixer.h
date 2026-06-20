#pragma once

/**
 * grove::sound::AdaptiveMixer — state-driven vertical layering for adaptive music (slice 1).
 *
 * WHAT : a set of music LAYERS (stems) whose gains are driven by a single emotional "tension"
 *        (0..1). Each layer crossfades linearly between a calm gain and a peak gain, so layers
 *        stack/unstack as tension rises — the core of adaptive music ("couches verticales
 *        empilées par la tension", grove_integration.md §🎵).
 *
 * WHY  : this is the de-risk slice — prove state-driven layered mixing CHEAPLY and HEADLESS.
 *        The mixer is PURE (gains only, no backend, no IIO), so it unit-tests with analytical
 *        oracles like LodColor.h / ZoomLadder.h. The engine owns the mix MATH; the game owns the
 *        intent + the stems (content). SoundManagerModule wires topics -> this mixer -> the
 *        ISoundBackend (it holds the playback handles and pushes currentGain via setSoundVolume).
 *
 * HOW  : target = gainCalm + tension*(gainPeak - gainCalm). So {1,1}=constant bed, {0,1}=fades
 *        in with tension, {1,0}=fades out with tension. tick() ramps each currentGain toward its
 *        target framerate-independently (exp approach), so layers fade smoothly, never jump.
 */

#include <cmath>
#include <string>
#include <vector>

namespace grove {
namespace sound {

// One vertical layer (stem) of the adaptive bed.
struct AdaptiveLayer {
    std::string id;
    float gainCalm = 1.0f;     // gain at tension 0
    float gainPeak = 1.0f;     // gain at tension 1
    float targetGain = 1.0f;   // where the gain wants to be (from the tension curve, or an explicit mix)
    float currentGain = 1.0f;  // ramped value actually pushed to the backend
    // curveDriven=false layers IGNORE the tension curve — they are driven only by setMix (the
    // leitmotif/theme selector, slice 3b). They start SILENT and a theme-select brings them in.
    bool curveDriven = true;
};

class AdaptiveMixer {
public:
    // Add or replace a layer. A curve-driven layer snaps its current AND target gain to the curve at
    // the current tension (stems play in sync — no add-time ramp). A non-curve-driven layer
    // (themed/leitmotif arrangement) starts SILENT and waits for setMix (the theme selector).
    void addLayer(const std::string& id, float gainCalm, float gainPeak, bool curveDriven = true) {
        const float tgt = curveDriven ? curve(gainCalm, gainPeak, m_tension) : 0.0f;
        for (auto& L : m_layers) {
            if (L.id == id) {
                L.gainCalm = gainCalm; L.gainPeak = gainPeak; L.curveDriven = curveDriven;
                L.targetGain = L.currentGain = tgt;
                return;
            }
        }
        m_layers.push_back({id, gainCalm, gainPeak, tgt, tgt, curveDriven});
    }

    void removeLayer(const std::string& id) {
        for (size_t i = 0; i < m_layers.size(); ++i) {
            if (m_layers[i].id == id) { m_layers.erase(m_layers.begin() + i); return; }
        }
    }

    // Set the global tension 0..1; recompute every CURVE-DRIVEN layer's target from its calm->peak
    // curve. Themed (non-curve-driven) layers are left to their selector (setMix).
    void setTension(float tension) {
        m_tension = clamp01(tension);
        for (auto& L : m_layers) if (L.curveDriven) L.targetGain = curve(L.gainCalm, L.gainPeak, m_tension);
    }

    // Explicit target for one layer (low-level override; the next setTension recomputes from curve).
    void setMix(const std::string& id, float gain) {
        for (auto& L : m_layers) if (L.id == id) { L.targetGain = clamp01(gain); return; }
    }

    // Ramp every currentGain toward its targetGain, framerate-independent (rate = fraction of the
    // remaining gap closed per second) — smooth layer fades instead of hard jumps.
    void tick(float dt, float rate) {
        const float a = 1.0f - std::exp(-rate * dt);
        for (auto& L : m_layers) L.currentGain += (L.targetGain - L.currentGain) * a;
    }

    const std::vector<AdaptiveLayer>& layers() const { return m_layers; }
    float tension() const { return m_tension; }
    int   count() const { return static_cast<int>(m_layers.size()); }

    const AdaptiveLayer* find(const std::string& id) const {
        for (const auto& L : m_layers) if (L.id == id) return &L;
        return nullptr;
    }

private:
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float curve(float calm, float peak, float tension) {
        return calm + tension * (peak - calm);
    }
    std::vector<AdaptiveLayer> m_layers;
    float m_tension = 0.0f;
};

} // namespace sound
} // namespace grove
