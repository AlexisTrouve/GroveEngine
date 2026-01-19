/**
 * Test: InputModule Basic Visual Test
 *
 * Tests the InputModule Phase 1 implementation:
 * - SDL event capture
 * - Mouse move/button/wheel events
 * - Keyboard key/text events
 * - IIO message publishing
 *
 * Instructions:
 * - Move mouse to test mouse:move events
 * - Click buttons to test mouse:button events
 * - Scroll wheel to test mouse:wheel events
 * - Press keys to test keyboard:key events
 * - Type text to test keyboard:text events
 * - Press ESC to exit
 */

#include <SDL2/SDL.h>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include "modules/InputModule/InputModule.h"

#include <iostream>
#include <iomanip>

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "InputModule Visual Test\n";
    std::cout << "========================================\n\n";
    std::cout << "Instructions:\n";
    std::cout << "  - Move mouse to see mouse:move events\n";
    std::cout << "  - Click to see mouse:button events\n";
    std::cout << "  - Scroll to see mouse:wheel events\n";
    std::cout << "  - Press keys to see keyboard:key events\n";
    std::cout << "  - Type to see keyboard:text events\n";
    std::cout << "  - Press ESC to exit\n";
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
        "InputModule Test - Press ESC to exit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    // Enable text input for keyboard:text events
    SDL_StartTextInput();

    std::cout << "Window created: " << width << "x" << height << "\n\n";

    // ========================================
    // Setup GroveEngine systems
    // ========================================

    auto& ioManager = grove::IntraIOManager::getInstance();

    auto inputIO = ioManager.createInstance("input_module");
    auto testIO = ioManager.createInstance("test_controller");

    std::cout << "IIO Manager setup complete\n";

    // ========================================
    // Load InputModule
    // ========================================

    grove::ModuleLoader inputLoader;

    std::string inputPath = "../modules/libInputModule.so";
#ifdef _WIN32
    inputPath = "../modules/InputModule.dll";
