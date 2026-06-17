#include "SoundManagerModule.h"

#include <grove/IDataNode.h>
#include <grove/JsonDataNode.h>

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
    }

    // Bring the backend up (the real SDLMixerBackend opens the device here; the mock just flags).
    if (m_backend) {
        m_backend->init();
    }
}

void SoundManagerModule::process(const IDataNode& /*input*/) {
    // Pull-based: drain everything queued this frame; each message hits handleMessage().
    while (m_io && m_io->hasMessages() > 0) {
        m_io->pullAndDispatch();
    }
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
}

void SoundManagerModule::applyMusicVolume() {
    if (m_backend) {
        m_backend->setMusicVolume(clamp01(m_master * m_music * m_musicBaseVolume));
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
