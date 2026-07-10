#pragma once

/**
 * grove::sound::SDLMixerBackend — real ISoundBackend over SDL2_mixer (sound system, slice 2).
 *
 * WHAT  : Implements ISoundBackend with SDL_mixer: opens the audio device, loads/caches SFX
 *         (Mix_Chunk) and music (Mix_Music) by path, and plays them with volume/pan/fade/loop.
 *
 * WHY   : Keeps SDL_mixer entirely behind the interface — SoundManagerModule never includes it,
 *         so the module's topic/bus logic stays SDL-free and headless-testable (slice 1). The
 *         host wires this backend in (module.setBackend(make_unique<SDLMixerBackend>())), the
 *         same injection model as InputModule_static.
 *
 * HOW   : Effective volumes arrive in [0,1] (the module applied the buses) and map to
 *         MIX_MAX_VOLUME. Pan [-1,1] maps to Mix_SetPanning left/right. Only the .cpp pulls in
 *         SDL_mixer.h, so this header is safe to include from build-system glue.
 */

#include "ISoundBackend.h"

#include <string>
#include <unordered_map>
#include <vector>

// SDL_mixer's opaque types (Mix_Chunk / Mix_Music). We include the real header rather than forward-declaring:
// the Mix_Music struct TAG differs across SDL2_mixer versions (`_Mix_Music` in older, `Mix_Music` in newer),
// so a hand-rolled `typedef struct _Mix_Music Mix_Music;` both uses a reserved identifier AND conflicts with
// SDL's own typedef on newer SDL2_mixer (a hard parse error). Only SDL-aware TUs include this backend header
// (the .cpp + the sound/video demos), so pulling in SDL_mixer here adds no dependency they don't already have.
#include <SDL_mixer.h>

namespace grove {
namespace sound {

class SDLMixerBackend : public ISoundBackend {
public:
    SDLMixerBackend() = default;
    ~SDLMixerBackend() override;

    bool init() override;
    void shutdown() override;

    int loadSound(const std::string& path) override;
    int loadMusic(const std::string& path) override;
    void unloadSound(const std::string& path) override;
    int playSound(int soundId, float volume, float pan, bool loop) override;
    void stopSound(int handle, int fadeMs) override;
    void stopAllSounds(int fadeMs) override;
    void setSoundVolume(int handle, float volume) override;
    void playMusic(int musicId, bool loop, int fadeMs, float volume) override;
    void stopMusic(int fadeMs) override;
    void setMusicVolume(float volume) override;

    // Music playback clock (slice 6b). SDL owns the clock, so updateMusic is a no-op; position +
    // duration come from SDL_mixer (>= 2.6). Version-guarded in the .cpp (older mixer -> -1).
    double getMusicPosition() const override;
    double getMusicDuration() const override;

private:
    bool m_open = false;

    // The track currently playing (set by playMusic, cleared by stopMusic) — needed to query its
    // position/duration for sound:music:position.
    Mix_Music* m_currentMusic = nullptr;

    // Caches: path -> index into the vectors (stable ids handed back to the module).
    std::unordered_map<std::string, int> m_soundIds;
    std::vector<Mix_Chunk*> m_sounds;
    std::unordered_map<std::string, int> m_musicIds;
    std::vector<Mix_Music*> m_musics;
};

} // namespace sound
} // namespace grove
