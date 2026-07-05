/**
 * Integration Test IT_058: VideoModule end-to-end — playback + A/V sync (video slice 6c).
 *
 * Drives the module with real video:* topics + a MockVideoBackend and asserts the presentation it
 * publishes: the frame stream (video:frame), the per-frame pixel upload (render:texture:upload with a
 * blob), the one-time texture+sprite setup, video:ended at the last frame, and — the load-bearing bit
 * — that the AUDIO clock is the master (frames follow sound:music:position, and a clock jump DROPS the
 * skipped frames rather than rendering them). "No E2E = it doesn't exist."
 */

#include <catch2/catch_test_macros.hpp>
#include "VideoModule.h"
#include "../mocks/MockVideoBackend.h"
#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace grove;
using nlohmann::json;

static std::string uid(const std::string& p) {
    auto n = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream o; o << p << "_" << n; return o.str();
}

namespace {
struct Harness {
    std::unique_ptr<VideoModule> module;
    test::MockVideoBackend* mock = nullptr;
    std::shared_ptr<IIO> moduleIO, pubIO, obsIO;

    // Observations.
    std::vector<int> frameIndices;         // video:frame index stream
    int uploadCount = 0; int uploadBlobBytes = -1;
    int endedCount = 0;
    int createCount = 0, spriteAddCount = 0;
    std::string musicPath; int musicCount = 0;

    Harness(int frames, double fps, const std::string& audio) {
        auto& mgr = IntraIOManager::getInstance();
        moduleIO = mgr.createInstance(uid("vid_mod"));
        pubIO    = mgr.createInstance(uid("vid_pub"));
        obsIO    = mgr.createInstance(uid("vid_obs"));

        auto backend = std::make_unique<test::MockVideoBackend>();
        backend->frames = frames; backend->fpsVal = fps; backend->w = 4; backend->h = 4;
        backend->audio = audio;
        mock = backend.get();

        module = std::make_unique<VideoModule>();
        module->setBackend(std::move(backend));
        JsonDataNode cfg("config");
        module->setConfiguration(cfg, moduleIO.get(), nullptr);

        obsIO->subscribe("video:frame", [this](const Message& m) { frameIndices.push_back(m.data->getInt("index", -1)); });
        obsIO->subscribe("video:ended", [this](const Message&) { ++endedCount; });
        obsIO->subscribe("render:texture:create", [this](const Message&) { ++createCount; });
        obsIO->subscribe("render:sprite:add", [this](const Message&) { ++spriteAddCount; });
        obsIO->subscribe("render:texture:upload", [this](const Message& m) {
            ++uploadCount;
            if (const auto* b = m.data->getBlob("pixels")) uploadBlobBytes = static_cast<int>(b->size());
        });
        obsIO->subscribe("sound:music", [this](const Message& m) { musicPath = m.data->getString("path", ""); ++musicCount; });
    }

    void drain() { while (obsIO->hasMessages() > 0) obsIO->pullAndDispatch(); }
    // Deliver a message WITHOUT advancing the clock (dt=0).
    void send(const std::string& topic, json payload) {
        pubIO->publish(topic, std::make_unique<JsonDataNode>("m", std::move(payload)));
        JsonDataNode in("input"); in.setDouble("deltaTime", 0.0);
        module->process(in); drain();
    }
    // Advance the clock by dt (silent clip: the dt clock is master).
    void tick(double dt) {
        JsonDataNode in("input"); in.setDouble("deltaTime", dt);
        module->process(in); drain();
    }
};
} // namespace

TEST_CASE("IT_058: VideoModule plays a silent clip frame-by-frame to the end (slice 6c)", "[integration][video][e2e]") {
    Harness h(/*frames=*/6, /*fps=*/30.0, /*audio=*/"");   // 6 frames @30fps = 0.2 s, silent -> dt clock

    // Play: sets up the texture + sprite ONCE and presents frame 0 immediately.
    h.send("video:play", json{{"path", "clip.mp4"}, {"x", 100}, {"y", 100}});
    REQUIRE(h.createCount == 1);
    REQUIRE(h.spriteAddCount == 1);
    REQUIRE(h.frameIndices == std::vector<int>{0});   // frame 0 on play
    REQUIRE(h.uploadCount == 1);
    REQUIRE(h.uploadBlobBytes == 4 * 4 * 4);           // w*h*4 RGBA bytes
    REQUIRE(h.musicCount == 0);                        // silent -> no audio track

    // Advance one frame per 1/30 s tick: frames 1..5, then a couple more ticks push the clock past the
    // clip's 0.2 s length -> video:ended (extra past-end ticks hold the last frame, add no new frame).
    for (int i = 0; i < 8; ++i) h.tick(1.0 / 30.0);

    REQUIRE(h.frameIndices == std::vector<int>{0, 1, 2, 3, 4, 5});   // every frame, in order, no drops
    REQUIRE(h.endedCount == 1);
    // fetchedFrames on the backend mirrors it (frame 0 + 1..5).
    REQUIRE(h.mock->fetchedFrames == std::vector<int>{0, 1, 2, 3, 4, 5});
}

TEST_CASE("IT_058b: the audio track is the master clock, and a clock jump DROPS skipped frames", "[integration][video][e2e]") {
    Harness h(/*frames=*/30, /*fps=*/30.0, /*audio=*/"vo/clip.ogg");   // 1 s, with audio

    h.send("video:play", json{{"path", "clip.mp4"}});
    REQUIRE(h.musicCount == 1);
    REQUIRE(h.musicPath == "vo/clip.ogg");   // the module handed the audio to SoundManager as the clock
    REQUIRE(h.frameIndices == std::vector<int>{0});

    // With audio, dt ticks do NOTHING — only the audio position drives the picture.
    h.tick(1.0);
    REQUIRE(h.frameIndices == std::vector<int>{0});   // no sound:music:position yet -> still on frame 0

    // The audio clock jumps to 0.1 s -> frame 3 (frames 1 & 2 were skipped, must be DROPPED not rendered).
    h.send("sound:music:position", json{{"path", "vo/clip.ogg"}, {"elapsed", 0.1}, {"duration", 1.0}});
    REQUIRE(h.frameIndices == std::vector<int>{0, 3});
    REQUIRE(h.mock->fetchedFrames == std::vector<int>{0, 3});   // frames 1,2 never fetched = dropped
}
