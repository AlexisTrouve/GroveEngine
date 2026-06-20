#pragma once

/**
 * grove::sound::ISoundBackend — audio backend abstraction (sound system, slice 1).
 *
 * WHAT  : The interface SoundManagerModule talks to. SDL_mixer (slice 2) implements it for real;
 *         a mock implements it for headless tests. Mirrors the RHIDevice pattern: the backend
 *         dependency (and its build/threading/format concerns) never leaks into the module's
 *         topic/bus logic.
 *
 * WHY   : Audio output can only be verified by ear, but the routing + master/music/sfx volume
 *         math is pure logic. Hiding the backend behind this interface makes that logic fully
 *         headless-testable (assert which backend calls the module makes, at what volume).
 *
 * HOW   : Volumes passed here are already EFFECTIVE (the module has applied the buses), in
 *         [0,1]. loadSound/loadMusic cache by path and return a stable handle (>=0), -1 on
 *         failure. Pan is [-1,1] (left..right).
 */

#include <string>

namespace grove {
namespace sound {

struct ISoundBackend {
    virtual ~ISoundBackend() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // Load + cache by path; stable handle (>=0) or -1 on failure. Idempotent per path.
    // (sound:preload uses loadSound to warm the cache without playing.)
    virtual int loadSound(const std::string& path) = 0;
    virtual int loadMusic(const std::string& path) = 0;

    // Free a cached SFX by path (sound:unload). No-op if not loaded.
    virtual void unloadSound(const std::string& path) = 0;

    // Play an SFX at an EFFECTIVE volume [0,1] and pan [-1,1], optionally looping. Returns a
    // playback HANDLE (>=0) the caller can later stop, or -1 if it couldn't start. The module
    // maps a game-supplied id to this handle so a looping SFX can be controlled.
    virtual int playSound(int soundId, float volume, float pan, bool loop) = 0;

    // Stop a specific SFX playback (by the handle playSound returned), optionally fading out.
    virtual void stopSound(int handle, int fadeMs) = 0;

    // Stop ALL SFX playbacks, optionally fading out.
    virtual void stopAllSounds(int fadeMs) = 0;

    // Re-apply the EFFECTIVE volume [0,1] to a PLAYING SFX (by the handle playSound returned).
    // The adaptive-music mixer uses this to ramp a looping stem's gain live as the game's tension
    // changes — symmetric to setMusicVolume, but per-SFX-channel.
    virtual void setSoundVolume(int handle, float volume) = 0;

    // Start music (replaces the current track): loop, fade-in ms, EFFECTIVE volume [0,1].
    virtual void playMusic(int musicId, bool loop, int fadeMs, float volume) = 0;

    // Stop the current music, optionally fading out over fadeMs.
    virtual void stopMusic(int fadeMs) = 0;

    // Re-apply the music's EFFECTIVE volume live (master/music bus changed mid-playback).
    virtual void setMusicVolume(float volume) = 0;
};

} // namespace sound
} // namespace grove
