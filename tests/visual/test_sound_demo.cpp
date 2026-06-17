/**
 * Manual audio demo (sound system, slice 2) — verified by EAR, not by an assertion.
 *
 * Drives the REAL path SoundManagerModule -> SDLMixerBackend -> SDL_mixer on the audio device:
 * generates a short beep WAV, then publishes sound:* topics to play it center / left / right and
 * to exercise the volume bus. There is no headless way to verify audio output; run it and listen.
 *
 *   ./build/tests/test_sound_demo
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
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace grove;

namespace {

void put32(std::ofstream& f, uint32_t v) { f.put(char(v & 0xFF)); f.put(char((v >> 8) & 0xFF)); f.put(char((v >> 16) & 0xFF)); f.put(char((v >> 24) & 0xFF)); }
void put16(std::ofstream& f, uint16_t v) { f.put(char(v & 0xFF)); f.put(char((v >> 8) & 0xFF)); }

// Write a mono 16-bit 44.1kHz sine beep to `path`. Returns false on I/O failure.
bool writeBeepWav(const std::string& path, float freq, int ms) {
    const uint32_t rate = 44100;
    const uint32_t samples = rate * static_cast<uint32_t>(ms) / 1000u;
    const uint32_t dataSize = samples * 2u;  // 16-bit mono

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write("RIFF", 4); put32(f, 36u + dataSize); f.write("WAVE", 4);
    f.write("fmt ", 4); put32(f, 16u); put16(f, 1); put16(f, 1);          // PCM, mono
    put32(f, rate); put32(f, rate * 2u); put16(f, 2); put16(f, 16);       // byteRate, blockAlign, bits
    f.write("data", 4); put32(f, dataSize);

    for (uint32_t i = 0; i < samples; ++i) {
        // Sine with a short fade in/out so it doesn't click.
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        float env = 1.0f;
        const float fade = 0.01f;
        const float dur = static_cast<float>(ms) / 1000.0f;
        if (t < fade) env = t / fade;
        else if (t > dur - fade) env = (dur - t) / fade;
        const float s = std::sin(6.2831853f * freq * t) * 0.35f * env;
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

    const std::string beep = std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "") + "grove_beep.wav";
    if (!writeBeepWav(beep, 440.0f, 250)) {
        std::cerr << "Could not write beep WAV to " << beep << "\n";
        SDL_Quit();
        return 1;
    }
    std::cout << "beep: " << beep << "\n";

    auto& mgr = IntraIOManager::getInstance();
    auto moduleIO = mgr.createInstance("sound_module");
    auto pubIO = mgr.createInstance("sound_pub");

    auto module = std::make_unique<SoundManagerModule>();
    module->setBackend(std::make_unique<sound::SDLMixerBackend>());
    JsonDataNode config("config");
    module->setConfiguration(config, moduleIO.get(), nullptr);

    auto sfx = [&](float pan, const char* label) {
        std::cout << "  play " << label << "\n";
        auto n = std::make_unique<JsonDataNode>("sfx");
        n->setString("path", beep);
        n->setDouble("pan", pan);
        pubIO->publish("sound:sfx", std::move(n));
        JsonDataNode in("input");
        module->process(in);     // module pulls -> SDLMixerBackend plays
        SDL_Delay(500);
    };

    std::cout << "You should hear three beeps: center, left, right.\n";
    sfx(0.0f, "center");
    sfx(-1.0f, "left");
    sfx(1.0f, "right");

    // Lower the SFX bus and play once more — should be noticeably quieter.
    { auto v = std::make_unique<JsonDataNode>("vol"); v->setString("bus", "sfx"); v->setDouble("value", 0.25);
      pubIO->publish("sound:volume", std::move(v)); JsonDataNode in("input"); module->process(in); }
    sfx(0.0f, "center @ 25% sfx bus (quieter)");

    module->shutdown();
    mgr.removeInstance("sound_module");
    mgr.removeInstance("sound_pub");
    SDL_Quit();
    std::cout << "done\n";
    return 0;
}
