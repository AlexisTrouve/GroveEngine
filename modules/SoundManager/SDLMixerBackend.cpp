#include "SDLMixerBackend.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <algorithm>

namespace grove {
namespace sound {

namespace {
// [0,1] effective volume -> SDL_mixer's 0..MIX_MAX_VOLUME.
int toMixVolume(float v) {
    if (v < 0.0f) v = 0.0f;
    else if (v > 1.0f) v = 1.0f;
    return static_cast<int>(v * static_cast<float>(MIX_MAX_VOLUME) + 0.5f);
}
} // namespace

SDLMixerBackend::~SDLMixerBackend() {
    shutdown();
}

bool SDLMixerBackend::init() {
    if (m_open) return true;

    // The module is the audio subsystem owner here; init it if the host hasn't.
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    }

    // Enable the compressed-music decoders we care about (WAV needs none).
    Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3);

    // 44.1kHz stereo, a modest buffer. NB: a fresh device != failure if codecs missing.
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        return false;
    }
    Mix_AllocateChannels(32);  // simultaneous one-shot SFX
    m_open = true;
    return true;
}

void SDLMixerBackend::shutdown() {
    if (!m_open) return;

    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    m_currentMusic = nullptr;   // the Mix_Music it pointed at is freed just below

    for (Mix_Chunk* c : m_sounds) if (c) Mix_FreeChunk(c);
    for (Mix_Music* m : m_musics) if (m) Mix_FreeMusic(m);
    m_sounds.clear();
    m_musics.clear();
    m_soundIds.clear();
    m_musicIds.clear();

    Mix_CloseAudio();
    Mix_Quit();
    m_open = false;
}

int SDLMixerBackend::loadSound(const std::string& path) {
    auto it = m_soundIds.find(path);
    if (it != m_soundIds.end()) return it->second;          // cached

    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if (!chunk) return -1;                                   // load failure -> caller skips

    const int id = static_cast<int>(m_sounds.size());
    m_sounds.push_back(chunk);
    m_soundIds[path] = id;
    return id;
}

int SDLMixerBackend::loadMusic(const std::string& path) {
    auto it = m_musicIds.find(path);
    if (it != m_musicIds.end()) return it->second;

    Mix_Music* music = Mix_LoadMUS(path.c_str());
    if (!music) return -1;

    const int id = static_cast<int>(m_musics.size());
    m_musics.push_back(music);
    m_musicIds[path] = id;
    return id;
}

void SDLMixerBackend::unloadSound(const std::string& path) {
    auto it = m_soundIds.find(path);
    if (it == m_soundIds.end()) return;
    const int id = it->second;
    if (id >= 0 && id < static_cast<int>(m_sounds.size()) && m_sounds[static_cast<size_t>(id)]) {
        Mix_FreeChunk(m_sounds[static_cast<size_t>(id)]);
        m_sounds[static_cast<size_t>(id)] = nullptr;  // leave a hole; ids stay stable
    }
    m_soundIds.erase(it);  // a future load of this path gets a fresh id
}

int SDLMixerBackend::playSound(int soundId, float volume, float pan, bool loop) {
    if (!m_open || soundId < 0 || soundId >= static_cast<int>(m_sounds.size())) return -1;
    Mix_Chunk* chunk = m_sounds[static_cast<size_t>(soundId)];
    if (!chunk) return -1;

    const int channel = Mix_PlayChannel(-1, chunk, loop ? -1 : 0);  // -1 loops forever
    if (channel < 0) return -1;                                     // all channels busy

    Mix_Volume(channel, toMixVolume(volume));

    // pan [-1,1] -> Mix_SetPanning left/right (0..255). Center = full both.
    if (pan < 0.0f) pan = std::max(pan, -1.0f);
    else if (pan > 0.0f) pan = std::min(pan, 1.0f);
    const Uint8 left  = (pan <= 0.0f) ? 255 : static_cast<Uint8>((1.0f - pan) * 255.0f);
    const Uint8 right = (pan >= 0.0f) ? 255 : static_cast<Uint8>((1.0f + pan) * 255.0f);
    Mix_SetPanning(channel, left, right);
    return channel;  // the handle the module tracks for sound:sfx:stop
}

void SDLMixerBackend::stopSound(int handle, int fadeMs) {
    if (!m_open || handle < 0) return;
    if (fadeMs > 0) Mix_FadeOutChannel(handle, fadeMs);
    else            Mix_HaltChannel(handle);
}

void SDLMixerBackend::stopAllSounds(int fadeMs) {
    if (!m_open) return;
    if (fadeMs > 0) Mix_FadeOutChannel(-1, fadeMs);  // -1 = all channels
    else            Mix_HaltChannel(-1);
}

void SDLMixerBackend::setSoundVolume(int handle, float volume) {
    // Live per-channel gain for adaptive stems. handle = the channel Mix_PlayChannel returned.
    if (!m_open || handle < 0) return;
    Mix_Volume(handle, toMixVolume(volume));
}

void SDLMixerBackend::playMusic(int musicId, bool loop, int fadeMs, float volume) {
    if (!m_open || musicId < 0 || musicId >= static_cast<int>(m_musics.size())) return;
    Mix_Music* music = m_musics[static_cast<size_t>(musicId)];
    if (!music) return;

    Mix_VolumeMusic(toMixVolume(volume));
    const int loops = loop ? -1 : 1;   // -1 = infinite (SDL_mixer), 1 = play once
    if (fadeMs > 0) Mix_FadeInMusic(music, loops, fadeMs);
    else            Mix_PlayMusic(music, loops);
    m_currentMusic = music;            // track for sound:music:position (slice 6b)
}

void SDLMixerBackend::stopMusic(int fadeMs) {
    if (!m_open) return;
    if (fadeMs > 0) Mix_FadeOutMusic(fadeMs);
    else            Mix_HaltMusic();
    m_currentMusic = nullptr;
}

void SDLMixerBackend::setMusicVolume(float volume) {
    if (!m_open) return;
    Mix_VolumeMusic(toMixVolume(volume));
}

double SDLMixerBackend::getMusicPosition() const {
    // SECONDS into the current track. Mix_GetMusicPosition arrived in SDL_mixer 2.6; on older
    // headers there is no way to query it -> -1 ("unknown"), so the module publishes nothing.
    if (!m_open || !m_currentMusic) return -1.0;
#if defined(SDL_MIXER_VERSION_ATLEAST) && SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
    const double pos = Mix_GetMusicPosition(m_currentMusic);
    return pos;   // Mix returns -1.0 itself if the codec can't report position
#else
    return -1.0;
#endif
}

double SDLMixerBackend::getMusicDuration() const {
    // Total SECONDS of the current track. Mix_MusicDuration also arrived in SDL_mixer 2.6.
    if (!m_open || !m_currentMusic) return -1.0;
#if defined(SDL_MIXER_VERSION_ATLEAST) && SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
    return Mix_MusicDuration(m_currentMusic);
#else
    return -1.0;
#endif
}

} // namespace sound
} // namespace grove
