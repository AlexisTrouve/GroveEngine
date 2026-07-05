#pragma once

/**
 * VideoModule — video playback as an IModule (video slice 6c).
 *
 * WHAT  : Plays a video source (decoded by an injected IVideoBackend) onto a runtime texture, kept in
 *         A/V sync with an audio track. On `video:play` it opens the backend, draws a sprite bound to
 *         the video texture, and each frame — driven by the MASTER CLOCK (the audio track's position,
 *         or its own dt clock when silent) — uploads the current frame's pixels to that texture.
 *
 * TOPICS (consumed):
 *   video:play  { path, audio?, textureId?, x?, y?, w?, h?, layer? }   open + start
 *   video:stop  {}                                                     stop + release
 *   video:pause { paused }                                             hold the clock
 *   sound:music:position { elapsed }                                   the audio master clock (slice 6b)
 *
 * TOPICS (published):
 *   render:texture:create { id, w, h }                 once, on play — the video texture
 *   render:sprite:add     { renderId, asset:id, x,y,w,h, layer }   once, on play — the on-screen quad
 *   render:texture:upload { id, w, h, +blob "pixels" } per changed frame — the new RGBA pixels (slice 6c-0c)
 *   sound:music           { path }                     on play, if the clip has audio (drives the clock)
 *   video:frame           { index, w, h }              per changed frame (event)
 *   video:ended           { }                          the clip reached its last frame
 *
 * WHY   : Mirrors SoundManager — the decoder is behind IVideoBackend (a mock in tests, the ffmpeg CLI
 *         in 6c-1) so the sync + topic logic is headless-testable. Audio is the master clock (music
 *         plays at wall-clock; the picture follows) — the VideoSync math holds/advances/drops frames.
 */

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/ITaskScheduler.h>
#include "IVideoBackend.h"
#include "VideoSync.h"

#include <cstdint>
#include <memory>
#include <string>

namespace grove {

class VideoModule : public IModule {
public:
    VideoModule();
    ~VideoModule() override;

    // Inject the decode backend (a test mock, or the ffmpeg-CLI backend in 6c-1). Set before play.
    void setBackend(std::unique_ptr<video::IVideoBackend> backend);

    // IModule interface
    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override;
    void process(const IDataNode& input) override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    std::string getType() const override { return "video"; }
    bool isIdle() const override { return !m_playing; }

private:
    void handleMessage(const Message& msg);
    void startPlayback(const IDataNode& d);
    void stopPlayback();
    void presentFrame(int index);   // upload this frame's pixels to the video texture

    IIO* m_io = nullptr;
    std::unique_ptr<IDataNode> m_config;
    std::unique_ptr<video::IVideoBackend> m_backend;
    video::VideoSync m_sync;

    bool m_playing = false;
    bool m_paused = false;
    bool m_hasAudio = false;
    double m_audioClock = 0.0;   // from sound:music:position — the master clock while playing with audio
    double m_dtClock = 0.0;      // own accumulated dt — the master clock for a silent clip

    // On-screen placement (from video:play).
    std::string m_textureId = "video";
    float m_x = 0.0f, m_y = 0.0f, m_w = 0.0f, m_h = 0.0f;
    int m_layer = 0;
    uint32_t m_spriteRenderId = 0;

    uint64_t m_framesPresented = 0;
};

} // namespace grove
