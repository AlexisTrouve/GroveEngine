/**
 * Test: BgfxRenderer Visual Sprites Test
 *
 * Tests the full BgfxRendererModule with sprites via IIO.
 * This validates Phase 5: sprites rendered via the module pipeline.
 */

#include <SDL.h>
#include <SDL_syswm.h>

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

    // Load textures from assets folder
    config.setString("defaultTexture", "../../assets/textures/1f440.png");
    config.setString("texture1", "../../assets/textures/5oxaxt1vo2f91.jpg");  // Second texture (Multipla)

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
        // Using different textureIds to test multi-texture batching
        for (int i = 0; i < 5; ++i) {
            auto sprite = std::make_unique<grove::JsonDataNode>("sprite");

            // Animate position in a circle
            float angle = time * 2.0f + i * (3.14159f * 2.0f / 5.0f);
            float radius = 150.0f;
            float centerX = width / 2.0f;
            float centerY = height / 2.0f;

            sprite->setDouble("cx", centerX + std::cos(angle) * radius);
            sprite->setDouble("cy", centerY + std::sin(angle) * radius);
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

            // Alternate between textures: even = eye (id=1), odd = multipla (id=2)
            sprite->setInt("textureId", (i % 2 == 0) ? 1 : 2);
            sprite->setInt("layer", i);

            std::unique_ptr<grove::IDataNode> data = std::move(sprite);
            gameIO->publish("render:sprite", std::move(data));
        }

        // Add a center sprite (Multipla!)
        {
            auto sprite = std::make_unique<grove::JsonDataNode>("sprite");
            sprite->setDouble("cx", width / 2.0f);
            sprite->setDouble("cy", height / 2.0f);
            sprite->setDouble("scaleX", 120.0);
            sprite->setDouble("scaleY", 80.0);  // Wider than tall (car aspect ratio)
            sprite->setDouble("rotation", -time * 0.5f);
            sprite->setInt("color", 0xFFFFFFFF);  // White (no tint)
            sprite->setInt("textureId", 2);  // Multipla texture
            sprite->setInt("layer", 10);

            std::unique_ptr<grove::IDataNode> data = std::move(sprite);
            gameIO->publish("render:sprite", std::move(data));
        }

        // ========================================
        // Test particle rendering (fire-like effect)
        // ========================================
        for (int i = 0; i < 20; ++i) {
            auto particle = std::make_unique<grove::JsonDataNode>("particle");

            // Spawn particles rising from bottom center
            float spawnX = width / 2.0f + (std::sin(time * 5.0f + i * 0.5f) * 50.0f);
            float spawnY = height - 50.0f;

            // Particles rise up with some horizontal drift
            float particleAge = std::fmod(time * 2.0f + i * 0.2f, 2.0f);  // 0-2 second cycle
            float life = 1.0f - (particleAge / 2.0f);  // 1.0 -> 0.0

            float px = spawnX + std::sin(particleAge * 3.0f + i) * 30.0f;
            float py = spawnY - particleAge * 100.0f;  // Rise up

            particle->setDouble("cx", px);
            particle->setDouble("cy", py);
            particle->setDouble("vx", 0.0);
            particle->setDouble("vy", -50.0);
            particle->setDouble("size", 15.0 + life * 20.0);  // Shrink as they age
            particle->setDouble("life", life);

            // Fire colors: white -> yellow -> orange -> red based on life
            uint8_t r, g, b;
            if (life > 0.7f) {
                // White/yellow core
                r = 255; g = 255; b = static_cast<uint8_t>(200 * (life - 0.7f) / 0.3f);
            } else if (life > 0.3f) {
                // Orange
                r = 255; g = static_cast<uint8_t>(100 + 155 * (life - 0.3f) / 0.4f); b = 0;
            } else {
                // Red fading out
                r = static_cast<uint8_t>(200 + 55 * life / 0.3f); g = static_cast<uint8_t>(50 * life / 0.3f); b = 0;
            }
            uint32_t color = (r << 24) | (g << 16) | (b << 8) | 0xFF;
            particle->setInt("color", static_cast<int>(color));
            particle->setInt("textureId", 0);

            std::unique_ptr<grove::IDataNode> data = std::move(particle);
            gameIO->publish("render:particle", std::move(data));
        }

        // ========================================
        // Smoke particles (second particle system - alpha blending, gray colors)
        // ========================================
        float smokeSpawnX = width * 0.7f;  // Right side
        float smokeSpawnY = height * 0.8f;

        for (int i = 0; i < 15; ++i) {
            auto smoke = std::make_unique<grove::JsonDataNode>("particle");

            // Smoke rises slower, drifts more
            float smokeAge = std::fmod(time * 1.0f + i * 0.15f, 3.0f);  // 3 second cycle
            float life = 1.0f - (smokeAge / 3.0f);

            float sx = smokeSpawnX + std::sin(smokeAge * 2.0f + i * 0.5f) * 50.0f;
            float sy = smokeSpawnY - smokeAge * 60.0f;  // Rise slower

            smoke->setDouble("cx", sx);
            smoke->setDouble("cy", sy);
            smoke->setDouble("vx", std::sin(time + i) * 20.0f);
            smoke->setDouble("vy", -30.0);
            smoke->setDouble("size", 20.0 + (1.0f - life) * 40.0);  // Grow as they age
            smoke->setDouble("life", life);

            // Smoke colors: dark gray -> light gray, fading alpha
            uint8_t gray = static_cast<uint8_t>(80 + 80 * (1.0f - life));  // Gets lighter
            uint8_t alpha = static_cast<uint8_t>(200 * life);  // Fades out
            uint32_t smokeColor = (gray << 24) | (gray << 16) | (gray << 8) | alpha;
            smoke->setInt("color", static_cast<int>(smokeColor));
            smoke->setInt("textureId", 0);
            smoke->setInt("blendMode", 0);  // Alpha blend (not additive)

            std::unique_ptr<grove::IDataNode> smokeData = std::move(smoke);
            gameIO->publish("render:particle", std::move(smokeData));
        }

        // ========================================
        // Sparkle particles (third particle system - small, fast, bright)
        // ========================================
        float sparkleX = width * 0.5f;
        float sparkleY = height * 0.3f;

        for (int i = 0; i < 8; ++i) {
            auto sparkle = std::make_unique<grove::JsonDataNode>("particle");

            float angle = (time * 3.0f + i * (3.14159f * 2.0f / 8.0f));
            float radius = 50.0f + std::sin(time * 5.0f + i) * 20.0f;
            float sparkleAge = std::fmod(time * 4.0f + i * 0.1f, 1.0f);
            float life = 1.0f - sparkleAge;

            float spx = sparkleX + std::cos(angle) * radius;
            float spy = sparkleY + std::sin(angle) * radius * 0.5f;

            sparkle->setDouble("cx", spx);
            sparkle->setDouble("cy", spy);
            sparkle->setDouble("vx", 0.0);
            sparkle->setDouble("vy", 0.0);
            sparkle->setDouble("size", 5.0 + life * 10.0);
            sparkle->setDouble("life", life);

            // Sparkle colors: cyan -> white -> magenta cycle
            float hue = std::fmod(time * 0.5f + i * 0.1f, 1.0f);
            uint8_t r, g, b;
            if (hue < 0.33f) {
                r = static_cast<uint8_t>(255 * (1.0f - hue * 3.0f));
                g = 255;
                b = static_cast<uint8_t>(255 * hue * 3.0f);
            } else if (hue < 0.66f) {
                r = static_cast<uint8_t>(255 * (hue - 0.33f) * 3.0f);
                g = static_cast<uint8_t>(255 * (1.0f - (hue - 0.33f) * 3.0f));
                b = 255;
            } else {
                r = 255;
                g = static_cast<uint8_t>(255 * (hue - 0.66f) * 3.0f);
                b = static_cast<uint8_t>(255 * (1.0f - (hue - 0.66f) * 3.0f));
            }
            uint32_t sparkleColor = (r << 24) | (g << 16) | (b << 8) | 0xFF;
            sparkle->setInt("color", static_cast<int>(sparkleColor));
            sparkle->setInt("textureId", 0);

            std::unique_ptr<grove::IDataNode> sparkleData = std::move(sparkle);
            gameIO->publish("render:particle", std::move(sparkleData));
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
