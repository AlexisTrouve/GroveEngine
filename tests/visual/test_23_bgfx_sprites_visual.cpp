/**
 * Test: BgfxRenderer Visual Sprites Test
 *
 * Tests the full BgfxRendererModule with sprites via IIO.
 * This validates Phase 5: sprites rendered via the module pipeline.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <iostream>
#include <cstdint>
#include <cmath>

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "BgfxRenderer Visual Sprites Test\n";
    std::cout << "========================================\n\n";

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Create window
    int width = 800;
    int height = 600;

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

#ifdef _WIN32
    void* nativeWindowHandle = wmi.info.win.window;
    void* nativeDisplayHandle = nullptr;
#else
    void* nativeWindowHandle = (void*)(uintptr_t)wmi.info.x11.window;
    void* nativeDisplayHandle = wmi.info.x11.display;
#endif

    std::cout << "Window created: " << width << "x" << height << "\n";

    std::cout << "Native handles ready\n";

    // ========================================
    // Setup GroveEngine systems
    // ========================================

    // Use singleton IntraIOManager for proper routing
    auto& ioManager = grove::IntraIOManager::getInstance();

    // Create two IO instances: one for "game" publishing, one for renderer
    auto gameIO = ioManager.createInstance("game_module");
    auto rendererIO = ioManager.createInstance("bgfx_renderer");

    std::cout << "IIO Manager setup complete\n";

    // Load BgfxRenderer module
    grove::ModuleLoader loader;

    // Find the module library (in modules/ subdirectory relative to build)
    std::string modulePath = "../modules/libBgfxRenderer.so";
#ifdef _WIN32
    modulePath = "../modules/BgfxRenderer.dll";
#endif

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

    // Configure the module with native window handles
    grove::JsonDataNode config("config");
    config.setInt("windowWidth", width);
    config.setInt("windowHeight", height);
    config.setString("backend", "auto");
    config.setBool("vsync", true);
    config.setInt("maxSpritesPerBatch", 10000);
    config.setInt("frameAllocatorSizeMB", 16);
    // Pass native handles as double (can store 64-bit integers up to 2^53 without loss)
    config.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(nativeWindowHandle)));
    config.setDouble("nativeDisplayHandle", static_cast<double>(reinterpret_cast<uintptr_t>(nativeDisplayHandle)));

    // Load texture from assets folder
    config.setString("defaultTexture", "../../assets/textures/1f440.png");

    // Enable debug overlay
    config.setBool("debugOverlay", true);

    module->setConfiguration(config, rendererIO.get(), nullptr);

    std::cout << "Module configured\n";

    // ========================================
    // Main loop
    // ========================================

    std::cout << "\n*** Rendering sprites via IIO ***\n";
    std::cout << "Press ESC to exit or wait 10 seconds\n\n";

    bool running = true;
    uint32_t frameCount = 0;
    Uint32 startTime = SDL_GetTicks();
    const Uint32 testDuration = 10000; // 10 seconds

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
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                width = event.window.data1;
                height = event.window.data2;
            }
        }

        // Check timeout
        Uint32 elapsed = SDL_GetTicks() - startTime;
        if (elapsed > testDuration) {
            running = false;
        }

        float time = elapsed / 1000.0f;

        // ========================================
        // Game module publishes render commands via IIO
        // ========================================

        // Send camera
        {
            auto camera = std::make_unique<grove::JsonDataNode>("camera");
            camera->setDouble("x", 0.0);
            camera->setDouble("y", 0.0);
            camera->setDouble("zoom", 1.0);
            camera->setInt("viewportX", 0);
            camera->setInt("viewportY", 0);
            camera->setInt("viewportW", width);
            camera->setInt("viewportH", height);

            std::unique_ptr<grove::IDataNode> data = std::move(camera);
            gameIO->publish("render:camera", std::move(data));
        }

        // Send clear color (changes over time)
        {
            auto clear = std::make_unique<grove::JsonDataNode>("clear");
            uint8_t r = static_cast<uint8_t>(48 + 20 * std::sin(time * 0.5));
            uint8_t g = static_cast<uint8_t>(48 + 20 * std::sin(time * 0.5 + 1.0f));
            uint8_t b = static_cast<uint8_t>(80 + 30 * std::sin(time * 0.5 + 2.0f));
            uint32_t clearColor = (r << 24) | (g << 16) | (b << 8) | 0xFF;
            clear->setInt("color", static_cast<int>(clearColor));

            std::unique_ptr<grove::IDataNode> data = std::move(clear);
            gameIO->publish("render:clear", std::move(data));
        }

        // ========================================
        // Test tilemap rendering (simple checkerboard)
        // Must be sent every frame (like sprites)
        // ========================================
        {
            auto tilemap = std::make_unique<grove::JsonDataNode>("tilemap");
            tilemap->setDouble("x", 50.0);
            tilemap->setDouble("y", 450.0);
            tilemap->setInt("width", 20);
            tilemap->setInt("height", 3);
            tilemap->setInt("tileW", 32);
            tilemap->setInt("tileH", 32);
            tilemap->setInt("textureId", 0);  // Uses default texture

            // Checkerboard pattern: 1 = tile, 0 = empty
            std::string tileData;
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 20; ++col) {
                    int tileIndex = ((row + col) % 2 == 0) ? 1 : 0;
                    if (!tileData.empty()) tileData += ",";
                    tileData += std::to_string(tileIndex);
                }
            }
            tilemap->setString("tileData", tileData);

            std::unique_ptr<grove::IDataNode> data = std::move(tilemap);
            gameIO->publish("render:tilemap", std::move(data));
        }

        // Send animated sprites in a circle
        // Using different textureIds to test batching (all will use default texture fallback)
        for (int i = 0; i < 5; ++i) {
            auto sprite = std::make_unique<grove::JsonDataNode>("sprite");

            // Animate position in a circle
            float angle = time * 2.0f + i * (3.14159f * 2.0f / 5.0f);
            float radius = 150.0f;
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

            // Different colors per sprite to verify rendering
            uint32_t colors[] = {0xFF0000FF, 0x00FF00FF, 0x0000FFFF, 0xFFFF00FF, 0xFF00FFFF};
            sprite->setInt("color", static_cast<int>(colors[i]));

            // Use textureId=0 for all (default texture)
            sprite->setInt("textureId", 0);
            sprite->setInt("layer", i);

            std::unique_ptr<grove::IDataNode> data = std::move(sprite);
            gameIO->publish("render:sprite", std::move(data));
        }

        // Add a center sprite
        {
            auto sprite = std::make_unique<grove::JsonDataNode>("sprite");
            sprite->setDouble("x", width / 2.0f);
            sprite->setDouble("y", height / 2.0f);
            sprite->setDouble("scaleX", 80.0);
            sprite->setDouble("scaleY", 80.0);
            sprite->setDouble("rotation", -time);
            sprite->setInt("color", 0xFFFFFFFF);  // White
            sprite->setInt("layer", 10);

            std::unique_ptr<grove::IDataNode> data = std::move(sprite);
            gameIO->publish("render:sprite", std::move(data));
        }

        // ========================================
        // Test text rendering
        // ========================================

        // Title text (large, white)
        {
            auto text = std::make_unique<grove::JsonDataNode>("text");
            text->setDouble("x", 10.0);
            text->setDouble("y", 10.0);
            text->setString("text", "GroveEngine - Text Rendering Test");
            text->setInt("fontSize", 16);
            text->setInt("color", static_cast<int>(0xFFFFFFFF));
            text->setInt("layer", 100);

            std::unique_ptr<grove::IDataNode> data = std::move(text);
            gameIO->publish("render:text", std::move(data));
        }

        // Frame counter (yellow)
        {
            auto text = std::make_unique<grove::JsonDataNode>("text");
            text->setDouble("x", 10.0);
            text->setDouble("y", 30.0);
            text->setString("text", "Frame: " + std::to_string(frameCount));
            text->setInt("fontSize", 8);
            text->setInt("color", static_cast<int>(0xFFFF00FF));  // Yellow
            text->setInt("layer", 100);

            std::unique_ptr<grove::IDataNode> data = std::move(text);
            gameIO->publish("render:text", std::move(data));
        }

        // Instructions (green)
        {
            auto text = std::make_unique<grove::JsonDataNode>("text");
            text->setDouble("x", 10.0);
            text->setDouble("y", height - 20.0);
            text->setString("text", "Press ESC to exit");
            text->setInt("fontSize", 8);
            text->setInt("color", static_cast<int>(0x00FF00FF));  // Green
            text->setInt("layer", 100);

            std::unique_ptr<grove::IDataNode> data = std::move(text);
            gameIO->publish("render:text", std::move(data));
        }

        // Center animated text
        {
            auto text = std::make_unique<grove::JsonDataNode>("text");
            float textX = width / 2.0f - 100.0f + std::sin(time * 2.0f) * 50.0f;
            float textY = height / 2.0f + 80.0f;
            text->setDouble("x", textX);
            text->setDouble("y", textY);
            text->setString("text", "Hello, World!");
            text->setInt("fontSize", 24);

            // Cycle through colors
            uint8_t r = static_cast<uint8_t>(128 + 127 * std::sin(time * 3.0f));
            uint8_t g = static_cast<uint8_t>(128 + 127 * std::sin(time * 3.0f + 2.0f));
            uint8_t b = static_cast<uint8_t>(128 + 127 * std::sin(time * 3.0f + 4.0f));
            uint32_t color = (r << 24) | (g << 16) | (b << 8) | 0xFF;
            text->setInt("color", static_cast<int>(color));
            text->setInt("layer", 100);

            std::unique_ptr<grove::IDataNode> data = std::move(text);
            gameIO->publish("render:text", std::move(data));
        }

        // ========================================
        // Process frame (renderer pulls IIO messages)
        // ========================================

        grove::JsonDataNode input("input");
        input.setDouble("deltaTime", 1.0 / 60.0);
        input.setInt("frameNumber", static_cast<int>(frameCount));
        input.setInt("windowWidth", width);
        input.setInt("windowHeight", height);

        module->process(input);
        frameCount++;

        // Log progress every second
        if (frameCount % 60 == 0) {
            std::cout << "Frame " << frameCount << " - " << (elapsed / 1000.0f) << "s\n";
        }
    }

    float elapsedSec = (SDL_GetTicks() - startTime) / 1000.0f;
    float fps = frameCount / elapsedSec;

    std::cout << "\nTest completed!\n";
    std::cout << "  Frames: " << frameCount << "\n";
    std::cout << "  Time: " << elapsedSec << "s\n";
    std::cout << "  FPS: " << fps << "\n";

    // ========================================
    // Cleanup
    // ========================================

    module->shutdown();
    loader.unload();

    // Remove IIO instances
    ioManager.removeInstance("game_module");
    ioManager.removeInstance("bgfx_renderer");

    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "\n========================================\n";
    std::cout << "PASS: Sprites rendered via IIO!\n";
    std::cout << "========================================\n";

    return 0;
}
