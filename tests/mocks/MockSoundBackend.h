#pragma once

/**
 * Mock ISoundBackend for headless sound tests.
 *
 * Records every backend call (and the effective volumes the module computed) so tests can
 * assert the module's topic->backend + bus-volume logic without any real audio. Caches
 * path->id like the real backend so repeated loads of one path reuse a handle.
 */

#include "SoundManager/ISoundBackend.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace grove {
namespace test {

class MockSoundBackend : public sound::ISoundBackend {
public:
    struct PlaySound { int soundId; float volume; float pan; bool loop; int handle; };
    struct PlayMusic { int musicId; bool loop; int fadeMs; float volume; };
    struct StopSound { int handle; int fadeMs; };
    struct SetVol { int handle; float volume; };

    // Recorded calls (named *Calls so they don't collide with the override method names).
    std::vector<PlaySound> playSoundCalls;
    std::vector<PlayMusic> playMusicCalls;
    std::vector<StopSound> stopSoundCalls;
    std::vector<int> stopAllCalls;            // fadeMs per stopAllSounds
    std::vector<std::string> unloadCalls;     // path per unloadSound
    std::vector<int> stopMusicCalls;          // fadeMs per call
    std::vector<float> setMusicVolumeCalls;   // effective volume per call
    std::vector<SetVol> setSoundVolumeCalls;  // (handle, effective volume) per setSoundVolume
    bool inited = false;

    bool init() override { inited = true; return true; }
    void shutdown() override { inited = false; }

    int loadSound(const std::string& path) override { return cache(path); }
    int loadMusic(const std::string& path) override { return cache(path); }

    void unloadSound(const std::string& path) override {
        unloadCalls.push_back(path);
        m_ids.erase(path);   // mirror the real backend dropping the cache entry
    }

    int playSound(int soundId, float volume, float pan, bool loop) override {
        const int handle = m_nextHandle++;   // simulate an allocated channel
        playSoundCalls.push_back({soundId, volume, pan, loop, handle});
        return handle;
    }
    void stopSound(int handle, int fadeMs) override { stopSoundCalls.push_back({handle, fadeMs}); }
    void stopAllSounds(int fadeMs) override { stopAllCalls.push_back(fadeMs); }
    void setSoundVolume(int handle, float volume) override { setSoundVolumeCalls.push_back({handle, volume}); }

    void playMusic(int musicId, bool loop, int fadeMs, float volume) override {
        playMusicCalls.push_back({musicId, loop, fadeMs, volume});
    }
    void stopMusic(int fadeMs) override { stopMusicCalls.push_back(fadeMs); }
    void setMusicVolume(float volume) override { setMusicVolumeCalls.push_back(volume); }

    // Test helpers.
    int idForPath(const std::string& path) {
        auto it = m_ids.find(path);
        return (it != m_ids.end()) ? it->second : -1;
    }
    int distinctLoads() const { return static_cast<int>(m_ids.size()); }

private:
    int cache(const std::string& path) {
        auto it = m_ids.find(path);
        if (it != m_ids.end()) return it->second;
        const int id = m_nextId++;
        m_ids[path] = id;
        return id;
    }

    std::unordered_map<std::string, int> m_ids;
    int m_nextId = 0;
    int m_nextHandle = 1;   // playback handles (channels), >=1
};

} // namespace test
} // namespace grove
