#include "SoundManagerModule.h"

#include <grove/IDataNode.h>
#include <grove/JsonDataNode.h>

#include <cmath>

namespace grove {

SoundManagerModule::SoundManagerModule()
    : m_config(std::make_unique<JsonDataNode>("config")) {}

SoundManagerModule::~SoundManagerModule() = default;

void SoundManagerModule::setBackend(std::unique_ptr<sound::ISoundBackend> backend) {
    m_backend = std::move(backend);
}

void SoundManagerModule::setConfiguration(const IDataNode& /*config*/, IIO* io, ITaskScheduler* /*scheduler*/) {
    m_io = io;

    // Single subscription to the whole sound:* family; dispatch by exact topic in the handler
    // (same pattern as SceneCollector's render:.* — '.*' is a terminal wildcard).
    if (m_io) {
        m_io->subscribe("sound:.*", [this](const Message& msg) { handleMessage(msg); });
        // Adaptive music spine (slice 1): the game publishes its emotional state on audio:* and the
        // mixer drives the stems. Kept a SEPARATE namespace from sound:* on purpose — sound:* is
        // imperative ("play this"), audio:* is declarative ("the mood is X").
        m_io->subscribe("audio:.*", [this](const Message& msg) { handleMessage(msg); });
    }

    // Bring the backend up (the real SDLMixerBackend opens the device here; the mock just flags).
    if (m_backend) {
        m_backend->init();
    }
}

void SoundManagerModule::process(const IDataNode& input) {
    // Pull-based: drain everything queued this frame FIRST (each message hits handleMessage), so
    // this frame's audio:intent/layer/mix have updated the mixer targets before we ramp.
    while (m_io && m_io->hasMessages() > 0) {
        m_io->pullAndDispatch();
    }

    // Then advance the beat clock (may release a pending quantized intent on a bar/beat) and ramp
    // the adaptive stem gains toward their (possibly just-changed) targets.
    const float dt = static_cast<float>(input.getDouble("deltaTime", 0.016));
    updateBeatClock(dt);
    tickAdaptive(dt);
}

void SoundManagerModule::handleMessage(const Message& msg) {
    if (!msg.data || !m_backend) return;
    const IDataNode& d = *msg.data;

    if (msg.topic == "sound:sfx") {
        const std::string path = d.getString("path", "");
        if (path.empty()) return;
        const float vol = clamp01(static_cast<float>(d.getDouble("volume", 1.0)));
        const float pan = static_cast<float>(d.getDouble("pan", 0.0));
        const bool loop = d.getBool("loop", false);
        const std::string id = d.getString("id", "");  // optional: makes the SFX controllable
        const int soundId = m_backend->loadSound(path);
        if (soundId < 0) return;
        const int handle = m_backend->playSound(soundId, clamp01(m_master * m_sfx * vol), pan, loop);
        if (!id.empty() && handle >= 0) {
            m_sfxHandles[id] = handle;  // track so sound:sfx:stop {id} can reach this channel
        }
        ++m_sfxCount;
    }
    else if (msg.topic == "sound:sfx:stop") {
        const std::string id = d.getString("id", "");
        const int fadeMs = static_cast<int>(d.getInt("fadeMs", 0));
        auto it = m_sfxHandles.find(id);
        if (it != m_sfxHandles.end()) {
            m_backend->stopSound(it->second, fadeMs);
            m_sfxHandles.erase(it);
        }
    }
    else if (msg.topic == "sound:sfx:stopAll") {
        const int fadeMs = static_cast<int>(d.getInt("fadeMs", 0));
        m_backend->stopAllSounds(fadeMs);
        m_sfxHandles.clear();
    }
    else if (msg.topic == "sound:preload") {
        const std::string path = d.getString("path", "");
        if (!path.empty()) m_backend->loadSound(path);  // warm the cache, don't play
    }
    else if (msg.topic == "sound:unload") {
        const std::string path = d.getString("path", "");
        if (!path.empty()) m_backend->unloadSound(path);
    }
    else if (msg.topic == "sound:music") {
        const std::string path = d.getString("path", "");
        if (path.empty()) return;
        const bool loop = d.getBool("loop", true);
        const int fadeMs = static_cast<int>(d.getInt("fadeMs", 0));
        m_musicBaseVolume = clamp01(static_cast<float>(d.getDouble("volume", 1.0)));
        const int id = m_backend->loadMusic(path);
        if (id < 0) return;
        m_backend->playMusic(id, loop, fadeMs, clamp01(m_master * m_music * m_musicBaseVolume));
        m_musicPlaying = true;
        ++m_musicCount;
    }
    else if (msg.topic == "sound:music:stop") {
        const int fadeMs = static_cast<int>(d.getInt("fadeMs", 0));
        m_backend->stopMusic(fadeMs);
        m_musicPlaying = false;
    }
    else if (msg.topic == "sound:volume") {
        const std::string bus = d.getString("bus", "");
        const float value = clamp01(static_cast<float>(d.getDouble("value", 1.0)));
        if (bus == "master")      m_master = value;
        else if (bus == "music")  m_music = value;
        else if (bus == "sfx")    m_sfx = value;
        // The master and music buses scale the currently-playing track -> re-apply live.
        if (bus == "master" || bus == "music") applyMusicVolume();
    }
    // ------------------------------------------------------------------------
    // Adaptive music (slice 1): tension-driven vertical layers.
    // ------------------------------------------------------------------------
    else if (msg.topic == "audio:layer") {
        // Register/replace a stem and start it looping. gainCalm/gainPeak define its crossfade
        // over tension ({1,1}=bed, {0,1}=fades in, {1,0}=fades out). It starts at the gain for
        // the current tension (stems play in sync), then ramps as tension changes (tickAdaptive).
        const std::string id = d.getString("id", "");
        const std::string path = d.getString("path", "");
        if (id.empty() || path.empty()) return;
        const float gainCalm = static_cast<float>(d.getDouble("gainCalm", 1.0));
        const float gainPeak = static_cast<float>(d.getDouble("gainPeak", 1.0));
        const bool loop = d.getBool("loop", true);
        const int soundId = m_backend->loadSound(path);
        if (soundId < 0) return;
        // Replacing an existing layer id: stop the old channel first.
        auto old = m_layerHandles.find(id);
        if (old != m_layerHandles.end()) m_backend->stopSound(old->second, 0);

        m_mixer.addLayer(id, gainCalm, gainPeak);
        const sound::AdaptiveLayer* L = m_mixer.find(id);
        const float eff = clamp01((L ? L->currentGain : 0.0f) * m_music * m_master);  // music bus
        const int handle = m_backend->playSound(soundId, eff, 0.0f, loop);
        m_layerHandles[id] = handle;
        m_layerLastSent[id] = eff;
    }
    else if (msg.topic == "audio:tempo") {
        // Configure the musical clock used to quantize transitions to the measure. bpm 0 = stop
        // (no quantization -> quantized intents apply immediately).
        const float bpm = static_cast<float>(d.getDouble("bpm", 0.0));
        const int beatsPerBar = static_cast<int>(d.getInt("beatsPerBar", 4));
        m_clock.setTempo(bpm, beatsPerBar);
    }
    else if (msg.topic == "audio:intent") {
        // The game's emotional state. quantize: "now" (default) applies immediately; "bar"/"beat"
        // STAGE the change and release it when the clock crosses that boundary (transitions land on
        // the measure). With the clock stopped there is nothing to wait for -> apply now.
        const float tension = static_cast<float>(d.getDouble("tension", 0.0));
        const std::string q = d.getString("quantize", "now");
        if (q == "now" || !m_clock.running()) {
            m_mixer.setTension(tension);
        } else {
            m_pendingIntent = true;
            m_pendingTension = tension;
            m_pendingOnBar = (q != "beat");   // "bar" (default for a quantized intent) unless "beat"
        }
    }
    else if (msg.topic == "audio:mix") {
        // Low-level: set one layer's target gain explicitly (until the next audio:intent).
        const std::string id = d.getString("id", "");
        if (!id.empty()) m_mixer.setMix(id, static_cast<float>(d.getDouble("gain", 0.0)));
    }
    else if (msg.topic == "audio:layer:stop") {
        // Stop + drop a stem (optionally fading the channel out).
        const std::string id = d.getString("id", "");
        const int fadeMs = static_cast<int>(d.getInt("fadeMs", 0));
        auto it = m_layerHandles.find(id);
        if (it != m_layerHandles.end()) {
            m_backend->stopSound(it->second, fadeMs);
            m_layerHandles.erase(it);
            m_layerLastSent.erase(id);
            m_mixer.removeLayer(id);
        }
    }
}

void SoundManagerModule::applyMusicVolume() {
    if (m_backend) {
        m_backend->setMusicVolume(clamp01(m_master * m_music * m_musicBaseVolume));
    }
}

void SoundManagerModule::tickAdaptive(float dt) {
    // QUOI : rampe les gains des stems vers leur cible et pousse au backend ceux qui ont changé.
    // POURQUOI : la cible bouge à chaque audio:intent ; le ramp (lissé, framerate-indépendant)
    //   donne un fondu de couches plutôt qu'un saut. On n'appelle setSoundVolume que sur un VRAI
    //   changement (seuil) pour ne pas spammer le backend chaque frame (retained-style).
    // COMMENT : eff = clamp01(currentGain * music * master) — les stems adaptatifs sont sur le bus
    //   MUSIC (sound:volume {bus:music} les affecte donc, via le re-push au prochain tick).
    if (!m_backend || m_mixer.count() == 0) return;
    m_mixer.tick(dt, m_layerRampRate);
    for (const auto& L : m_mixer.layers()) {
        auto hIt = m_layerHandles.find(L.id);
        if (hIt == m_layerHandles.end()) continue;
        const float eff = clamp01(L.currentGain * m_music * m_master);
        auto sIt = m_layerLastSent.find(L.id);
        if (sIt == m_layerLastSent.end() || std::fabs(sIt->second - eff) > 1e-3f) {
            m_backend->setSoundVolume(hIt->second, eff);
            m_layerLastSent[L.id] = eff;
        }
    }
}

void SoundManagerModule::updateBeatClock(float dt) {
    // Advance the clock and, if a quantized intent is waiting, release it the moment the clock
    // crosses the requested boundary (bar or beat) — this is the "transitions calées sur la mesure".
    m_clock.advance(dt);
    if (m_pendingIntent) {
        const bool fire = m_pendingOnBar ? m_clock.crossedBar() : m_clock.crossedBeat();
        if (fire) {
            m_mixer.setTension(m_pendingTension);
            m_pendingIntent = false;
        }
    }
}

void SoundManagerModule::shutdown() {
    if (m_backend) m_backend->shutdown();
}

std::unique_ptr<IDataNode> SoundManagerModule::getState() {
    // Preserve the volume buses across hot-reload.
    auto state = std::make_unique<JsonDataNode>("state");
    state->setDouble("master", m_master);
    state->setDouble("music", m_music);
    state->setDouble("sfx", m_sfx);
    return state;
}

void SoundManagerModule::setState(const IDataNode& state) {
    m_master = clamp01(static_cast<float>(state.getDouble("master", 1.0)));
    m_music = clamp01(static_cast<float>(state.getDouble("music", 1.0)));
    m_sfx = clamp01(static_cast<float>(state.getDouble("sfx", 1.0)));
}

const IDataNode& SoundManagerModule::getConfiguration() {
    return *m_config;
}

std::unique_ptr<IDataNode> SoundManagerModule::getHealthStatus() {
    auto health = std::make_unique<JsonDataNode>("health");
    health->setString("status", "ok");
    health->setBool("backendReady", m_backend != nullptr);
    health->setInt("sfxPlayed", static_cast<int>(m_sfxCount));
    health->setInt("musicPlayed", static_cast<int>(m_musicCount));
    return health;
}

} // namespace grove
