/**
 * UIModule Interactive Showcase Demo
 *
 * Features:
 * - All widget types (9 widgets)
 * - ScrollPanel with dynamic content
 * - Tooltips on every widget
 * - Live event console
 * - Hot-reload support
 * - Interactive controls
 *
 * Controls:
 * - Mouse: Click, hover, drag
 * - Keyboard: Type in text fields
 * - Mouse wheel: Scroll panels
 * - ESC: Quit
 * - R: Reload UI from JSON
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>

#include <iostream>
#include <memory>
#include <chrono>
#include <deque>
#include <sstream>

using namespace grove;

// Event log for displaying in console
struct EventLog {
    std::deque<std::string> messages;
    static const size_t MAX_MESSAGES = 15;

    void add(const std::string& msg) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << "[" << (time % 100) << "." << ms.count() << "] " << msg;

        messages.push_back(ss.str());
        if (messages.size() > MAX_MESSAGES) {
            messages.pop_front();
        }

        std::cout << ss.str() << std::endl;
    }

    void clear() {
        messages.clear();
    }
};

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "   UIModule Interactive Showcase Demo  \n";
    std::cout << "========================================\n\n";
    std::cout << "Controls:\n";
    std::cout << "  - Mouse: Click, hover, drag widgets\n";
    std::cout << "  - Keyboard: Type in text fields\n";
    std::cout << "  - Mouse wheel: Scroll panels\n";
    std::cout << "  - ESC: Quit\n";
    std::cout << "  - R: Reload UI from JSON\n\n";

    EventLog eventLog;
    eventLog.add("Demo starting...");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    const int WINDOW_WIDTH = 1200;
    const int WINDOW_HEIGHT = 800;

    SDL_Window* window = SDL_CreateWindow(
        "UIModule Showcase - All Features Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    eventLog.add("SDL window created");

    // Setup IIO
    auto& ioManager = IntraIOManager::getInstance();
    auto rendererIO = ioManager.createInstance("bgfx_renderer");
    auto uiIO = ioManager.createInstance("ui_module");

    // Load BgfxRenderer
    ModuleLoader rendererLoader;
    std::string rendererPath = "../modules/libBgfxRenderer.so";
#ifdef _WIN32
    rendererPath = "../modules/BgfxRenderer.dll";
#endif

    std::unique_ptr<IModule> renderer;
    try {
        renderer = rendererLoader.load(rendererPath, "bgfx_renderer");
        eventLog.add("BgfxRenderer loaded");
    } catch (const std::exception& e) {
        std::cerr << "Failed to load renderer: " << e.what() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Configure renderer
    JsonDataNode rendererConfig("config");
    rendererConfig.setInt("windowWidth", WINDOW_WIDTH);
    rendererConfig.setInt("windowHeight", WINDOW_HEIGHT);
    rendererConfig.setString("backend", "auto");
    rendererConfig.setBool("vsync", true);

    // Pass SDL window native handle
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
#ifdef _WIN32
        rendererConfig.setInt("windowHandle", reinterpret_cast<int64_t>(wmInfo.info.win.window));
#elif __linux__
        rendererConfig.setInt("windowHandle", static_cast<int64_t>(wmInfo.info.x11.window));
#endif
    }

    renderer->setConfiguration(rendererConfig, rendererIO.get(), nullptr);
    eventLog.add("Renderer configured");

    // Load UIModule
    ModuleLoader uiLoader;
    std::string uiPath = "../modules/libUIModule.so";
#ifdef _WIN32
    uiPath = "../modules/UIModule.dll";
#endif

    std::unique_ptr<IModule> uiModule;
    try {
        uiModule = uiLoader.load(uiPath, "ui_module");
        eventLog.add("UIModule loaded");
    } catch (const std::exception& e) {
        std::cerr << "Failed to load UI module: " << e.what() << std::endl;
        renderer->shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Configure UIModule
    JsonDataNode uiConfig("config");
    uiConfig.setInt("windowWidth", WINDOW_WIDTH);
    uiConfig.setInt("windowHeight", WINDOW_HEIGHT);
    uiConfig.setString("layoutFile", "../../assets/ui/demo_showcase.json");
    uiConfig.setInt("baseLayer", 1000);

    uiModule->setConfiguration(uiConfig, uiIO.get(), nullptr);
    eventLog.add("UIModule configured");

    // Subscribe to UI events
    uiIO->subscribe("ui:click");
    uiIO->subscribe("ui:action");
    uiIO->subscribe("ui:value_changed");
    uiIO->subscribe("ui:text_changed");
    uiIO->subscribe("ui:text_submit");
    uiIO->subscribe("ui:hover");
    uiIO->subscribe("ui:focus_gained");
    uiIO->subscribe("ui:focus_lost");

    eventLog.add("Ready! Interact with widgets below.");

    // Check renderer health
    auto rendererHealth = renderer->getHealthStatus();
    bool rendererOK = rendererHealth &&
                     rendererHealth->getString("status", "") == "healthy";

    if (!rendererOK) {
        std::cout << "⚠️  Renderer not healthy, running in UI-only mode (no rendering)\n";
        eventLog.add("⚠️ Renderer offline - UI-only mode");
    } else {
        std::cout << "✅ Renderer healthy\n";
        eventLog.add("✅ Renderer active");
    }

    // Stats
    int clickCount = 0;
    int actionCount = 0;
    int valueChangeCount = 0;
    int hoverCount = 0;

    // Main loop
    bool running = true;
    auto lastFrameTime = std::chrono::high_resolution_clock::now();

    while (running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        // Process SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
                else if (event.key.keysym.sym == SDLK_r) {
                    // Hot-reload UI
                    eventLog.add("🔄 Reloading UI from JSON...");
                    uiModule->setConfiguration(uiConfig, uiIO.get(), nullptr);
                    eventLog.add("✅ UI reloaded!");
                }
                else {
                    // Forward keyboard to UI
                    auto keyPress = std::make_unique<JsonDataNode>("key_press");
                    keyPress->setInt("key", event.key.keysym.sym);
                    keyPress->setInt("char", event.key.keysym.sym);
                    uiIO->publish("input:key:press", std::move(keyPress));
                }
            }
            else if (event.type == SDL_MOUSEMOTION) {
                auto mouseMove = std::make_unique<JsonDataNode>("mouse_move");
                mouseMove->setDouble("x", static_cast<double>(event.motion.x));
                mouseMove->setDouble("y", static_cast<double>(event.motion.y));
                uiIO->publish("input:mouse:move", std::move(mouseMove));
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                auto mouseButton = std::make_unique<JsonDataNode>("mouse_button");
                mouseButton->setInt("button", event.button.button - 1);
                mouseButton->setBool("pressed", event.type == SDL_MOUSEBUTTONDOWN);
                mouseButton->setDouble("x", static_cast<double>(event.button.x));
                mouseButton->setDouble("y", static_cast<double>(event.button.y));
                uiIO->publish("input:mouse:button", std::move(mouseButton));
            }
            else if (event.type == SDL_MOUSEWHEEL) {
                auto mouseWheel = std::make_unique<JsonDataNode>("mouse_wheel");
                mouseWheel->setDouble("delta", static_cast<double>(event.wheel.y));
                uiIO->publish("input:mouse:wheel", std::move(mouseWheel));
            }
            else if (event.type == SDL_TEXTINPUT) {
                auto textInput = std::make_unique<JsonDataNode>("text_input");
                textInput->setString("text", event.text.text);
                uiIO->publish("input:text", std::move(textInput));
            }
        }

        // Process UI events
        while (uiIO->hasMessages() > 0) {
            auto msg = uiIO->pullMessage();

            if (msg.topic == "ui:click") {
                clickCount++;
                std::string widgetId = msg.data->getString("widgetId", "");
                eventLog.add("🖱️  Click: " + widgetId);
            }
            else if (msg.topic == "ui:action") {
                actionCount++;
                std::string action = msg.data->getString("action", "");
                std::string widgetId = msg.data->getString("widgetId", "");
                eventLog.add("⚡ Action: " + action + " (" + widgetId + ")");

                // Handle demo actions
                if (action == "demo:clear_log") {
                    eventLog.clear();
                    eventLog.add("Log cleared");
                }
                else if (action == "demo:reset_stats") {
                    clickCount = 0;
                    actionCount = 0;
                    valueChangeCount = 0;
                    hoverCount = 0;
                    eventLog.add("Stats reset");
                }
            }
            else if (msg.topic == "ui:value_changed") {
                valueChangeCount++;
                std::string widgetId = msg.data->getString("widgetId", "");

                if (msg.data->hasChild("value")) {
                    double value = msg.data->getDouble("value", 0.0);
                    eventLog.add("📊 Value: " + widgetId + " = " + std::to_string(static_cast<int>(value)));
                }
                else if (msg.data->hasChild("checked")) {
                    bool checked = msg.data->getBool("checked", false);
                    eventLog.add("☑️  Checkbox: " + widgetId + " = " + (checked ? "ON" : "OFF"));
                }
            }
            else if (msg.topic == "ui:text_changed") {
                std::string widgetId = msg.data->getString("widgetId", "");
                std::string text = msg.data->getString("text", "");
                eventLog.add("✏️  Text: " + widgetId + " = \"" + text + "\"");
            }
            else if (msg.topic == "ui:text_submit") {
                std::string widgetId = msg.data->getString("widgetId", "");
                std::string text = msg.data->getString("text", "");
                eventLog.add("✅ Submit: " + widgetId + " = \"" + text + "\"");
            }
            else if (msg.topic == "ui:hover") {
                bool enter = msg.data->getBool("enter", false);
                if (enter) {
                    hoverCount++;
                    // Don't log hover to avoid spam
                }
            }
        }

        // Update modules
        JsonDataNode frameInput("input");
        frameInput.setDouble("deltaTime", deltaTime);

        uiModule->process(frameInput);

        // Only call renderer if it's healthy
        if (rendererOK) {
            renderer->process(frameInput);
        }

        // Limit framerate to ~60fps
        SDL_Delay(16);
    }

    // Cleanup
    std::cout << "\nShutdown sequence...\n";

    eventLog.add("Shutting down...");
    std::cout << "\nFinal stats:\n";
    std::cout << "  Clicks: " << clickCount << "\n";
    std::cout << "  Actions: " << actionCount << "\n";
    std::cout << "  Value changes: " << valueChangeCount << "\n";
    std::cout << "  Hovers: " << hoverCount << "\n";

    uiModule->shutdown();
    uiModule.reset();
    uiLoader.unload();

    renderer->shutdown();
    renderer.reset();
    rendererLoader.unload();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ioManager.removeInstance("bgfx_renderer");
    ioManager.removeInstance("ui_module");

    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "\n✅ Demo shutdown complete\n";
    return 0;
}
