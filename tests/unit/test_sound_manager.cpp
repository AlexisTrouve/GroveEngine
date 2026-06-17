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
#include "../mocks/MockSoundBackend.h"

#include "grove/IntraIOManager.h"
#include "grove/IntraIO.h"
#include "grove/JsonDataNode.h"

#include <chrono>
#include <memory>
#include <sstream>

using namespace grove;
using namespace grove::sound;
using Catch::Matchers::WithinAbs;

static std::string uid(const std::string& p) {
    auto n = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream o; o << p << "_" << n; return o.str();
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