#endif

    std::unique_ptr<grove::IModule> inputModuleBase;
    try {
        inputModuleBase = inputLoader.load(inputPath, "input_module");
    } catch (const std::exception& e) {
        std::cerr << "Failed to load InputModule: " << e.what() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!inputModuleBase) {
        std::cerr << "Failed to load InputModule\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Cast to InputModule to access feedEvent()
    grove::InputModule* inputModule = dynamic_cast<grove::InputModule*>(inputModuleBase.get());
    if (!inputModule) {
        std::cerr << "Failed to cast to InputModule\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "InputModule loaded\n";

    // Configure InputModule
    grove::JsonDataNode inputConfig("config");
    inputConfig.setString("backend", "sdl");
    inputConfig.setBool("enableMouse", true);
    inputConfig.setBool("enableKeyboard", true);
    inputConfig.setBool("enableGamepad", false);

    inputModule->setConfiguration(inputConfig, inputIO.get(), nullptr);
    std::cout << "InputModule configured\n\n";

    // ========================================
    // Subscribe to input events
    // ========================================

    // Track last mouse move to avoid spam
    int lastMouseX = -1;
    int lastMouseY = -1;

    testIO->subscribe("input:mouse:move", [&](const Message& msg) {
        int x = msg.data->getInt("x", 0);
        int y = msg.data->getInt("y", 0);

        // Only print if position changed (reduce spam)
        if (x != lastMouseX || y != lastMouseY) {
            std::cout << "[MOUSE MOVE] x=" << std::setw(4) << x
                      << ", y=" << std::setw(4) << y << "\n";
            lastMouseX = x;
            lastMouseY = y;
        }
    });

    testIO->subscribe("input:mouse:button", [](const Message& msg) {
        int button = msg.data->getInt("button", 0);
        bool pressed = msg.data->getBool("pressed", false);
        int x = msg.data->getInt("x", 0);
        int y = msg.data->getInt("y", 0);

        const char* buttonNames[] = { "LEFT", "MIDDLE", "RIGHT" };
        const char* buttonName = (button >= 0 && button < 3) ? buttonNames[button] : "UNKNOWN";

        std::cout << "[MOUSE BUTTON] " << buttonName
                  << " " << (pressed ? "PRESSED" : "RELEASED")
                  << " at (" << x << ", " << y << ")\n";
    });

    testIO->subscribe("input:mouse:wheel", [](const Message& msg) {
        double delta = msg.data->getDouble("delta", 0.0);
        std::cout << "[MOUSE WHEEL] delta=" << delta
                  << " (" << (delta > 0 ? "UP" : "DOWN") << ")\n";
    });

    testIO->subscribe("input:keyboard:key", [](const Message& msg) {
        int scancode = msg.data->getInt("scancode", 0);
        bool pressed = msg.data->getBool("pressed", false);
        bool repeat = msg.data->getBool("repeat", false);
        bool shift = msg.data->getBool("shift", false);
        bool ctrl = msg.data->getBool("ctrl", false);
        bool alt = msg.data->getBool("alt", false);

        const char* keyName = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));

        std::cout << "[KEYBOARD KEY] " << keyName
                  << " " << (pressed ? "PRESSED" : "RELEASED");

        if (repeat) std::cout << " (REPEAT)";
        if (shift || ctrl || alt) {
            std::cout << " [";
            if (shift) std::cout << "SHIFT ";
            if (ctrl) std::cout << "CTRL ";
            if (alt) std::cout << "ALT";
            std::cout << "]";
        }

        std::cout << "\n";
    });

    testIO->subscribe("input:keyboard:text", [](const Message& msg) {
        std::string text = msg.data->getString("text", "");
        std::cout << "[KEYBOARD TEXT] \"" << text << "\"\n";
    });

    std::cout << "Subscribed to all input topics\n";
    std::cout << "========================================\n\n";

    // ========================================
    // Main loop
    // ========================================

    bool running = true;
    uint32_t frameCount = 0;
    uint32_t lastTime = SDL_GetTicks();

    while (running) {
        frameCount++;

        // 1. Poll SDL events and feed to InputModule
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }

            // Feed event to InputModule (thread-safe)
            inputModule->feedEvent(&event);
        }

        // 2. Process InputModule (converts buffered events to IIO messages)
        grove::JsonDataNode input("input");
        inputModule->process(input);

        // 3. Dispatch IIO messages from InputModule (callbacks handle printing)
        while (testIO->hasMessages() > 0) {
            testIO->pullAndDispatch();
        }

        // 4. Cap at ~60 FPS
        SDL_Delay(16);

        // Print stats every 5 seconds
        uint32_t currentTime = SDL_GetTicks();
        if (currentTime - lastTime >= 5000) {
            auto health = inputModule->getHealthStatus();
            std::cout << "\n--- Stats (5s) ---\n";
            std::cout << "Frames: " << health->getInt("frameCount", 0) << "\n";
            std::cout << "Events processed: " << health->getInt("eventsProcessed", 0) << "\n";
            std::cout << "Events/frame: " << std::fixed << std::setprecision(2)
                      << health->getDouble("eventsPerFrame", 0.0) << "\n";
            std::cout << "Status: " << health->getString("status", "unknown") << "\n";
            std::cout << "-------------------\n\n";
            lastTime = currentTime;
        }
    }

    // ========================================
    // Cleanup
    // ========================================

    std::cout << "\n========================================\n";
    std::cout << "Final stats:\n";

    auto finalHealth = inputModule->getHealthStatus();
    std::cout << "Total frames: " << finalHealth->getInt("frameCount", 0) << "\n";
    std::cout << "Total events: " << finalHealth->getInt("eventsProcessed", 0) << "\n";
    std::cout << "Avg events/frame: " << std::fixed << std::setprecision(2)
              << finalHealth->getDouble("eventsPerFrame", 0.0) << "\n";

    inputModule->shutdown();
    inputLoader.unload();

    SDL_StopTextInput();
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "========================================\n";
    std::cout << "Test completed successfully!\n";

    return 0;
}
