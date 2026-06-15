/**
 * Test: UIModule ScrollPanel Test (Phase 7.1)
 *
 * Tests the UIScrollPanel implementation:
 * - Vertical scrolling with mouse wheel
 * - Scrollbar rendering and interaction
 * - Drag-to-scroll functionality
 * - Content clipping
 * - Long content lists
 */

#include <SDL.h>
#include <SDL_syswm.h>

#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <iostream>
#include <cstdint>

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "UIModule ScrollPanel Test (Phase 7.1)\n";
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
        "UIModule ScrollPanel Test - Use mouse wheel to scroll - ESC to exit",
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

    // ========================================
    // Setup GroveEngine systems
    // ========================================

    auto& ioManager = grove::IntraIOManager::getInstance();

    auto gameIO = ioManager.createInstance("game_module");
    auto rendererIO = ioManager.createInstance("bgfx_renderer");
    auto uiIO = ioManager.createInstance("ui_module");

    std::cout << "IIO Manager setup complete\n";

    // Subscribe to UI events
    uiIO->subscribe("ui:click");
    uiIO->subscribe("ui:hover");

    // ========================================
    // Load BgfxRenderer module
    // ========================================

    grove::ModuleLoader rendererLoader;

    std::string rendererPath = "../modules/libBgfxRenderer.so";
#ifdef _WIN32
    rendererPath = "../modules/BgfxRenderer.dll";
