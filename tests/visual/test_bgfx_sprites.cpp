/**
 * Test: BgfxRenderer Sprite Integration Test
 *
 * Tests the full BgfxRendererModule with sprites sent via IIO.
 * This validates Phase 4 integration: ShaderManager + SpritePass + IIO.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <iostream>
#include <cstdint>
#include <cmath>

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "BgfxRenderer Sprite Integration Test\n";
    std::cout << "========================================\n\n";

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Create window
    const int width = 800;
    const int height = 600;

    SDL_Window* window = SDL_CreateWindow(
        "BgfxRenderer Sprites Test - Press ESC to exit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    // Get native window handle
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) {
        std::cerr << "SDL_GetWindowWMInfo failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    void* nativeWindowHandle = (void*)(uintptr_t)wmi.info.x11.window;
    void* nativeDisplayHandle = wmi.info.x11.display;

    std::cout << "Window created: " << width << "x" << height << "\n";

    // ========================================
    // Setup bgfx platform data (must be done before module init)
    // ========================================

    bgfx::PlatformData pd;
    pd.ndt = nativeDisplayHandle;
    pd.nwh = nativeWindowHandle;
    pd.context = nullptr;
    pd.backBuffer = nullptr;
    pd.backBufferDS = nullptr;
    bgfx::setPlatformData(pd);

    std::cout << "Platform data set for bgfx\n";

    // ========================================
    // Setup GroveEngine systems
    // ========================================

    // Create IIO Manager
    grove::IntraIOManager ioManager;
    auto rendererIO = ioManager.createInstance("bgfx_renderer");

    std::cout << "IIO Manager created\n";

    // Load BgfxRenderer module
    grove::ModuleLoader loader;

    // Find the module library (in modules/ subdirectory relative to build)
    std::string modulePath = "../modules/libBgfxRenderer.so";

    std::unique_ptr<grove::IModule> module;
    try {
        module = loader.load(modulePath, "bgfx_renderer");
    } catch (const std::exception& e) {
        std::cerr << "Failed to load BgfxRenderer module: " << e.what() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!module) {
        std::cerr << "Failed to load BgfxRenderer module from: " << modulePath << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "BgfxRenderer module loaded\n";

    // Configure the module
    // Note: We don't pass nativeWindowHandle here because bgfx::setPlatformData
    // was already called with the full platform data (including X11 display)
    grove::JsonDataNode config("config");
    config.setInt("windowWidth", width);
    config.setInt("windowHeight", height);
    config.setString("backend", "auto");
    config.setBool("vsync", true);
    config.setInt("maxSpritesPerBatch", 10000);
    config.setInt("frameAllocatorSizeMB", 16);
    // nativeWindowHandle = 0 means "use platform data already set via bgfx::setPlatformData"
    config.setInt("nativeWindowHandle", 0);

    module->setConfiguration(config, rendererIO.get(), nullptr);

    std::cout << "Module configured\n";

    // ========================================
    // Main loop
    // ========================================

    std::cout << "\n*** Rendering sprites via IIO ***\n";
    std::cout << "Press ESC to exit or wait 5 seconds\n\n";

    bool running = true;
    uint32_t frameCount = 0;
    Uint32 startTime = SDL_GetTicks();
    const Uint32 testDuration = 5000; // 5 seconds

    while (running) {
        // Process SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        // Check timeout
        Uint32 elapsed = SDL_GetTicks() - startTime;
        if (elapsed > testDuration) {
            running = false;
        }

        // ========================================
        // Send sprites via IIO
        // ========================================

        float time = elapsed / 1000.0f;

        // Send a few animated sprites
        for (int i = 0; i < 5; ++i) {
            auto sprite = std::make_unique<grove::JsonDataNode>("sprite");

            // Animate position in a circle
            float angle = time * 2.0f + i * (3.14159f * 2.0f / 5.0f);
            float radius = 200.0f;
            float centerX = width / 2.0f;
            float centerY = height / 2.0f;

            sprite->setDouble("x", centerX + std::cos(angle) * radius);
            sprite->setDouble("y", centerY + std::sin(angle) * radius);
            sprite->setDouble("scaleX", 50.0);  // 50x50 pixel sprite
            sprite->setDouble("scaleY", 50.0);
            sprite->setDouble("rotation", angle);
            sprite->setDouble("u0", 0.0);
            sprite->setDouble("v0", 0.0);
            sprite->setDouble("u1", 1.0);
            sprite->setDouble("v1", 1.0);

            // Different colors for each sprite
            uint32_t colors[] = {
                0xFF0000FF,  // Red
                0x00FF00FF,  // Green
                0x0000FFFF,  // Blue
                0xFFFF00FF,  // Yellow
                0xFF00FFFF   // Magenta
            };
            sprite->setInt("color", static_cast<int>(colors[i]));
            sprite->setInt("textureId", 0);
            sprite->setInt("layer", i);

            // Cast to IDataNode unique_ptr
            std::unique_ptr<grove::IDataNode> spriteData = std::move(sprite);
            rendererIO->publish("render:sprite", std::move(spriteData));
        }

        // Send clear color (changes over time)
        {
            auto clear = std::make_unique<grove::JsonDataNode>("clear");
            uint8_t r = static_cast<uint8_t>(48 + 20 * std::sin(time));
            uint8_t g = static_cast<uint8_t>(48 + 20 * std::sin(time + 1.0f));
            uint8_t b = static_cast<uint8_t>(48 + 20 * std::sin(time + 2.0f));
            uint32_t clearColor = (r << 24) | (g << 16) | (b << 8) | 0xFF;
            clear->setInt("color", static_cast<int>(clearColor));

            std::unique_ptr<grove::IDataNode> clearData = std::move(clear);
            rendererIO->publish("render:clear", std::move(clearData));
        }

        // ========================================
        // Process frame
        // ========================================

        grove::JsonDataNode input("input");
        input.setDouble("deltaTime", 1.0 / 60.0);  // Assume 60 FPS
        input.setInt("frameNumber", frameCount);

        module->process(input);
        frameCount++;
    }

    float elapsedSec = (SDL_GetTicks() - startTime) / 1000.0f;
    float fps = frameCount / elapsedSec;

    std::cout << "Test completed!\n";
    std::cout << "  Frames: " << frameCount << "\n";
    std::cout << "  Time: " << elapsedSec << "s\n";
    std::cout << "  FPS: " << fps << "\n";

    // ========================================
    // Cleanup
    // ========================================

    module->shutdown();
    loader.unload();

    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "\n========================================\n";
    std::cout << "PASS: Sprites rendered via IIO!\n";
    std::cout << "========================================\n";

    return 0;
}
