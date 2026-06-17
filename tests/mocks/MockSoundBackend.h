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
    struct PlaySound { int soundId; float volume; float pan; };
    struct PlayMusic { int musicId; bool loop; int fadeMs; float volume; };

    // Recorded calls (named *Calls so they don't collide with the override method names).
    std::vector<PlaySound> playSoundCalls;
    std::vector<PlayMusic> playMusicCalls;
    std::vector<int> stopMusicCalls;          // fadeMs per call
    std::vector<float> setMusicVolumeCalls;   // effective volume per call
    bool inited = false;

    bool init() override { inited = true; return true; }
    void shutdown() override { inited = false; }

    int loadSound(const std::string& path) override { return cache(path); }
    int loadMusic(const std::string& path) override { return cache(path); }

    void playSound(int soundId, float volume, float pan) override {
        playSoundCalls.push_back({soundId, volume, pan});
    }
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
};

} // namespace test
} // namespace grove
