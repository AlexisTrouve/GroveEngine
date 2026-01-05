/**
 * Minimal test using BgfxRenderer_static - no DLLs loaded
 * Tests if BgfxRendererModule works in isolation
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <cmath>
#include <memory>

#include "BgfxRendererModule.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace grove;

int main(int argc, char* argv[]) {
    // Setup logging
    auto logger = spdlog::stdout_color_mt("Main");
    spdlog::set_level(spdlog::level::info);

    logger->info("=== BgfxRenderer Static Only Test ===");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        logger->error("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "BgfxRenderer Static Test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        logger->error("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Get native window handle
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    SDL_GetWindowWMInfo(window, &wmi);

    logger->info("Window created, handle: {}", (void*)wmi.info.win.window);

    // Create IO instances from singleton manager
    // NOTE: Must use singleton because IntraIO::publish/subscribe use IntraIOManager::getInstance()
    // IMPORTANT: Need SEPARATE instances for publisher and subscriber because messages
    // are not delivered back to the sender (see IntraIOManager::routeMessage)
    auto rendererIOPtr = IntraIOManager::getInstance().createInstance("renderer");
    IIO* rendererIO = rendererIOPtr.get();

    // Separate "game" instance for publishing render commands
    auto gameIOPtr = IntraIOManager::getInstance().createInstance("game");
    IIO* gameIO = gameIOPtr.get();

    // Create and configure renderer
    auto renderer = std::make_unique<BgfxRendererModule>();

    JsonDataNode config("config");
    config.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
    config.setInt("windowWidth", 800);
    config.setInt("windowHeight", 600);
    config.setString("backend", "d3d11");
    config.setString("shaderPath", "./shaders");
    config.setBool("vsync", true);

    logger->info("Configuring BgfxRendererModule...");
    renderer->setConfiguration(config, rendererIO, nullptr);

    logger->info("BgfxRendererModule configured");

    // Main loop
    logger->info("Starting main loop...");
    bool running = true;
    int frameCount = 0;
    Uint64 start = SDL_GetPerformanceCounter();

    // Setup camera for orthographic projection (required for pixel-space rendering)
    {
        auto cam = std::make_unique<JsonDataNode>("camera");
        cam->setDouble("x", 0.0);
        cam->setDouble("y", 0.0);
        cam->setDouble("zoom", 1.0);
        cam->setInt("viewportX", 0);
        cam->setInt("viewportY", 0);
        cam->setInt("viewportW", 800);
        cam->setInt("viewportH", 600);
        gameIO->publish("render:camera", std::move(cam));
    }

    while (running && frameCount < 300) {  // Run for 300 frames (~5 sec)
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        // Send render commands via IIO before processing
        // Draw debug rectangles (colored boxes without texture)
        float t = frameCount * 0.02f;

        // Moving red rectangle
        {
            auto rect = std::make_unique<JsonDataNode>("rect");
            rect->setDouble("x", 100 + std::sin(t) * 50);
            rect->setDouble("y", 100);
            rect->setDouble("width", 100);
            rect->setDouble("height", 100);
            rect->setInt("color", 0xFF0000FF);  // Red, alpha=255
            rect->setBool("filled", true);
            gameIO->publish("render:debug:rect", std::move(rect));
        }

        // Moving green rectangle
        {
            auto rect = std::make_unique<JsonDataNode>("rect");
            rect->setDouble("x", 300);
            rect->setDouble("y", 200 + std::cos(t) * 50);
            rect->setDouble("width", 80);
            rect->setDouble("height", 80);
            rect->setInt("color", 0x00FF00FF);  // Green
            rect->setBool("filled", true);
            gameIO->publish("render:debug:rect", std::move(rect));
        }

        // Rotating blue rectangle
        {
            auto rect = std::make_unique<JsonDataNode>("rect");
            rect->setDouble("x", 500);
            rect->setDouble("y", 300);
            rect->setDouble("width", 120);
            rect->setDouble("height", 60);
            rect->setInt("color", 0x0088FFFF);  // Blue
            rect->setBool("filled", true);
            gameIO->publish("render:debug:rect", std::move(rect));
        }

        // Draw debug lines
        {
            auto line = std::make_unique<JsonDataNode>("line");
            line->setDouble("x1", 50);
            line->setDouble("y1", 50);
            line->setDouble("x2", 750);
            line->setDouble("y2", 550);
            line->setInt("color", 0xFFFF00FF);  // Yellow
            gameIO->publish("render:debug:line", std::move(line));
        }

        // Process renderer
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        input.setInt("frameCount", frameCount);

        renderer->process(input);

        frameCount++;

        // Log every 60 frames
        if (frameCount % 60 == 0) {
            logger->info("Frame {}", frameCount);
        }
    }

    Uint64 end = SDL_GetPerformanceCounter();
    double elapsed = (end - start) / (double)SDL_GetPerformanceFrequency();

    logger->info("Rendered {} frames in {:.2f}s ({:.1f} FPS)", frameCount, elapsed, frameCount / elapsed);

    // Cleanup
    logger->info("Shutting down...");
    renderer->shutdown();
    IntraIOManager::getInstance().removeInstance("renderer");
    IntraIOManager::getInstance().removeInstance("game");
    SDL_DestroyWindow(window);
    SDL_Quit();

    logger->info("=== Test complete ===");
    return 0;
}