#endif

    std::unique_ptr<grove::IModule> rendererModule;
    try {
        rendererModule = rendererLoader.load(rendererPath, "bgfx_renderer");
    } catch (const std::exception& e) {
        std::cerr << "Failed to load BgfxRenderer module: " << e.what() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!rendererModule) {
        std::cerr << "Failed to load BgfxRenderer module\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "BgfxRenderer module loaded\n";

    // Configure renderer
    grove::JsonDataNode rendererConfig("config");
    rendererConfig.setInt("windowWidth", width);
    rendererConfig.setInt("windowHeight", height);
    rendererConfig.setString("backend", "auto");
    rendererConfig.setBool("vsync", true);
    rendererConfig.setDouble("nativeWindowHandle", static_cast<double>(reinterpret_cast<uintptr_t>(nativeWindowHandle)));
    rendererConfig.setDouble("nativeDisplayHandle", static_cast<double>(reinterpret_cast<uintptr_t>(nativeDisplayHandle)));
    rendererConfig.setBool("debugOverlay", true);

    rendererModule->setConfiguration(rendererConfig, rendererIO.get(), nullptr);
    std::cout << "BgfxRenderer configured\n";

    // ========================================
    // Load UIModule
    // ========================================

    grove::ModuleLoader uiLoader;

    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/UIModule.dll";
#endif

    std::unique_ptr<grove::IModule> uiModule;
    try {
        uiModule = uiLoader.load(uiPath, "ui_module");
    } catch (const std::exception& e) {
        std::cerr << "Failed to load UIModule: " << e.what() << "\n";
        rendererModule->shutdown();
        rendererLoader.unload();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!uiModule) {
        std::cerr << "Failed to load UIModule\n";
        rendererModule->shutdown();
        rendererLoader.unload();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "UIModule loaded\n";

    // Configure UI module with scroll test JSON
    grove::JsonDataNode uiConfig("config");
    uiConfig.setInt("windowWidth", width);
    uiConfig.setInt("windowHeight", height);
    uiConfig.setString("layoutFile", "../../assets/ui/test_scroll.json");
    uiConfig.setInt("baseLayer", 1000);

    uiModule->setConfiguration(uiConfig, uiIO.get(), nullptr);
    std::cout << "UIModule configured with scroll test\n";

    // ========================================
    // Main loop
    // ========================================

    std::cout << "\n*** ScrollPanel Test Running ***\n";
    std::cout << "You should see:\n";
    std::cout << "  - A scroll panel with many items (30+)\n";
    std::cout << "  - A scrollbar on the right side\n";
    std::cout << "  - Use MOUSE WHEEL to scroll up/down\n";
    std::cout << "  - Drag scrollbar to navigate\n";
    std::cout << "  - Drag content to scroll\n";
    std::cout << "\nPress ESC to exit or wait 60 seconds\n\n";

    bool running = true;
    uint32_t frameCount = 0;
    Uint32 startTime = SDL_GetTicks();
    const Uint32 testDuration = 60000;  // 60 seconds

    int mouseX = 0, mouseY = 0;

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

            // Forward mouse events to UI
            if (event.type == SDL_MOUSEMOTION) {
                mouseX = event.motion.x;
                mouseY = event.motion.y;

                auto mouseMove = std::make_unique<grove::JsonDataNode>("mouse_move");
                mouseMove->setDouble("x", static_cast<double>(mouseX));
                mouseMove->setDouble("y", static_cast<double>(mouseY));
                uiIO->publish("input:mouse:move", std::move(mouseMove));
            }
            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                auto mouseButton = std::make_unique<grove::JsonDataNode>("mouse_button");
                mouseButton->setInt("button", event.button.button - 1);
                mouseButton->setBool("pressed", event.type == SDL_MOUSEBUTTONDOWN);
                mouseButton->setDouble("x", static_cast<double>(mouseX));
                mouseButton->setDouble("y", static_cast<double>(mouseY));
                uiIO->publish("input:mouse:button", std::move(mouseButton));
            }
            if (event.type == SDL_MOUSEWHEEL) {
                auto mouseWheel = std::make_unique<grove::JsonDataNode>("mouse_wheel");
                // SDL wheel: y > 0 = scroll up, y < 0 = scroll down
                mouseWheel->setDouble("delta", static_cast<double>(event.wheel.y));
                uiIO->publish("input:mouse:wheel", std::move(mouseWheel));

                std::cout << "  [MOUSE WHEEL] delta: " << event.wheel.y << "\n";
            }
        }

        // Check timeout
        Uint32 elapsed = SDL_GetTicks() - startTime;
        if (elapsed > testDuration) {
            running = false;
        }

        // Check for UI events
        while (uiIO->hasMessages() > 0) {
            auto msg = uiIO->pullMessage();

            if (msg.topic == "ui:click") {
                std::string widgetId = msg.data->getString("widgetId", "");
                std::cout << "  [UI EVENT] Click: " << widgetId << "\n";
            }
        }

        // ========================================
        // Render
        // ========================================

        // Camera
        {
            auto camera = std::make_unique<grove::JsonDataNode>("camera");
            camera->setDouble("x", 0.0);
            camera->setDouble("y", 0.0);
            camera->setDouble("zoom", 1.0);
            camera->setInt("viewportW", width);
            camera->setInt("viewportH", height);
            gameIO->publish("render:camera", std::move(camera));
        }

        // Clear color
        {
            auto clear = std::make_unique<grove::JsonDataNode>("clear");
            clear->setInt("color", static_cast<int>(0x1a1a1aFF));
            gameIO->publish("render:clear", std::move(clear));
        }

        // Process UI module
        grove::JsonDataNode uiInput("input");
        uiInput.setDouble("deltaTime", 1.0 / 60.0);
        uiModule->process(uiInput);

        // Process renderer
        grove::JsonDataNode renderInput("input");
        renderInput.setDouble("deltaTime", 1.0 / 60.0);
        renderInput.setInt("windowWidth", width);
        renderInput.setInt("windowHeight", height);
        rendererModule->process(renderInput);

        frameCount++;
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

    uiModule->shutdown();
    uiLoader.unload();

    rendererModule->shutdown();
    rendererLoader.unload();

    ioManager.removeInstance("game_module");
    ioManager.removeInstance("bgfx_renderer");
    ioManager.removeInstance("ui_module");

    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "\n========================================\n";
    std::cout << "PASS: UIModule Phase 7.1 ScrollPanel Test Complete!\n";
    std::cout << "========================================\n";

    return 0;
}
