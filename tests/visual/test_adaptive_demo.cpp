/**
 * Manual ADAPTIVE-audio demo (sound system, slices 1-3) — verified by EAR, not by an assertion.
 *
 * Drives the REAL path SoundManagerModule -> SDLMixerBackend -> SDL_mixer with SYNTHESIZED
 * placeholder stems (generated in-process, no asset files), so you can HEAR the adaptive engine:
 *   Phase 1 — a calm bed only (tension 0).
 *   Phase 2 — tension ramps 0->1: a second layer fades IN (vertical layering, slice 1).
 *   Phase 3 — a sting is requested mid-bar but LANDS ON THE NEXT BAR (quantized cue, slices 2-3a).
 *   Phase 4 — a leitmotif crossfades soft -> broken by "state" (slice 3b).
 * There is no headless way to verify audio output; run it and listen.
 *
 *   ./build/tests/test_adaptive_demo
 */

#define SDL_MAIN_HANDLED   // plain console main(), no SDL2main/WinMain trampoline
#include <SDL.h>

#include "SoundManager/SoundManagerModule.h"
#include "SoundManager/SDLMixerBackend.h"

#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

using namespace grove;

namespace {

const double TAU = 6.283185307179586;
const double PI  = 3.141592653589793;

void put32(std::ofstream& f, uint32_t v) { f.put(char(v & 0xFF)); f.put(char((v >> 8) & 0xFF)); f.put(char((v >> 16) & 0xFF)); f.put(char((v >> 24) & 0xFF)); }
void put16(std::ofstream& f, uint16_t v) { f.put(char(v & 0xFF)); f.put(char((v >> 8) & 0xFF)); }

inline float sine(double f, double t) { return static_cast<float>(std::sin(TAU * f * t)); }

// Write a mono 16-bit 44.1kHz WAV of `seconds`, sampling fn(t) in [-1,1]. For a looping stem, fn
// must be periodic over `seconds` (or envelope to 0 at both ends) so the loop is seamless.
bool writeWav(const std::string& path, double seconds, const std::function<float(double)>& fn) {
    const uint32_t rate = 44100;
    const uint32_t samples = static_cast<uint32_t>(rate * seconds);
    const uint32_t dataSize = samples * 2u;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write("RIFF", 4); put32(f, 36u + dataSize); f.write("WAVE", 4);
    f.write("fmt ", 4); put32(f, 16u); put16(f, 1); put16(f, 1);          // PCM, mono
    put32(f, rate); put32(f, rate * 2u); put16(f, 2); put16(f, 16);
    f.write("data", 4); put32(f, dataSize);

    for (uint32_t i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / rate;
        float s = fn(t);
        if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
        put16(f, static_cast<uint16_t>(static_cast<int16_t>(s * 32767.0f)));
    }
    return true;
}

} // namespace

