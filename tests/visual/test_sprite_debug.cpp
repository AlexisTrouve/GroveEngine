/**
 * Diagnostic Test: Sprite Rendering Debug
 *
 * Ce test isole le problème de rendu des sprites UI en comparant:
 * 1. Sprites hardcodés directement dans SpritePass
 * 2. Sprites envoyés via IIO (comme UIRenderer le fait)
 *
 * Objectif: Identifier EXACTEMENT où le pipeline échoue
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <memory>

#include "BgfxRendererModule.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace grove;

class SpriteDiagnostic {
public:
    bool init(SDL_Window* window) {
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        SDL_GetWindowWMInfo(window, &wmi);

        m_logger = spdlog::stdout_color_mt("SpriteDiag");
        spdlog::set_level(spdlog::level::debug);

        m_logger->info("=== DIAGNOSTIC: Sprite Rendering ===");

        // Create IIO instances
        m_rendererIO = IntraIOManager::getInstance().createInstance("renderer_diag");
        m_testIO = IntraIOManager::getInstance().createInstance("test_publisher");

        // Initialize renderer
        m_renderer = std::make_unique<BgfxRendererModule>();
        JsonDataNode config("config");
        config.setDouble("nativeWindowHandle",
            static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        config.setInt("windowWidth", 800);
        config.setInt("windowHeight", 600);
        config.setString("backend", "d3d11");
        config.setBool("vsync", true);
        m_renderer->setConfiguration(config, m_rendererIO.get(), nullptr);

        m_logger->info("Renderer initialized");
        return true;
    }

    void runTest() {
        m_logger->info("\n========================================");
        m_logger->info("TEST: Publishing sprites via IIO");
        m_logger->info("========================================\n");

        // Publish camera first (like UIModule does)
        {
            auto cam = std::make_unique<JsonDataNode>("camera");
            cam->setDouble("x", 0.0);
            cam->setDouble("y", 0.0);
            cam->setDouble("zoom", 1.0);
            cam->setInt("viewportX", 0);
            cam->setInt("viewportY", 0);
            cam->setInt("viewportW", 800);
            cam->setInt("viewportH", 600);
            m_testIO->publish("render:camera", std::move(cam));
            m_logger->info("[PUB] Camera: viewport 800x600");
        }

        // TEST 1: Sprite at known position (should be visible)
        {
            auto sprite = std::make_unique<JsonDataNode>("sprite");
            // Position at CENTER of screen (400, 300)
            sprite->setDouble("x", 400.0);  // Center X
            sprite->setDouble("y", 300.0);  // Center Y
            sprite->setDouble("scaleX", 200.0);  // 200px wide
            sprite->setDouble("scaleY", 150.0);  // 150px tall
            sprite->setInt("color", 0xFF0000FF);  // Red, full alpha
            sprite->setInt("textureId", 0);
            sprite->setInt("layer", 100);

            m_logger->info("[PUB] Sprite 1: pos=({},{}) scale=({},{}) color=0x{:08X}",
                400.0, 300.0, 200.0, 150.0, 0xFF0000FF);

            m_testIO->publish("render:sprite", std::move(sprite));
        }

        // TEST 2: Sprite at top-left corner
        {
            auto sprite = std::make_unique<JsonDataNode>("sprite");
            sprite->setDouble("x", 100.0);
            sprite->setDouble("y", 100.0);
            sprite->setDouble("scaleX", 100.0);
            sprite->setDouble("scaleY", 100.0);
            sprite->setInt("color", 0x00FF00FF);  // Green
            sprite->setInt("textureId", 0);
            sprite->setInt("layer", 101);

            m_logger->info("[PUB] Sprite 2: pos=({},{}) scale=({},{}) color=0x{:08X}",
                100.0, 100.0, 100.0, 100.0, 0x00FF00FF);

            m_testIO->publish("render:sprite", std::move(sprite));
        }

        // TEST 3: Sprite simulating UIButton (like UIRenderer::drawRect)
        {
            float btnX = 50.0f, btnY = 400.0f;
            float btnW = 200.0f, btnH = 50.0f;

            auto sprite = std::make_unique<JsonDataNode>("sprite");
            // UIRenderer centers the sprite
            sprite->setDouble("x", static_cast<double>(btnX + btnW * 0.5f));
            sprite->setDouble("y", static_cast<double>(btnY + btnH * 0.5f));
            sprite->setDouble("scaleX", static_cast<double>(btnW));
            sprite->setDouble("scaleY", static_cast<double>(btnH));
            sprite->setInt("color", 0x0984E3FF);  // Blue (like primary button)
            sprite->setInt("textureId", 0);
            sprite->setInt("layer", 1000);  // UI layer

            m_logger->info("[PUB] Button Sprite: pos=({},{}) scale=({},{}) color=0x{:08X}",
                btnX + btnW * 0.5f, btnY + btnH * 0.5f, btnW, btnH, 0x0984E3FF);

            m_testIO->publish("render:sprite", std::move(sprite));
        }

        // Now render
        m_logger->info("\n[RENDER] Processing frame...");

        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        input.setInt("frameCount", m_frameCount++);

        m_renderer->process(input);

        m_logger->info("[RENDER] Frame complete\n");
    }

    void shutdown() {
        m_renderer->shutdown();
        IntraIOManager::getInstance().removeInstance("renderer_diag");
        IntraIOManager::getInstance().removeInstance("test_publisher");
        m_logger->info("Diagnostic test complete");
    }

    int getFrameCount() const { return m_frameCount; }

private:
    std::shared_ptr<spdlog::logger> m_logger;
    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::shared_ptr<IntraIO> m_rendererIO;
    std::shared_ptr<IntraIO> m_testIO;
    int m_frameCount = 0;
};

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Sprite Rendering Diagnostic",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SpriteDiagnostic diag;
    if (!diag.init(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "\n=== SPRITE DIAGNOSTIC TEST ===" << std::endl;
    std::cout << "Expected: 3 colored rectangles" << std::endl;
    std::cout << "  - RED (200x150) at center of screen" << std::endl;
    std::cout << "  - GREEN (100x100) at top-left" << std::endl;
    std::cout << "  - BLUE (200x50) at bottom-left (simulating UI button)" << std::endl;
    std::cout << "\nPress ESC to exit\n" << std::endl;

    bool running = true;
    int frameCount = 0;
    const int maxFrames = 300;  // Run for ~5 seconds

    while (running && frameCount < maxFrames) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        diag.runTest();
        frameCount++;

        SDL_Delay(16);
    }

    std::cout << "\nRendered " << frameCount << " frames" << std::endl;
    std::cout << "If you saw the colored rectangles: IIO routing works!" << std::endl;
    std::cout << "If blank/red background only: Problem in SceneCollector or SpritePass" << std::endl;

    diag.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
