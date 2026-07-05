/**
 * Video demo — play a REAL MP4 on screen WITH SYNCED AUDIO (video slice 6c, by-eye + by-ear).
 *
 * Wires the whole stack in one window: BgfxRenderer (D3D11) + SoundManager (SDL_mixer) + VideoModule
 * (FfmpegCliBackend). It plays an .mp4 — the picture on a full-window sprite, the audio through the
 * sound system as the A/V master clock. This is the compositional proof the headless tests can't give
 * (a real decoder + a real GPU + real audio, synced) — the equivalent of the SDL sound demo's by-ear check.
 *
 * RUN (from build/, so the relative paths + temp clip work):
 *   ./tests/test_video_demo                 -> generates a 5 s testsrc+sine clip and plays it
 *   ./tests/test_video_demo path/to/clip.mp4 -> plays your own file
 *   ESC / close the window to quit.  Needs ffmpeg on PATH. NOT a ctest (windowed, by-eye+ear).
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include "BgfxRendererModule.h"
#include "SoundManager/SoundManagerModule.h"
#include "SoundManager/SDLMixerBackend.h"
#include "VideoModule.h"
#include "FfmpegCliBackend.h"

#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace grove;

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::warn);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SDL_SetMainReady();

    // --- The clip: argv[1], or generate a 5 s test pattern with a 440 Hz tone. ---
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string clip;
    bool generated = false;
    if (argc > 1) {
        clip = argv[1];
    } else {
        clip = (fs::temp_directory_path(ec) / "grove_video_demo.mp4").string();
        const std::string gen =
            "ffmpeg -y -v error -f lavfi -i testsrc=duration=5:size=640x480:rate=30 "
            "-f lavfi -i sine=frequency=440:duration=5 -pix_fmt yuv420p -c:v libx264 -c:a aac \"" + clip + "\"";
        std::printf("Generating test clip (testsrc + 440 Hz sine)...\n");
        if (std::system(gen.c_str()) != 0 || !fs::exists(clip)) {
            std::printf("FATAL: ffmpeg not available / generation failed.\n");
            return 1;
        }
        generated = true;
    }

    const int W = 800, H = 600;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { std::printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("GroveEngine — video demo (ESC to quit)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_SHOWN);
    if (!win) { std::printf("SDL_CreateWindow: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version); SDL_GetWindowWMInfo(win, &wmi);

    auto& mgr = IntraIOManager::getInstance();
    auto rIO = mgr.createInstance("demo_render");
    auto sIO = mgr.createInstance("demo_sound");
    auto vIO = mgr.createInstance("demo_video");
    auto gIO = mgr.createInstance("demo_game");

    // --- Renderer (D3D11). ---
    auto renderer = std::make_unique<BgfxRendererModule>();
    { JsonDataNode c("config");
      c.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
      c.setInt("windowWidth", W); c.setInt("windowHeight", H); c.setString("backend", "d3d11"); c.setBool("vsync", true);
      renderer->setConfiguration(c, rIO.get(), nullptr); }
    if (!renderer->getDevice()) { std::printf("FATAL: no GPU device.\n"); return 1; }

    // --- Sound (SDL_mixer): plays the extracted audio + drives sound:music:position (the A/V clock). ---
    auto sound = std::make_unique<SoundManagerModule>();
    sound->setBackend(std::make_unique<sound::SDLMixerBackend>());
    { JsonDataNode c("config"); sound->setConfiguration(c, sIO.get(), nullptr); }

    // --- Video (ffmpeg CLI): decodes the MP4, uploads frames, syncs to the audio clock. ---
    auto video = std::make_unique<VideoModule>();
    video->setBackend(std::make_unique<video::FfmpegCliBackend>());
    { JsonDataNode c("config"); video->setConfiguration(c, vIO.get(), nullptr); }

    bool ended = false;
    gIO->subscribe("video:ended", [&](const Message&) { ended = true; });

    // Play: full-window sprite.
    { auto p = std::make_unique<JsonDataNode>("play");
      p->setString("path", clip);
      p->setDouble("x", 0); p->setDouble("y", 0); p->setDouble("w", W); p->setDouble("h", H); p->setInt("layer", 10);
      gIO->publish("video:play", std::move(p)); }

    std::printf("Playing: %s   (ESC to quit)\n", clip.c_str());

    Uint64 prev = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    bool quit = false;
    while (!quit && !ended) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) quit = true;
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - prev) / freq; prev = now;
        if (dt > 0.1) dt = 0.1;   // clamp a hitch so the clock doesn't leap

        // Clear + camera (world view at 1:1, origin 0 -> sprite pixel coords).
        { auto c = std::make_unique<JsonDataNode>("clear"); c->setInt("color", 0x101018FF); gIO->publish("render:clear", std::move(c)); }
        { auto cam = std::make_unique<JsonDataNode>("cam");
          cam->setDouble("x",0); cam->setDouble("y",0); cam->setDouble("zoom",1.0);
          cam->setInt("viewportX",0); cam->setInt("viewportY",0); cam->setInt("viewportW",W); cam->setInt("viewportH",H);
          gIO->publish("render:camera", std::move(cam)); }

        // Process order: video (advance + upload) -> sound (play + publish position) -> renderer (draw).
        { JsonDataNode in("input"); in.setDouble("deltaTime", dt); video->process(in); }
        { JsonDataNode in("input"); in.setDouble("deltaTime", dt); sound->process(in); }
        { JsonDataNode in("input"); in.setDouble("deltaTime", dt); renderer->process(in); }
        while (gIO->hasMessages() > 0) gIO->pullAndDispatch();
    }

    std::printf(ended ? "Clip ended.\n" : "Quit.\n");
    video->shutdown(); sound->shutdown(); renderer->shutdown();
    // The IIO instances (shared_ptrs above) auto-remove on scope exit — no explicit removeInstance.
    SDL_DestroyWindow(win); SDL_Quit();
    if (generated) fs::remove(clip, ec);
    return 0;
}
