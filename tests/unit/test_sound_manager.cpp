/**
 * Unit Tests: SoundManagerModule (sound system, slice 1 — headless).
 *
 * WHAT  : Locks the topic -> backend + bus-volume logic of the sound module, with NO SDL_mixer.
 *         The module consumes sound:* IIO topics and drives an ISoundBackend; a MockSoundBackend
 *         records the calls so we assert exactly what the module asks the backend to do and at
 *         what effective volume.
 *
 * WHY    : Audio output needs ears (the real SDLMixerBackend + manual check come in slice 2),
 *          but the routing and the master/music/sfx bus math are pure logic — fully headless-
 *          testable via the mock, exactly like MockRHIDevice for the renderer.
 *
 * HOW    : A test IIO instance feeds sound:* messages; module.process() pulls + dispatches.
 *          The mock backend (injected) records playSound/playMusic/stopMusic/setMusicVolume.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SoundManager/SoundManagerModule.h"
#include "SoundManager/AdaptiveMixer.h"
#include "SoundManager/BeatClock.h"
#include "../mocks/MockSoundBackend.h"

#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>

using namespace grove;
using namespace grove::sound;
using Catch::Matchers::WithinAbs;

static std::string uid(const std::string& p) {
    auto n = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream o; o << p << "_" << n; return o.str();
}

// Last effective volume the module pushed to a given playback handle via setSoundVolume, or -1.
static float lastVolForHandle(const test::MockSoundBackend& m, int handle) {
    float v = -1.0f;
    for (const auto& c : m.setSoundVolumeCalls) if (c.handle == handle) v = c.volume;
    return v;
}

// How many setSoundVolume calls targeted a given handle (0 = its gain was never pushed/changed).
static int countVolForHandle(const test::MockSoundBackend& m, int handle) {
    int n = 0;
    for (const auto& c : m.setSoundVolumeCalls) if (c.handle == handle) ++n;
    return n;
}

// Build a module wired to a fresh mock backend + test IIO. Returns the module, the publisher
// IIO (to send sound:* on), and a raw pointer to the mock (to assert).
struct Harness {
    std::unique_ptr<SoundManagerModule> module;
    std::shared_ptr<IIO> moduleIO;
    std::shared_ptr<IIO> pubIO;
    test::MockSoundBackend* mock = nullptr;

    Harness() {
        auto& mgr = IntraIOManager::getInstance();
        moduleIO = mgr.createInstance(uid("sound_mod"));
        pubIO = mgr.createInstance(uid("sound_pub"));

        auto backend = std::make_unique<test::MockSoundBackend>();
        mock = backend.get();

        module = std::make_unique<SoundManagerModule>();
        module->setBackend(std::move(backend));

        JsonDataNode config("config");
        module->setConfiguration(config, moduleIO.get(), nullptr);
    }

    void publish(const std::string& topic, std::unique_ptr<JsonDataNode> data) {
        pubIO->publish(topic, std::move(data));
    }
    void pump() {
        JsonDataNode in("input");
        module->process(in);
    }
    void pump(double dt) {
        JsonDataNode in("input");
        in.setDouble("deltaTime", dt);
        module->process(in);
    }
};

TEST_CASE("SoundManager - sound:sfx loads and plays a one-shot at full volume by default", "[sound][unit]") {
    Harness h;
    auto n = std::make_unique<JsonDataNode>("sfx");
    n->setString("path", "boom.wav");
    h.publish("sound:sfx", std::move(n));
    h.pump();

    REQUIRE(h.mock->playSoundCalls.size() == 1);
    REQUIRE(h.mock->playSoundCalls[0].soundId == h.mock->idForPath("boom.wav"));
    REQUIRE_THAT(h.mock->playSoundCalls[0].volume, WithinAbs(1.0f, 0.0001f));
    REQUIRE_THAT(h.mock->playSoundCalls[0].pan, WithinAbs(0.0f, 0.0001f));
}

TEST_CASE("SoundManager - sfx bus scales SFX volume", "[sound][unit]") {
    Harness h;
    { auto v = std::make_unique<JsonDataNode>("vol"); v->setString("bus", "sfx"); v->setDouble("value", 0.5); h.publish("sound:volume", std::move(v)); }
    { auto n = std::make_unique<JsonDataNode>("sfx"); n->setString("path", "x.wav"); n->setDouble("volume", 1.0); h.publish("sound:sfx", std::move(n)); }
    h.pump();

    REQUIRE(h.mock->playSoundCalls.size() == 1);
    REQUIRE_THAT(h.mock->playSoundCalls[0].volume, WithinAbs(0.5f, 0.0001f));  // master(1)*sfx(.5)*call(1)
}

TEST_CASE("SoundManager - per-call volume and pan pass through", "[sound][unit]") {
    Harness h;
    auto n = std::make_unique<JsonDataNode>("sfx");
    n->setString("path", "x.wav"); n->setDouble("volume", 0.4); n->setDouble("pan", -0.5);
    h.publish("sound:sfx", std::move(n));
    h.pump();

    REQUIRE_THAT(h.mock->playSoundCalls[0].volume, WithinAbs(0.4f, 0.0001f));
    REQUIRE_THAT(h.mock->playSoundCalls[0].pan, WithinAbs(-0.5f, 0.0001f));
}

TEST_CASE("SoundManager - sound:music plays with loop, fade and effective volume", "[sound][unit]") {
    Harness h;
    auto n = std::make_unique<JsonDataNode>("music");
    n->setString("path", "theme.ogg"); n->setBool("loop", true); n->setInt("fadeMs", 500); n->setDouble("volume", 0.8);
    h.publish("sound:music", std::move(n));
    h.pump();

    REQUIRE(h.mock->playMusicCalls.size() == 1);
    REQUIRE(h.mock->playMusicCalls[0].musicId == h.mock->idForPath("theme.ogg"));
    REQUIRE(h.mock->playMusicCalls[0].loop == true);
    REQUIRE(h.mock->playMusicCalls[0].fadeMs == 500);
    REQUIRE_THAT(h.mock->playMusicCalls[0].volume, WithinAbs(0.8f, 0.0001f));
}

TEST_CASE("SoundManager - sound:music:stop forwards the fade", "[sound][unit]") {
    Harness h;
    auto n = std::make_unique<JsonDataNode>("stop"); n->setInt("fadeMs", 1000);
    h.publish("sound:music:stop", std::move(n));
    h.pump();

    REQUIRE(h.mock->stopMusicCalls.size() == 1);
    REQUIRE(h.mock->stopMusicCalls[0] == 1000);
}

TEST_CASE("SoundManager - changing master/music bus re-applies live music volume", "[sound][unit]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("music"); n->setString("path", "t.ogg"); n->setDouble("volume", 1.0); h.publish("sound:music", std::move(n)); }
    h.pump();
    { auto v = std::make_unique<JsonDataNode>("vol"); v->setString("bus", "master"); v->setDouble("value", 0.5); h.publish("sound:volume", std::move(v)); }
    h.pump();

    REQUIRE_FALSE(h.mock->setMusicVolumeCalls.empty());
    REQUIRE_THAT(h.mock->setMusicVolumeCalls.back(), WithinAbs(0.5f, 0.0001f));  // master(.5)*music(1)*base(1)
}

TEST_CASE("SoundManager - effective volume is clamped to [0,1]", "[sound][unit]") {
    Harness h;
    { auto v = std::make_unique<JsonDataNode>("vol"); v->setString("bus", "master"); v->setDouble("value", 2.0); h.publish("sound:volume", std::move(v)); }
    { auto n = std::make_unique<JsonDataNode>("sfx"); n->setString("path", "x.wav"); n->setDouble("volume", 1.0); h.publish("sound:sfx", std::move(n)); }
    h.pump();

    REQUIRE_THAT(h.mock->playSoundCalls[0].volume, WithinAbs(1.0f, 0.0001f));  // clamped, not 2.0
}

TEST_CASE("SoundManager - repeated SFX of the same path reuse one loaded handle", "[sound][unit]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("sfx"); n->setString("path", "same.wav"); h.publish("sound:sfx", std::move(n)); }
    { auto n = std::make_unique<JsonDataNode>("sfx"); n->setString("path", "same.wav"); h.publish("sound:sfx", std::move(n)); }
    h.pump();

    REQUIRE(h.mock->playSoundCalls.size() == 2);
    REQUIRE(h.mock->playSoundCalls[0].soundId == h.mock->playSoundCalls[1].soundId);  // same cached id
    REQUIRE(h.mock->distinctLoads() == 1);                                  // loaded once
}

// ============================================================================
// Slice 3: resource management (preload/unload) + channel control (loop/stop/stopAll)
// ============================================================================

TEST_CASE("SoundManager - sound:preload loads a sound without playing it", "[sound][unit]") {
    Harness h;
    auto n = std::make_unique<JsonDataNode>("preload"); n->setString("path", "amb.wav");
    h.publish("sound:preload", std::move(n));
    h.pump();

    REQUIRE(h.mock->distinctLoads() == 1);          // cached...
    REQUIRE(h.mock->playSoundCalls.empty());        // ...but nothing played
}

TEST_CASE("SoundManager - sound:unload frees a cached sound", "[sound][unit]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("preload"); n->setString("path", "x.wav"); h.publish("sound:preload", std::move(n)); }
    h.pump();
    { auto n = std::make_unique<JsonDataNode>("unload"); n->setString("path", "x.wav"); h.publish("sound:unload", std::move(n)); }
    h.pump();

    REQUIRE(h.mock->unloadCalls.size() == 1);
    REQUIRE(h.mock->unloadCalls[0] == "x.wav");
}

TEST_CASE("SoundManager - looping SFX with an id can be stopped by that id", "[sound][unit]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("sfx"); n->setString("path", "engine.wav"); n->setBool("loop", true); n->setString("id", "engine"); h.publish("sound:sfx", std::move(n)); }
    h.pump();
    REQUIRE(h.mock->playSoundCalls.size() == 1);
    REQUIRE(h.mock->playSoundCalls[0].loop == true);
    const int handle = h.mock->playSoundCalls[0].handle;

    { auto n = std::make_unique<JsonDataNode>("stop"); n->setString("id", "engine"); n->setInt("fadeMs", 200); h.publish("sound:sfx:stop", std::move(n)); }
    h.pump();

    REQUIRE(h.mock->stopSoundCalls.size() == 1);
    REQUIRE(h.mock->stopSoundCalls[0].handle == handle);
    REQUIRE(h.mock->stopSoundCalls[0].fadeMs == 200);
}

TEST_CASE("SoundManager - sound:sfx:stopAll stops every SFX", "[sound][unit]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("sfx"); n->setString("path", "a.wav"); h.publish("sound:sfx", std::move(n)); }
    { auto n = std::make_unique<JsonDataNode>("sfx"); n->setString("path", "b.wav"); h.publish("sound:sfx", std::move(n)); }
    h.pump();
    { auto n = std::make_unique<JsonDataNode>("stopall"); n->setInt("fadeMs", 100); h.publish("sound:sfx:stopAll", std::move(n)); }
    h.pump();

    REQUIRE(h.mock->stopAllCalls.size() == 1);
    REQUIRE(h.mock->stopAllCalls[0] == 100);
}

TEST_CASE("SoundManager - stopping an unknown id is harmless", "[sound][unit]") {
    Harness h;
    auto n = std::make_unique<JsonDataNode>("stop"); n->setString("id", "nope");
    h.publish("sound:sfx:stop", std::move(n));
    h.pump();

    REQUIRE(h.mock->stopSoundCalls.empty());        // no handle mapped -> no backend call
}

TEST_CASE("SoundManager - a one-shot SFX (no id) needs no handle tracking", "[sound][unit]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("sfx"); n->setString("path", "boom.wav"); h.publish("sound:sfx", std::move(n)); }
    h.pump();
    REQUIRE(h.mock->playSoundCalls.size() == 1);
    REQUIRE(h.mock->playSoundCalls[0].loop == false);   // default
}

// ============================================================================
// Slice 1 (adaptive audio): tension-driven vertical layering.
//   - the PURE mixer (AdaptiveMixer): analytical oracles, no module/IIO/backend.
//   - the MODULE wiring (audio:* topics -> mixer -> backend): via the mock.
// ============================================================================

TEST_CASE("AdaptiveMixer - layers crossfade calm->peak by tension", "[sound][unit][adaptive]") {
    AdaptiveMixer mix;
    mix.addLayer("bed",  1.0f, 1.0f);   // constant bed
    mix.addLayer("rise", 0.0f, 1.0f);   // fades in with tension
    mix.addLayer("calm", 1.0f, 0.0f);   // fades out with tension

    // tension 0 (default): bed=1, rise=0, calm=1 (current+target snap on add).
    REQUIRE_THAT(mix.find("bed")->targetGain,  WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(mix.find("rise")->targetGain, WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(mix.find("calm")->targetGain, WithinAbs(1.0f, 1e-4f));

    mix.setTension(1.0f);
    REQUIRE_THAT(mix.find("bed")->targetGain,  WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(mix.find("rise")->targetGain, WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(mix.find("calm")->targetGain, WithinAbs(0.0f, 1e-4f));

    mix.setTension(0.5f);
    REQUIRE_THAT(mix.find("rise")->targetGain, WithinAbs(0.5f, 1e-4f));
    REQUIRE_THAT(mix.find("calm")->targetGain, WithinAbs(0.5f, 1e-4f));
}

TEST_CASE("AdaptiveMixer - tick ramps currentGain toward target (framerate-independent)", "[sound][unit][adaptive]") {
    AdaptiveMixer mix;
    mix.addLayer("rise", 0.0f, 1.0f);   // current=target=0 at tension 0
    mix.setTension(1.0f);               // target -> 1, current still 0
    REQUIRE_THAT(mix.find("rise")->currentGain, WithinAbs(0.0f, 1e-4f));

    const float rate = std::log(2.0f);  // r*dt = ln2 -> close exactly half the gap each tick
    mix.tick(1.0f, rate);
    REQUIRE_THAT(mix.find("rise")->currentGain, WithinAbs(0.5f, 1e-4f));
    mix.tick(1.0f, rate);
    REQUIRE_THAT(mix.find("rise")->currentGain, WithinAbs(0.75f, 1e-4f));
}

TEST_CASE("AdaptiveMixer - setMix overrides one layer's target", "[sound][unit][adaptive]") {
    AdaptiveMixer mix;
    mix.addLayer("rise", 0.0f, 1.0f);
    mix.setMix("rise", 0.25f);
    REQUIRE_THAT(mix.find("rise")->targetGain, WithinAbs(0.25f, 1e-4f));
}

TEST_CASE("SoundManager - audio:layer starts a looping stem at its calm gain", "[sound][unit][adaptive]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","bed");  n->setString("path","bed.ogg");  n->setDouble("gainCalm",1.0); n->setDouble("gainPeak",1.0); h.publish("audio:layer", std::move(n)); }
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","rise"); n->setString("path","rise.ogg"); n->setDouble("gainCalm",0.0); n->setDouble("gainPeak",1.0); h.publish("audio:layer", std::move(n)); }
    h.pump();

    REQUIRE(h.mock->playSoundCalls.size() == 2);
    REQUIRE(h.mock->playSoundCalls[0].loop == true);     // stems loop
    REQUIRE(h.mock->playSoundCalls[1].loop == true);
    // tension 0: the bed plays at full, the rise stem starts silent (its calm gain is 0).
    REQUIRE_THAT(h.mock->playSoundCalls[0].volume, WithinAbs(1.0f, 1e-3f));
    REQUIRE_THAT(h.mock->playSoundCalls[1].volume, WithinAbs(0.0f, 1e-3f));
}

TEST_CASE("SoundManager - audio:intent ramps stem gains (vertical layering)", "[sound][unit][adaptive]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","bed");  n->setString("path","bed.ogg");  n->setDouble("gainCalm",1.0); n->setDouble("gainPeak",1.0); h.publish("audio:layer", std::move(n)); }
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","rise"); n->setString("path","rise.ogg"); n->setDouble("gainCalm",0.0); n->setDouble("gainPeak",1.0); h.publish("audio:layer", std::move(n)); }
    h.pump(0.016);
    const int riseHandle = h.mock->playSoundCalls[1].handle;

    // Tension up -> the rise stem climbs toward full over a few frames.
    { auto n = std::make_unique<JsonDataNode>("intent"); n->setDouble("tension",1.0); h.publish("audio:intent", std::move(n)); }
    for (int i = 0; i < 60; ++i) h.pump(0.05);
    INFO("rise vol after tension=1: " << lastVolForHandle(*h.mock, riseHandle));
    REQUIRE_THAT(lastVolForHandle(*h.mock, riseHandle), WithinAbs(1.0f, 0.02f));

    // Tension back to calm -> the rise stem fades back out.
    { auto n = std::make_unique<JsonDataNode>("intent"); n->setDouble("tension",0.0); h.publish("audio:intent", std::move(n)); }
    for (int i = 0; i < 60; ++i) h.pump(0.05);
    REQUIRE_THAT(lastVolForHandle(*h.mock, riseHandle), WithinAbs(0.0f, 0.02f));
}

TEST_CASE("SoundManager - the music bus scales adaptive stems", "[sound][unit][adaptive]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","rise"); n->setString("path","r.ogg"); n->setDouble("gainCalm",0.0); n->setDouble("gainPeak",1.0); h.publish("audio:layer", std::move(n)); }
    { auto v = std::make_unique<JsonDataNode>("vol"); v->setString("bus","music"); v->setDouble("value",0.5); h.publish("sound:volume", std::move(v)); }
    { auto n = std::make_unique<JsonDataNode>("intent"); n->setDouble("tension",1.0); h.publish("audio:intent", std::move(n)); }
    h.pump(0.016);
    const int riseHandle = h.mock->playSoundCalls[0].handle;

    for (int i = 0; i < 60; ++i) h.pump(0.05);
    // currentGain -> 1, music bus 0.5, master 1 -> effective 0.5 (adaptive stems are on the music bus).
    REQUIRE_THAT(lastVolForHandle(*h.mock, riseHandle), WithinAbs(0.5f, 0.02f));
}

TEST_CASE("SoundManager - audio:layer:stop stops and drops a stem", "[sound][unit][adaptive]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","rise"); n->setString("path","r.ogg"); h.publish("audio:layer", std::move(n)); }
    h.pump();
    const int handle = h.mock->playSoundCalls[0].handle;

    { auto n = std::make_unique<JsonDataNode>("stop"); n->setString("id","rise"); n->setInt("fadeMs",300); h.publish("audio:layer:stop", std::move(n)); }
    h.pump();

    REQUIRE(h.mock->stopSoundCalls.size() == 1);
    REQUIRE(h.mock->stopSoundCalls[0].handle == handle);
    REQUIRE(h.mock->stopSoundCalls[0].fadeMs == 300);
}

// ============================================================================
// Slice 2 (adaptive audio): beat clock + bar-quantized transitions.
//   - the PURE BeatClock: analytical oracles.
//   - the MODULE: audio:tempo + audio:intent{quantize} applies on the measure (via the mock).
// ============================================================================

TEST_CASE("BeatClock - advances beats and detects beat/bar boundaries", "[sound][unit][adaptive]") {
    BeatClock c;
    c.setTempo(120.0f, 4);              // 2 beats/sec, 4 beats/bar -> 2.0s per bar
    REQUIRE(c.running());

    c.advance(0.5f);                    // beatPos 1.0
    REQUIRE(c.crossedBeat());
    REQUIRE_FALSE(c.crossedBar());

    c.advance(0.5f);                    // 2.0
    c.advance(0.5f);                    // 3.0
    REQUIRE_FALSE(c.crossedBar());

    c.advance(0.5f);                    // 4.0 -> bar boundary
    REQUIRE(c.crossedBar());
}

TEST_CASE("BeatClock - a stopped clock (bpm 0) never advances", "[sound][unit][adaptive]") {
    BeatClock c;                        // default bpm 0
    REQUIRE_FALSE(c.running());
    c.advance(1.0f);
    REQUIRE_FALSE(c.crossedBeat());
    REQUIRE_FALSE(c.crossedBar());
    REQUIRE(c.beatPos() == 0.0);
}

TEST_CASE("SoundManager - audio:intent quantize:bar applies on the NEXT bar", "[sound][unit][adaptive]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","rise"); n->setString("path","r.ogg"); n->setDouble("gainCalm",0.0); n->setDouble("gainPeak",1.0); h.publish("audio:layer", std::move(n)); }
    { auto n = std::make_unique<JsonDataNode>("tempo"); n->setDouble("bpm",120.0); n->setInt("beatsPerBar",4); h.publish("audio:tempo", std::move(n)); }  // 2.0s/bar
    h.pump(0.016);
    const int handle = h.mock->playSoundCalls[0].handle;

    // Stage a quantized intent, then run ~1.9s (< one bar): it must NOT apply yet -> the rise
    // stem's gain is never pushed (still silent).
    { auto n = std::make_unique<JsonDataNode>("intent"); n->setDouble("tension",1.0); n->setString("quantize","bar"); h.publish("audio:intent", std::move(n)); }
    for (int i = 0; i < 38; ++i) h.pump(0.05);   // 1.9s
    REQUIRE(countVolForHandle(*h.mock, handle) == 0);   // staged, not applied -> no gain change

    // Cross the bar (total > 2.0s) and let it ramp -> the change releases and the stem climbs.
    for (int i = 0; i < 40; ++i) h.pump(0.05);   // +2.0s
    REQUIRE_THAT(lastVolForHandle(*h.mock, handle), WithinAbs(1.0f, 0.05f));
}

TEST_CASE("SoundManager - audio:intent quantize:now applies immediately", "[sound][unit][adaptive]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","rise"); n->setString("path","r.ogg"); n->setDouble("gainCalm",0.0); n->setDouble("gainPeak",1.0); h.publish("audio:layer", std::move(n)); }
    { auto n = std::make_unique<JsonDataNode>("tempo"); n->setDouble("bpm",120.0); n->setInt("beatsPerBar",4); h.publish("audio:tempo", std::move(n)); }
    h.pump(0.016);
    const int handle = h.mock->playSoundCalls[0].handle;

    { auto n = std::make_unique<JsonDataNode>("intent"); n->setDouble("tension",1.0); n->setString("quantize","now"); h.publish("audio:intent", std::move(n)); }
    for (int i = 0; i < 40; ++i) h.pump(0.05);   // no bar wait -> rose right away
    REQUIRE_THAT(lastVolForHandle(*h.mock, handle), WithinAbs(1.0f, 0.05f));
}

TEST_CASE("SoundManager - quantize:bar with no tempo set applies immediately", "[sound][unit][adaptive]") {
    Harness h;   // no audio:tempo -> clock stopped -> nothing to wait for
    { auto n = std::make_unique<JsonDataNode>("layer"); n->setString("id","rise"); n->setString("path","r.ogg"); n->setDouble("gainCalm",0.0); n->setDouble("gainPeak",1.0); h.publish("audio:layer", std::move(n)); }
    h.pump(0.016);
    const int handle = h.mock->playSoundCalls[0].handle;

    { auto n = std::make_unique<JsonDataNode>("intent"); n->setDouble("tension",1.0); n->setString("quantize","bar"); h.publish("audio:intent", std::move(n)); }
    for (int i = 0; i < 40; ++i) h.pump(0.05);
    REQUIRE_THAT(lastVolForHandle(*h.mock, handle), WithinAbs(1.0f, 0.05f));
}

// ============================================================================
// Slice 3a (adaptive audio): one-shot cues / stingers (quantizable, music bus).
// ============================================================================

TEST_CASE("SoundManager - audio:cue quantize:now fires immediately on the music bus", "[sound][unit][adaptive]") {
    Harness h;
    { auto v = std::make_unique<JsonDataNode>("vol"); v->setString("bus","music"); v->setDouble("value",0.5); h.publish("sound:volume", std::move(v)); }
    { auto n = std::make_unique<JsonDataNode>("cue"); n->setString("path","sting.ogg"); n->setDouble("volume",1.0); n->setString("quantize","now"); h.publish("audio:cue", std::move(n)); }
    h.pump();

    REQUIRE(h.mock->playSoundCalls.size() == 1);
    REQUIRE(h.mock->playSoundCalls[0].loop == false);                       // a cue is a one-shot
    REQUIRE_THAT(h.mock->playSoundCalls[0].volume, WithinAbs(0.5f, 1e-3f)); // music bus 0.5
}

TEST_CASE("SoundManager - audio:cue quantize:bar fires on the next bar", "[sound][unit][adaptive]") {
    Harness h;
    { auto n = std::make_unique<JsonDataNode>("tempo"); n->setDouble("bpm",120.0); n->setInt("beatsPerBar",4); h.publish("audio:tempo", std::move(n)); }  // 2.0s/bar
    h.pump(0.016);

    { auto n = std::make_unique<JsonDataNode>("cue"); n->setString("path","sting.ogg"); n->setString("quantize","bar"); h.publish("audio:cue", std::move(n)); }
    for (int i = 0; i < 38; ++i) h.pump(0.05);   // 1.9s -> staged, not fired
    REQUIRE(h.mock->playSoundCalls.empty());

    for (int i = 0; i < 8; ++i) h.pump(0.05);    // cross the 2.0s bar
    REQUIRE(h.mock->playSoundCalls.size() == 1);
    REQUIRE(h.mock->playSoundCalls[0].loop == false);
}

TEST_CASE("SoundManager - audio:cue with no tempo fires immediately even if quantized", "[sound][unit][adaptive]") {
    Harness h;   // no tempo -> clock stopped -> nothing to wait for
    { auto n = std::make_unique<JsonDataNode>("cue"); n->setString("path","s.ogg"); n->setString("quantize","bar"); h.publish("audio:cue", std::move(n)); }
    h.pump();
    REQUIRE(h.mock->playSoundCalls.size() == 1);
}
