#include "VideoModule.h"

#include <grove/IDataNode.h>
#include <grove/JsonDataNode.h>

namespace grove {

namespace {
// One video sprite per module; a high renderId so it never collides with the UI's (which count from 1).
constexpr uint32_t kVideoSpriteRenderId = 0x7F000001u;
} // namespace

VideoModule::VideoModule()
    : m_config(std::make_unique<JsonDataNode>("config")) {}

VideoModule::~VideoModule() = default;

void VideoModule::setBackend(std::unique_ptr<video::IVideoBackend> backend) {
    m_backend = std::move(backend);
}

void VideoModule::setConfiguration(const IDataNode& /*config*/, IIO* io, ITaskScheduler* /*scheduler*/) {
    m_io = io;
    if (m_io) {
        auto cb = [this](const Message& msg) { handleMessage(msg); };
        m_io->subscribe("video:play", cb);
        m_io->subscribe("video:stop", cb);
        m_io->subscribe("video:pause", cb);
        m_io->subscribe("sound:music:position", cb);   // the audio master clock (slice 6b)
    }
}

void VideoModule::process(const IDataNode& input) {
    // Drain first (a video:play / sound:music:position handled this frame updates the state below).
    while (m_io && m_io->hasMessages() > 0) m_io->pullAndDispatch();

    if (!m_playing || !m_backend || m_paused) return;

    // Master clock. With audio: DEAD-RECKON — advance by dt each frame for smooth 60fps video, and let
    // sound:music:position SNAP m_audioClock back to the true audio time (it arrives throttled ~15 Hz, so
    // dead-reckoning between snaps keeps the picture fluid without drifting off the audio). Silent: dt clock.
    const double dt = input.getDouble("deltaTime", 0.016);
    const double clock = m_hasAudio ? (m_audioClock += dt) : (m_dtClock += dt);

    const video::FrameTick t = m_sync.update(clock);
    if (t.changed) presentFrame(t.index);
    if (t.ended) {
        m_io->publish("video:ended", std::make_unique<JsonDataNode>("ended"));
        stopPlayback();
    }
}

void VideoModule::handleMessage(const Message& msg) {
    if (!msg.data) return;
    const IDataNode& d = *msg.data;

    if (msg.topic == "video:play")        startPlayback(d);
    else if (msg.topic == "video:stop")   stopPlayback();
    else if (msg.topic == "video:pause")  m_paused = d.getBool("paused", true);
    else if (msg.topic == "sound:music:position") {
        // SNAP the dead-reckoned clock back to the true audio position (corrects accumulated dt drift).
        if (m_hasAudio) m_audioClock = d.getDouble("elapsed", m_audioClock);
    }
}

void VideoModule::startPlayback(const IDataNode& d) {
    if (!m_backend || !m_io) return;
    const std::string path = d.getString("path", "");
    if (path.empty() || !m_backend->open(path)) return;

    m_sync.configure(m_backend->fps(), m_backend->frameCount());
    m_sync.reset();

    const int bw = m_backend->width(), bh = m_backend->height();
    m_textureId = d.getString("textureId", "video");
    m_x = static_cast<float>(d.getDouble("x", 0.0));
    m_y = static_cast<float>(d.getDouble("y", 0.0));
    m_w = static_cast<float>(d.getDouble("w", static_cast<double>(bw)));
    m_h = static_cast<float>(d.getDouble("h", static_cast<double>(bh)));
    m_layer = static_cast<int>(d.getInt("layer", 0));
    m_spriteRenderId = kVideoSpriteRenderId;

    // Create the video texture + the on-screen quad ONCE; per-frame we only refresh the texture pixels.
    { auto t = std::make_unique<JsonDataNode>("tex");
      t->setString("id", m_textureId); t->setInt("w", bw); t->setInt("h", bh);
      m_io->publish("render:texture:create", std::move(t)); }
    { auto s = std::make_unique<JsonDataNode>("sprite");
      s->setInt("renderId", static_cast<int>(m_spriteRenderId));
      s->setString("asset", m_textureId);
      s->setDouble("x", static_cast<double>(m_x + m_w * 0.5f));   // sprite pos = centre (shader centres the quad)
      s->setDouble("y", static_cast<double>(m_y + m_h * 0.5f));
      s->setDouble("scaleX", static_cast<double>(m_w));
      s->setDouble("scaleY", static_cast<double>(m_h));
      s->setInt("layer", m_layer);
      m_io->publish("render:sprite:add", std::move(s)); }

    // Audio master clock: an explicit "audio" wins, else whatever the backend reports.
    std::string audio = d.getString("audio", "");
    if (audio.empty()) audio = m_backend->audioTrack();
    m_hasAudio = !audio.empty();
    m_audioClock = 0.0;
    m_dtClock = 0.0;
    if (m_hasAudio) {
        auto m = std::make_unique<JsonDataNode>("music");
        m->setString("path", audio);
        m_io->publish("sound:music", std::move(m));   // SoundManager plays it -> drives sound:music:position
    }

    m_playing = true;
    m_paused = false;
    m_framesPresented = 0;

    // Present frame 0 immediately (at clock 0), before any dt/audio advance — so a paused-at-start
    // video still shows its first frame, and the clock-driven ticks then advance from 1 onward.
    const video::FrameTick t0 = m_sync.update(0.0);
    presentFrame(t0.index);
}

void VideoModule::presentFrame(int index) {
    if (!m_backend || !m_io) return;
    const uint8_t* rgba = m_backend->frameRGBA(index);
    if (!rgba) return;
    const int bw = m_backend->width(), bh = m_backend->height();

    // Refresh the video texture's pixels (raw RGBA blob — the arbitrary-pixel upload path, slice 6c-0c).
    auto up = std::make_unique<JsonDataNode>("tex");
    up->setString("id", m_textureId);
    up->setInt("w", bw);
    up->setInt("h", bh);
    up->setBlob("pixels", rgba, static_cast<size_t>(bw) * static_cast<size_t>(bh) * 4u);
    m_io->publish("render:texture:upload", std::move(up));

    // Frame event (game logic / tests).
    auto ev = std::make_unique<JsonDataNode>("frame");
    ev->setInt("index", index);
    ev->setInt("w", bw);
    ev->setInt("h", bh);
    m_io->publish("video:frame", std::move(ev));
    ++m_framesPresented;
}

void VideoModule::stopPlayback() {
    if (m_backend) m_backend->close();
    if (m_playing && m_io && m_spriteRenderId) {
        auto s = std::make_unique<JsonDataNode>("sprite");
        s->setInt("renderId", static_cast<int>(m_spriteRenderId));
        m_io->publish("render:sprite:remove", std::move(s));
    }
    m_playing = false;
    m_paused = false;
    m_hasAudio = false;
}

void VideoModule::shutdown() { stopPlayback(); }

std::unique_ptr<IDataNode> VideoModule::getState() {
    // Video playback is transient — nothing meaningful survives a hot-reload (the decoder's open
    // file + frame position can't be carried). Report the flag only.
    auto state = std::make_unique<JsonDataNode>("state");
    state->setBool("playing", m_playing);
    return state;
}

void VideoModule::setState(const IDataNode& /*state*/) { /* transient — no restore */ }

const IDataNode& VideoModule::getConfiguration() { return *m_config; }

std::unique_ptr<IDataNode> VideoModule::getHealthStatus() {
    auto health = std::make_unique<JsonDataNode>("health");
    health->setString("status", "ok");
    health->setBool("backendReady", m_backend != nullptr);
    health->setBool("playing", m_playing);
    health->setInt("framesPresented", static_cast<int>(m_framesPresented));
    return health;
}

} // namespace grove
