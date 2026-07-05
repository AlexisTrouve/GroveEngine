#pragma once

/**
 * grove::video::FfmpegCliBackend — real MP4/H.264/AAC decode by driving the ffmpeg CLI (video slice 6c-1).
 *
 * WHAT  : An IVideoBackend that plays any ffmpeg-supported container (MP4, WebM, MOV…) WITHOUT linking
 *         libav*. It probes metadata with ffprobe, extracts the audio track to a temp OGG (which the
 *         sound system plays as the A/V master clock), and pipes raw RGBA frames from a long-running
 *         `ffmpeg … -f rawvideo -pix_fmt rgba -` subprocess.
 *
 * WHY   : Alexi's pick — the ffmpeg CLI is already on the box, so we get real MP4 at runtime with ZERO
 *         heavy build dependency (libav dev libs aren't on the toolchain; linking them is a chantier).
 *         To SHIP a game, bundle ffmpeg.exe alongside it. Kept behind IVideoBackend so the module stays
 *         decoder-agnostic + headless-testable via the mock; this backend is only exercised where ffmpeg
 *         is present (a gated test, like the SDL sound demo is "verified by ear").
 *
 * HOW   : FORWARD-ONLY — playback advances (and DROPS skipped frames), so frameRGBA(index) decodes
 *         forward from the pipe, discarding frames the sync skipped, and never rewinds (seek = a
 *         follow-on). popen (portable: _popen/_pclose on Windows, popen/pclose on POSIX) reads the raw
 *         byte stream. Needs `ffmpeg` + `ffprobe` on PATH (or pass the exe path).
 */

#include "IVideoBackend.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace grove {
namespace video {

class FfmpegCliBackend : public IVideoBackend {
public:
    explicit FfmpegCliBackend(std::string ffmpegExe = "ffmpeg");
    ~FfmpegCliBackend() override;

    bool open(const std::string& path) override;
    void close() override;

    int    width() const override { return m_width; }
    int    height() const override { return m_height; }
    double fps() const override { return m_fps; }
    int    frameCount() const override { return m_frameCount; }

    const uint8_t* frameRGBA(int index) override;
    std::string audioTrack() const override { return m_audioPath; }

private:
    bool probe(const std::string& path);   // ffprobe -> width/height/fps/frameCount/duration

    std::string m_ffmpeg;
    std::string m_ffprobe;

    int    m_width = 0, m_height = 0;
    int    m_frameCount = -1;
    double m_fps = 0.0, m_durationSec = 0.0;

    std::FILE* m_pipe = nullptr;          // the ffmpeg raw-RGBA frame pipe
    int m_currentIndex = -1;              // last frame read off the pipe (forward-only)
    std::vector<uint8_t> m_frame;         // the last-decoded frame (width*height*4)
    std::string m_audioPath;              // extracted temp audio, removed on close ("" = silent)
};

} // namespace video
} // namespace grove
