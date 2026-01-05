/**
 * Test using BgfxDevice directly via IRHIDevice::create()
 * No ShaderManager, no RenderGraph - just init + frame
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <memory>

#include "RHI/RHIDevice.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace grove::rhi;

int main(int argc, char* argv[]) {
    auto logger = spdlog::stdout_color_mt("Main");
    spdlog::set_level(spdlog::level::info);

    logger->info("=== BgfxDevice Direct Test ===");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        logger->error("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "BgfxDevice Direct Test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        logger->error("SDL_CreateWindow failed");
        SDL_Quit();
        return 1;
    }

    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    SDL_GetWindowWMInfo(window, &wmi);

    logger->info("Window handle: {}", (void*)wmi.info.win.window);

    // Create BgfxDevice via factory
    auto device = IRHIDevice::create();

    logger->info("Initializing BgfxDevice...");
    if (!device->init(wmi.info.win.window, nullptr, 800, 600)) {
        logger->error("BgfxDevice init failed");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    logger->info("BgfxDevice initialized, starting loop...");

    bool running = true;
    int frameCount = 0;

    while (running && frameCount < 60) {
        logger->info("Frame {} start", frameCount);
        spdlog::default_logger()->flush();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        // Just frame() - exactly like test_bgfx_minimal_win
        device->frame();

        logger->info("Frame {} complete", frameCount);
        frameCount++;
    }

    logger->info("Rendered {} frames", frameCount);

    device->shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    logger->info("=== Test complete ===");
    return 0;
}
