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
    virtual int loadSound(const std::string& path) = 0;
    virtual int loadMusic(const std::string& path) = 0;

    // One-shot SFX at an EFFECTIVE volume [0,1] and pan [-1,1].
    virtual void playSound(int soundId, float volume, float pan) = 0;

    // Start music (replaces the current track): loop, fade-in ms, EFFECTIVE volume [0,1].
    virtual void playMusic(int musicId, bool loop, int fadeMs, float volume) = 0;

    // Stop the current music, optionally fading out over fadeMs.
    virtual void stopMusic(int fadeMs) = 0;

    // Re-apply the music's EFFECTIVE volume live (master/music bus changed mid-playback).
    virtual void setMusicVolume(float volume) = 0;
};

} // namespace sound
} // namespace grove
