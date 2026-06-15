/**
 * Visual Test: Full Stack Interactive Demo
 *
 * Demonstrates complete integration of:
 * - BgfxRenderer (2D rendering)
 * - UIModule (widgets)
 * - InputModule (mouse + keyboard)
 * - Game logic responding to UI events
 *
 * Controls:
 * - Mouse: Click buttons, drag sliders
 * - Keyboard: Type in text input, press Space to spawn sprites
 * - ESC: Exit
 *
 * Build modes:
 * - USE_STATIC_BGFX: Link BgfxRenderer statically (required on Windows)
 * - Default: Load BgfxRenderer as DLL (works on Linux/Mac)
 */

#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <iostream>
#include <vector>
#include <random>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Static linking for BgfxRenderer (required on Windows due to bgfx DLL issues)
#ifdef USE_STATIC_BGFX
#include "BgfxRendererModule.h"
#endif

// Function pointer type for feedEvent (loaded from DLL)
typedef void (*FeedEventFunc)(grove::IModule*, const void*);

using namespace grove;

// Simple game state
struct Sprite {
    float x, y;
    float vx, vy;
    uint32_t color;
};

class GameLogic {
public:
    GameLogic(IIO* io) : m_io(io) {
        m_logger = spdlog::stdout_color_mt("GameLogic");

        // Subscribe to UI events (callback-based dispatch; pull model removed from IIO).
        // Each former branch of the update() pull switch is migrated into the callback for
        // its topic, capturing 'this' so the game-state mutations and logs are preserved.
        m_io->subscribe("ui:click", [](const grove::Message&) {});
        m_io->subscribe("ui:action", [this](const grove::Message& msg) {
            std::string action = msg.data->getString("action", "");
            m_logger->info("UI Action: {}", action);

            if (action == "spawn_sprite") {
                spawnSprite();
            } else if (action == "clear_sprites") {
                m_sprites.clear();
                m_logger->info("Cleared all sprites");
            } else if (action == "toggle_background") {
                m_darkBackground = !m_darkBackground;
                m_logger->info("Background: {}", m_darkBackground ? "Dark" : "Light");
            }
        });
        m_io->subscribe("ui:value_changed", [this](const grove::Message& msg) {
            std::string widgetId = msg.data->getString("widgetId", "");

            if (widgetId == "speed_slider") {
                m_spawnSpeed = static_cast<float>(msg.data->getDouble("value", 100.0));
                m_logger->info("Spawn speed: {}", m_spawnSpeed);
            }
        });
        m_io->subscribe("input:keyboard:key", [this](const grove::Message& msg) {
            int scancode = msg.data->getInt("scancode", 0);
            bool pressed = msg.data->getBool("pressed", false);

            if (pressed && scancode == SDL_SCANCODE_SPACE) {
                spawnSprite();
            }
        });
    }

    void update(float deltaTime) {
        // Update sprites
        for (auto& sprite : m_sprites) {
            sprite.x += sprite.vx * deltaTime;
            sprite.y += sprite.vy * deltaTime;

            // Bounce off walls (1280x720 window)
            if (sprite.x < 0 || sprite.x > 1280) sprite.vx = -sprite.vx;
            if (sprite.y < 0 || sprite.y > 720) sprite.vy = -sprite.vy;
        }

        // Process events (callbacks registered in the constructor dispatch automatically)
        while (m_io->hasMessages() > 0) {
            m_io->pullAndDispatch();
        }
    }

    void render(IIO* rendererIO) {
        // Publish clear color
        auto clear = std::make_unique<JsonDataNode>("clear");
        clear->setInt("color", m_darkBackground ? 0x1a1a1aFF : 0x303030FF);
        rendererIO->publish("render:clear", std::move(clear));

        // Render sprites
        int layer = 5;
        for (const auto& sprite : m_sprites) {
            auto spriteNode = std::make_unique<JsonDataNode>("sprite");
            spriteNode->setDouble("x", sprite.x);
            spriteNode->setDouble("y", sprite.y);
            spriteNode->setDouble("scaleX", 32.0);
            spriteNode->setDouble("scaleY", 32.0);
            spriteNode->setDouble("rotation", 0.0);
            spriteNode->setDouble("u0", 0.0);
            spriteNode->setDouble("v0", 0.0);
            spriteNode->setDouble("u1", 1.0);
            spriteNode->setDouble("v1", 1.0);
            spriteNode->setInt("color", sprite.color);
            spriteNode->setInt("textureId", 0);  // White texture
            spriteNode->setInt("layer", layer);
            rendererIO->publish("render:sprite", std::move(spriteNode));
        }

        // Render sprite count
        auto text = std::make_unique<JsonDataNode>("text");
        text->setDouble("x", 20.0);
        text->setDouble("y", 20.0);
        text->setString("text", "Sprites: " + std::to_string(m_sprites.size()) + " (Press SPACE to spawn)");
        text->setDouble("fontSize", 24.0);
        text->setInt("color", 0xFFFFFFFF);
        text->setInt("layer", 2000);  // Above UI
        rendererIO->publish("render:text", std::move(text));
    }

private:
    void spawnSprite() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> posX(100.0f, 1820.0f);
        std::uniform_real_distribution<float> posY(100.0f, 980.0f);
        std::uniform_real_distribution<float> vel(-1.0f, 1.0f);
        std::uniform_int_distribution<uint32_t> colorDist(0x80000000, 0xFFFFFFFF);

