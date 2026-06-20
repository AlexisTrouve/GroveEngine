#pragma once

/**
 * SoundManagerModule — music + SFX via IIO topics (sound system, slice 1).
 *
 * WHAT  : An IModule that consumes sound:* topics and drives an ISoundBackend, applying the
 *         master/music/sfx volume buses. Backend-agnostic: SDL_mixer is injected (slice 2) or a
 *         mock in tests, so all of this logic is headless-testable.
 *
 * TOPICS (consumed):
 *   sound:sfx          { path, volume?=1, pan?=0 }              one-shot SFX
 *   sound:music        { path, loop?=true, fadeMs?=0, volume?=1 } play/replace music
 *   sound:music:stop   { fadeMs?=0 }                            stop music (with fade)
 *   sound:volume       { bus: "master"|"music"|"sfx", value }   set a bus volume [0,1]
 *
 * Effective volume sent to the backend = clamp01(per-call volume * bus * master).
 */

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/ITaskScheduler.h>
#include "ISoundBackend.h"
#include "AdaptiveMixer.h"
#include "BeatClock.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace grove {

class SoundManagerModule : public IModule {
public:
    SoundManagerModule();
    ~SoundManagerModule() override;

    // Inject the audio backend (a test mock, or the real SDLMixerBackend in slice 2). Must be
    // set before setConfiguration. (Slice 2 will create a default SDLMixerBackend if none set.)
    void setBackend(std::unique_ptr<sound::ISoundBackend> backend);

    // IModule interface
    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override;
    void process(const IDataNode& input) override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    std::string getType() const override { return "sound_manager"; }
    bool isIdle() const override { return true; }

private:
    void handleMessage(const Message& msg);
    void applyMusicVolume();                 // push effective music volume to the backend
    void tickAdaptive(float dt);             // ramp adaptive stem gains -> backend (per frame)
    void updateBeatClock(float dt);          // advance the beat clock + release a pending quantized intent
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    IIO* m_io = nullptr;
    std::unique_ptr<sound::ISoundBackend> m_backend;
    std::unique_ptr<IDataNode> m_config;

    // Volume buses, each in [0,1].
    float m_master = 1.0f;
    float m_music = 1.0f;
    float m_sfx = 1.0f;
    float m_musicBaseVolume = 1.0f;  // last per-call music volume (for live bus re-apply)
    bool m_musicPlaying = false;

    // Game-supplied id -> backend playback handle, for controllable (e.g. looping) SFX so
    // sound:sfx:stop can target the right channel. One-shot SFX (no id) are not tracked.
    std::unordered_map<std::string, int> m_sfxHandles;

    // Adaptive music bed (slice 1): tension-driven vertical layers (stems). The mixer holds the
    // gain math (pure); the module holds each layer's backend handle + the last volume it pushed
    // (so it only calls setSoundVolume on a real change). Adaptive stems sit on the MUSIC bus.
    sound::AdaptiveMixer m_mixer;
    std::unordered_map<std::string, int> m_layerHandles;     // layer id -> backend playback handle
    std::unordered_map<std::string, float> m_layerLastSent;  // layer id -> last effective gain sent
    float m_layerRampRate = 8.0f;                            // stem gain ramp speed (fraction/second)

    // Beat clock + a pending quantized intent (slice 2): a tension change asked with
    // quantize:"bar"/"beat" is staged here and applied when the clock crosses that boundary, so
    // transitions land on the measure. Clock stopped (no audio:tempo) => changes apply immediately.
    sound::BeatClock m_clock;
    bool  m_pendingIntent = false;
    float m_pendingTension = 0.0f;
    bool  m_pendingOnBar = true;    // true: wait for the next BAR; false: next BEAT

    // Pending cues / stingers (slice 3): one-shot musical events asked with quantize:"bar"/"beat"
    // wait here and fire (playSound, no loop) when the clock crosses that boundary. Cues are on the
    // MUSIC bus (they're part of the score, not gameplay SFX).
    struct PendingCue { int soundId; float callVolume; bool onBar; };
    std::vector<PendingCue> m_pendingCues;

    // Leitmotif themes (slice 3b): theme name -> list of (state, layerId) arrangements. A themed
    // layer is registered non-curve-driven (tension doesn't touch it); audio:theme {id,state}
    // crossfades to the layer whose state matches and silences the others (a discrete selector).
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> m_themes;

    uint64_t m_sfxCount = 0;
    uint64_t m_musicCount = 0;
};

} // namespace grove