int main() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init(AUDIO) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    const std::string dir = std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "");

    // --- synthesize placeholder stems (2.0s SEAMLESS loops; the sting is a one-shot) ---
    const std::string bed    = dir + "grove_bed.wav";
    const std::string tens   = dir + "grove_tension.wav";
    const std::string sting  = dir + "grove_sting.wav";
    const std::string soft   = dir + "grove_soft.wav";
    const std::string broken = dir + "grove_broken.wav";

    // Bed: a low drone (integer-cycle freqs over 2s -> seamless loop, no fade needed).
    writeWav(bed, 2.0, [](double t) { return 0.22f * (sine(110, t) + 0.7f * sine(165, t)); });
    // Tension: a detuned beating pair + a 6 Hz tremolo (all integer cycles in 2s -> seamless).
    writeWav(tens, 2.0, [](double t) {
        const float trem = 0.6f + 0.4f * static_cast<float>(std::sin(TAU * 6.0 * t));
        return 0.20f * trem * (sine(440, t) + sine(446, t));
    });
    // Sting: a bright chord with a fast decay (a one-shot, so a decay envelope is fine).
    writeWav(sting, 0.8, [](double t) {
        const float e = static_cast<float>(std::exp(-6.0 * t));
        return 0.5f * e * (sine(660, t) + sine(880, t) + sine(990, t));
    });
    // Leitmotif: a 4-note arpeggio, each note enveloped to 0 at its edges -> seamless loop + no clicks.
    auto arp = [](double t, double n0, double n1, double n2, double n3) {
        const double notes[4] = {n0, n1, n2, n3};
        const int k = static_cast<int>(t / 0.5) % 4;
        const double lt = t - 0.5 * std::floor(t / 0.5);   // time within the note
        const double env = std::sin(PI * lt / 0.5);        // 0 -> 1 -> 0 across the note
        return static_cast<float>(0.28 * env * std::sin(TAU * notes[k] * lt));
    };
    writeWav(soft,   2.0, [&](double t) { return arp(t, 330, 415, 494, 659); });  // bright, major-ish
    writeWav(broken, 2.0, [&](double t) { return arp(t, 311, 370, 440, 466); });  // darker, detuned

    // --- wire the module to the real backend ---
    auto& mgr = IntraIOManager::getInstance();
    auto mIO = mgr.createInstance("adaptive_mod");
    auto pub = mgr.createInstance("adaptive_pub");
    auto module = std::make_unique<SoundManagerModule>();
    module->setBackend(std::make_unique<sound::SDLMixerBackend>());
    JsonDataNode cfg("config");
    module->setConfiguration(cfg, mIO.get(), nullptr);

    auto send = [&](const std::string& topic, const std::function<void(JsonDataNode&)>& fill) {
        auto n = std::make_unique<JsonDataNode>("m"); fill(*n); pub->publish(topic, std::move(n));
    };
    // Run the module ~60fps in real time for `sec` seconds (ramps stem gains, ticks the beat clock).
    auto pump = [&](double sec) {
        const int frames = static_cast<int>(sec / 0.016);
        for (int i = 0; i < frames; ++i) {
            JsonDataNode in("input"); in.setDouble("deltaTime", 0.016);
            module->process(in);
            SDL_Delay(16);
        }
    };

    std::cout << "=== GroveEngine adaptive-audio demo (listen) ===\n";

    // Phase 1 — bed + a (silent at tension 0) tension layer.
    send("audio:layer", [&](JsonDataNode& n) { n.setString("id", "bed");     n.setString("path", bed);  n.setDouble("gainCalm", 1.0); n.setDouble("gainPeak", 1.0); });
    send("audio:layer", [&](JsonDataNode& n) { n.setString("id", "tension"); n.setString("path", tens); n.setDouble("gainCalm", 0.0); n.setDouble("gainPeak", 1.0); });
    std::cout << "Phase 1: calm bed only (tension 0)\n";
    pump(3.0);

    // Phase 2 — ramp tension 0 -> 1; the tension layer fades in (vertical layering).
    std::cout << "Phase 2: tension rising 0 -> 1 (the tension layer fades in)\n";
    for (int s = 0; s <= 10; ++s) {
        const double tn = s / 10.0;
        send("audio:intent", [&](JsonDataNode& n) { n.setDouble("tension", tn); });
        pump(0.5);
    }
    pump(1.0);

    // Phase 3 — request a sting MID-bar; it must land on the next bar.
    std::cout << "Phase 3: bar-quantized sting (120 BPM 4/4 -> lands on the next bar)\n";
    send("audio:tempo", [&](JsonDataNode& n) { n.setDouble("bpm", 120.0); n.setInt("beatsPerBar", 4); });
    send("audio:cue",   [&](JsonDataNode& n) { n.setString("path", sting); n.setString("quantize", "bar"); });
    pump(3.0);

    // Phase 4 — leitmotif: swap the bed/tension for a themed motif, crossfade soft -> broken.
    std::cout << "Phase 4: leitmotif crossfade soft -> broken (arrangement follows state)\n";
    send("audio:layer:stop", [&](JsonDataNode& n) { n.setString("id", "tension"); });
    send("audio:layer:stop", [&](JsonDataNode& n) { n.setString("id", "bed"); });
    send("audio:layer", [&](JsonDataNode& n) { n.setString("id", "ldr_soft");   n.setString("path", soft);   n.setString("theme", "leader"); n.setString("state", "soft");   n.setDouble("gainPeak", 1.0); });
    send("audio:layer", [&](JsonDataNode& n) { n.setString("id", "ldr_broken"); n.setString("path", broken); n.setString("theme", "leader"); n.setString("state", "broken"); n.setDouble("gainPeak", 1.0); });
    send("audio:theme", [&](JsonDataNode& n) { n.setString("id", "leader"); n.setString("state", "soft"); });
    std::cout << "  ... soft arrangement\n";
    pump(3.0);
    send("audio:theme", [&](JsonDataNode& n) { n.setString("id", "leader"); n.setString("state", "broken"); });
    std::cout << "  ... broken arrangement\n";
    pump(3.0);

    module->shutdown();
    mgr.removeInstance("adaptive_mod");
    mgr.removeInstance("adaptive_pub");
    SDL_Quit();
    std::cout << "done\n";
    return 0;
}