        Sprite sprite;
        sprite.x = posX(gen);
        sprite.y = posY(gen);
        sprite.vx = vel(gen) * m_spawnSpeed;
        sprite.vy = vel(gen) * m_spawnSpeed;
        sprite.color = colorDist(gen) | 0xFF;  // Force full alpha

        m_sprites.push_back(sprite);
        m_logger->info("Spawned sprite at ({}, {})", sprite.x, sprite.y);
    }

    IIO* m_io;
    std::shared_ptr<spdlog::logger> m_logger;
    std::vector<Sprite> m_sprites;
    float m_spawnSpeed = 100.0f;
    bool m_darkBackground = false;
};

#undef main  // Undefine SDL's main macro for Windows

int main(int argc, char* argv[]) {
    // Setup logging to both console AND file
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("full_stack_demo.log", true);

        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("Main", sinks.begin(), sinks.end());
        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);  // Auto-flush pour pas perdre de logs
    } catch (const std::exception& e) {
        std::cerr << "Failed to setup logging: " << e.what() << "\n";
        return 1;
    }

    auto logger = spdlog::get("Main");

    logger->info("==============================================");
    logger->info("  Full Stack Interactive Demo");
    logger->info("==============================================");
    logger->info("Log file: full_stack_demo.log");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        logger->error("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    // Create window (windowed mode, not fullscreen)
    const int WINDOW_WIDTH = 1280;
    const int WINDOW_HEIGHT = 720;
    SDL_Window* window = SDL_CreateWindow(
        "GroveEngine - Full Stack Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        logger->error("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Get native window handle
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    void* nativeHandle = nullptr;

#ifdef _WIN32
    nativeHandle = wmInfo.info.win.window;
#elif defined(__linux__)
    nativeHandle = (void*)(uintptr_t)wmInfo.info.x11.window;
#elif defined(__APPLE__)
    nativeHandle = wmInfo.info.cocoa.window;
#endif

    logger->info("Native window handle: {}", nativeHandle);

    // Create IIO instances
    auto& ioManager = IntraIOManager::getInstance();
    auto rendererIO = ioManager.createInstance("renderer");
    auto uiIO = ioManager.createInstance("ui");
    auto inputIO = ioManager.createInstance("input");
    auto gameIO = ioManager.createInstance("game");

    // Load modules
    ModuleLoader uiLoader, inputLoader;
#ifndef USE_STATIC_BGFX
    ModuleLoader rendererLoader;
#endif

    std::string uiPath = "./modules/UIModule.dll";
    std::string inputPath = "./modules/InputModule.dll";

#ifndef _WIN32
    uiPath = "./modules/libUIModule.so";
    inputPath = "./modules/libInputModule.so";
#endif

    logger->info("Loading modules...");

    // Load/Create BgfxRenderer
    std::unique_ptr<IModule> renderer;
#ifdef USE_STATIC_BGFX
    // Static linking: instantiate directly (required on Windows)
    renderer = std::make_unique<BgfxRendererModule>();
    logger->info("✅ BgfxRenderer created (static)");
#else
    // Dynamic linking: load from DLL
    std::string rendererPath = "./modules/BgfxRenderer.dll";
    #ifndef _WIN32
    rendererPath = "./modules/libBgfxRenderer.so";
    #endif
    try {
        renderer = rendererLoader.load(rendererPath, "renderer");
        logger->info("✅ BgfxRenderer loaded (dynamic)");
    } catch (const std::exception& e) {
        logger->error("Failed to load BgfxRenderer: {}", e.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif

    // Configure BgfxRenderer
    JsonDataNode rendererConfig("config");
    rendererConfig.setInt("windowWidth", WINDOW_WIDTH);
    rendererConfig.setInt("windowHeight", WINDOW_HEIGHT);
    rendererConfig.setString("backend", "opengl");  // Force OpenGL instead of D3D11
    rendererConfig.setBool("vsync", true);
    rendererConfig.setInt("nativeWindowHandle", (int)(intptr_t)nativeHandle);
    renderer->setConfiguration(rendererConfig, rendererIO.get(), nullptr);

    // Load UIModule
    std::unique_ptr<IModule> uiModule;
    try {
        uiModule = uiLoader.load(uiPath, "ui");
        logger->info("✅ UIModule loaded");
    } catch (const std::exception& e) {
        logger->error("Failed to load UIModule: {}", e.what());
        renderer->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Configure UIModule with inline layout
    JsonDataNode uiConfig("config");
    uiConfig.setInt("windowWidth", WINDOW_WIDTH);
    uiConfig.setInt("windowHeight", WINDOW_HEIGHT);
    uiConfig.setInt("baseLayer", 1000);

    // Create inline layout
    auto layout = std::make_unique<JsonDataNode>("layout");
    auto widgets = std::make_unique<JsonDataNode>("widgets");

    // Panel background
    auto panel = std::make_unique<JsonDataNode>("panel");
    panel->setString("type", "UIPanel");
    panel->setString("id", "control_panel");
    panel->setInt("x", 20);
    panel->setInt("y", 80);
    panel->setInt("width", 300);
    panel->setInt("height", 300);
    panel->setInt("color", 0x404040CC);  // Semi-transparent gray
    widgets->setChild("panel", std::move(panel));

    // Title label
    auto title = std::make_unique<JsonDataNode>("title");
    title->setString("type", "UILabel");
    title->setString("id", "title_label");
    title->setInt("x", 40);
    title->setInt("y", 100);
    title->setInt("width", 260);
    title->setInt("height", 40);
    title->setString("text", "Control Panel");
    title->setInt("fontSize", 28);
    title->setInt("color", 0xFFFFFFFF);
    widgets->setChild("title", std::move(title));

    // Spawn button
    auto spawnBtn = std::make_unique<JsonDataNode>("spawn_button");
    spawnBtn->setString("type", "UIButton");
    spawnBtn->setString("id", "spawn_button");
    spawnBtn->setInt("x", 40);
    spawnBtn->setInt("y", 160);
    spawnBtn->setInt("width", 120);
    spawnBtn->setInt("height", 40);
    spawnBtn->setString("text", "Spawn");
    spawnBtn->setString("action", "spawn_sprite");
    spawnBtn->setInt("fontSize", 20);
    widgets->setChild("spawn_button", std::move(spawnBtn));

    // Clear button
    auto clearBtn = std::make_unique<JsonDataNode>("clear_button");
    clearBtn->setString("type", "UIButton");
    clearBtn->setString("id", "clear_button");
    clearBtn->setInt("x", 180);
    clearBtn->setInt("y", 160);
    clearBtn->setInt("width", 120);
    clearBtn->setInt("height", 40);
    clearBtn->setString("text", "Clear");
    clearBtn->setString("action", "clear_sprites");
    clearBtn->setInt("fontSize", 20);
    widgets->setChild("clear_button", std::move(clearBtn));

    // Speed slider
    auto slider = std::make_unique<JsonDataNode>("speed_slider");
    slider->setString("type", "UISlider");
    slider->setString("id", "speed_slider");
    slider->setInt("x", 40);
    slider->setInt("y", 220);
    slider->setInt("width", 260);
    slider->setInt("height", 30);
    slider->setDouble("min", 10.0);
    slider->setDouble("max", 500.0);
    slider->setDouble("value", 100.0);
    slider->setString("orientation", "horizontal");
    widgets->setChild("speed_slider", std::move(slider));

    // Speed label
    auto speedLabel = std::make_unique<JsonDataNode>("speed_label");
    speedLabel->setString("type", "UILabel");
    speedLabel->setString("id", "speed_label");
    speedLabel->setInt("x", 40);
    speedLabel->setInt("y", 260);
    speedLabel->setInt("width", 260);
    speedLabel->setInt("height", 30);
    speedLabel->setString("text", "Speed: 100");
    speedLabel->setInt("fontSize", 18);
    speedLabel->setInt("color", 0xCCCCCCFF);
    widgets->setChild("speed_label", std::move(speedLabel));

    // Background toggle button
    auto bgBtn = std::make_unique<JsonDataNode>("bg_button");
    bgBtn->setString("type", "UIButton");
    bgBtn->setString("id", "bg_button");
    bgBtn->setInt("x", 40);
    bgBtn->setInt("y", 310);
    bgBtn->setInt("width", 260);
    bgBtn->setInt("height", 40);
    bgBtn->setString("text", "Toggle Background");
    bgBtn->setString("action", "toggle_background");
    bgBtn->setInt("fontSize", 18);
    widgets->setChild("bg_button", std::move(bgBtn));

    layout->setChild("widgets", std::move(widgets));
    uiConfig.setChild("layout", std::move(layout));

    uiModule->setConfiguration(uiConfig, uiIO.get(), nullptr);

    // Load InputModule
    std::unique_ptr<IModule> inputModuleBase;
    FeedEventFunc feedEventFunc = nullptr;
    try {
        inputModuleBase = inputLoader.load(inputPath, "input");
        logger->info("✅ InputModule loaded");

        // Get the feedEvent function from the DLL
#ifdef _WIN32
        HMODULE inputDll = LoadLibraryA(inputPath.c_str());
        if (inputDll) {
            feedEventFunc = (FeedEventFunc)GetProcAddress(inputDll, "feedEventToInputModule");
            if (!feedEventFunc) {
                logger->warn("feedEventToInputModule not found in InputModule.dll");
            }
        }
#else
        void* inputDll = dlopen(inputPath.c_str(), RTLD_NOW);
        if (inputDll) {
            feedEventFunc = (FeedEventFunc)dlsym(inputDll, "feedEventToInputModule");
        }
#endif
    } catch (const std::exception& e) {
        logger->error("Failed to load InputModule: {}", e.what());
        uiModule->shutdown();
        renderer->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!feedEventFunc) {
        logger->error("Failed to get feedEventToInputModule function");
        uiModule->shutdown();
        renderer->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Configure InputModule
    JsonDataNode inputConfig("config");
    inputConfig.setString("backend", "sdl");
    inputConfig.setBool("enableMouse", true);
    inputConfig.setBool("enableKeyboard", true);
    inputModuleBase->setConfiguration(inputConfig, inputIO.get(), nullptr);

    // Create game logic
    GameLogic gameLogic(gameIO.get());

    logger->info("\n==============================================");
    logger->info("Demo started! Controls:");
    logger->info("  - Click buttons to spawn/clear sprites");
    logger->info("  - Drag slider to change speed");
    logger->info("  - Press SPACE to spawn sprite");
    logger->info("  - Press ESC to exit");
    logger->info("==============================================\n");

    // Main loop
    bool running = true;
    Uint64 lastTime = SDL_GetPerformanceCounter();
    int frameCount = 0;

    logger->info("Entering main loop...");
    spdlog::default_logger()->flush();

    while (running) {
        logger->info("Frame {} start", frameCount);
        spdlog::default_logger()->flush();
        logger->info("  SDL_PollEvent...");
        spdlog::default_logger()->flush();
        // Handle SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            logger->info("  Event type: {}", event.type);
            spdlog::default_logger()->flush();
            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }

            // Feed to InputModule via exported C function
            logger->info("  feedEventFunc...");
            spdlog::default_logger()->flush();
            feedEventFunc(inputModuleBase.get(), &event);
        }

        // Calculate deltaTime
        Uint64 now = SDL_GetPerformanceCounter();
        double deltaTime = (now - lastTime) / (double)SDL_GetPerformanceFrequency();
        lastTime = now;

        // Clamp deltaTime to avoid huge jumps
        if (deltaTime > 0.1) deltaTime = 0.016;

        // Process modules
        JsonDataNode input("input");
        input.setDouble("deltaTime", deltaTime);
        input.setInt("frameCount", frameCount);

        logger->info("Processing input module...");
        inputModuleBase->process(input);
        logger->info("Processing UI module...");
        uiModule->process(input);
        logger->info("Updating game logic...");
        gameLogic.update((float)deltaTime);
        logger->info("Rendering game...");
        gameLogic.render(rendererIO.get());
        logger->info("Processing renderer...");
        spdlog::default_logger()->flush();
        renderer->process(input);
        logger->info("Frame {} complete", frameCount);

        frameCount++;
    }

    // Cleanup
    logger->info("\nShutting down...");
    inputModuleBase->shutdown();
    uiModule->shutdown();
    renderer->shutdown();

    ioManager.removeInstance("renderer");
    ioManager.removeInstance("ui");
    ioManager.removeInstance("input");
    ioManager.removeInstance("game");

    SDL_DestroyWindow(window);
    SDL_Quit();

    logger->info("✅ Demo exited cleanly");
    return 0;
}
