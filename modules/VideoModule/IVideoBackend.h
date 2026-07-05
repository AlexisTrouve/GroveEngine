#pragma once

/**
 * grove::video::IVideoBackend — video decode abstraction (video slice 6c).
 *
 * WHAT  : The interface VideoModule talks to for PIXELS. A backend opens a source, reports its
 *         geometry / frame rate / length, and hands back the RGBA bytes of a given frame. The real
 *         backend (6c-1) shells out to the ffmpeg CLI (H.264/AAC MP4); an image-sequence backend
 *         loads PNG frames; a mock feeds synthetic frames for headless tests.
 *
 * WHY   : Mirrors ISoundBackend — the heavy / platform-specific decoder (ffmpeg subprocess + pipes)
 *         never leaks into the module's A/V-sync + topic logic, which stays headless-testable. The
 *         decoder is injected (setBackend) and optional (build flag), so a host that doesn't want
 *         video links nothing extra.
 *
 * HOW   : frameRGBA(index) returns width*height*4 bytes (RGBA8), owned by the backend and valid until
 *         the next call — VideoModule uploads a COPY to the GPU immediately. audioTrack() names the
 *         audio the module hands to the sound system as the MASTER CLOCK ("" = silent → dt clock).
 *         frameCount()<0 = unknown length (a live stream); fps()>0.
 */

#include <cstdint>
#include <string>

namespace grove {
namespace video {

struct IVideoBackend {
    virtual ~IVideoBackend() = default;

    // Open a source. Returns false if it couldn't be opened (the module then does nothing).
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;

    virtual int    width() const = 0;
    virtual int    height() const = 0;
    virtual double fps() const = 0;
    virtual int    frameCount() const = 0;   // < 0 = unknown length (streaming)

    // RGBA8 bytes of frame `index` (width*height*4), or nullptr if unavailable. Owned by the backend,
    // valid until the next frameRGBA() call.
    virtual const uint8_t* frameRGBA(int index) = 0;

    // The audio track to drive as the master clock ("" = the clip is silent → the module uses dt).
    virtual std::string audioTrack() const { return {}; }
};

} // namespace video
} // namespace grove
