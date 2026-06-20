#pragma once

/**
 * grove::sound::BeatClock — a musical clock for quantizing adaptive transitions (slice 2).
 *
 * WHAT : advances a beat position from a tempo (BPM) + time signature (beats per bar), and reports
 *        whether a BEAT or BAR boundary was crossed in the last tick.
 *
 * WHY  : adaptive music must change ON THE MEASURE ("transitions calées sur la mesure",
 *        grove_integration.md §🎵). A section/intent change requested mid-bar should wait for the
 *        next bar so it lands musically, not jarringly. SoundManagerModule stages a quantized
 *        change and releases it when crossedBar()/crossedBeat() fires.
 *
 * HOW  : pure (no audio), header-only -> analytical oracle test (like AdaptiveMixer / ZoomLadder).
 *        beatsPerSec = bpm/60; advance() accumulates beatPos and diffs floor(beatPos) and
 *        floor(beatPos / beatsPerBar) across the tick to detect crossings. bpm<=0 => stopped
 *        (no quantization — the module then applies changes immediately).
 */

#include <cmath>

namespace grove {
namespace sound {

class BeatClock {
public:
    void setTempo(float bpm, int beatsPerBar) {
        m_bpm = bpm;
        m_beatsPerBar = beatsPerBar > 0 ? beatsPerBar : 1;
    }

    bool   running()     const { return m_bpm > 0.0f; }
    float  bpm()         const { return m_bpm; }
    int    beatsPerBar() const { return m_beatsPerBar; }
    double beatPos()     const { return m_beatPos; }

    // Advance by dt seconds; record how many beat/bar boundaries were crossed this tick.
    void advance(float dt) {
        m_crossedBeats = 0;
        m_crossedBars = 0;
        if (m_bpm <= 0.0f || dt <= 0.0f) return;
        const double prev = m_beatPos;
        m_beatPos += (static_cast<double>(m_bpm) / 60.0) * static_cast<double>(dt);
        m_crossedBeats = floorInt(m_beatPos) - floorInt(prev);
        m_crossedBars  = floorInt(m_beatPos / m_beatsPerBar) - floorInt(prev / m_beatsPerBar);
    }

    bool crossedBeat() const { return m_crossedBeats > 0; }
    bool crossedBar()  const { return m_crossedBars  > 0; }

    // Beats remaining until the next bar boundary (for telegraphing an upcoming change).
    double beatsToNextBar() const {
        const double inBar = m_beatPos - floorInt(m_beatPos / m_beatsPerBar) * static_cast<double>(m_beatsPerBar);
        return static_cast<double>(m_beatsPerBar) - inBar;
    }

private:
    static int floorInt(double v) { return static_cast<int>(std::floor(v)); }

    float  m_bpm = 0.0f;
    int    m_beatsPerBar = 4;
    double m_beatPos = 0.0;
    int    m_crossedBeats = 0;
    int    m_crossedBars = 0;
};

} // namespace sound
} // namespace grove
