/**
 * UIModule Complete Showcase
 *
 * Demonstrates ALL widgets of UIModule:
 * - UIPanel (containers, backgrounds)
 * - UIButton (click actions, hover states)
 * - UILabel (static text, different sizes)
 * - UICheckbox (toggle states)
 * - UISlider (horizontal values)
 * - UITextInput (text entry)
 * - UIProgressBar (animated progress)
 * - UIScrollPanel (scrollable content)
 *
 * Uses BgfxRenderer for rendering.
 * Uses manual SDL input converted to IIO messages.
 *
 * Controls:
 * - Mouse: Interact with UI widgets
 * - ESC: Exit
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <cmath>
#include <memory>
#include <string>

#include "BgfxRendererModule.h"
#include "UIModule.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp>

using namespace grove;

// Create the UI layout JSON - MINIMAL VERSION WITH ONLY TEXTURED BUTTONS
static nlohmann::json createUILayout() {
    nlohmann::json root;

    // Root panel (TRANSPARENT like test_button_with_png!)
    root["type"] = "panel";
    root["id"] = "root";
    root["x"] = 0;
    root["y"] = 0;
    root["width"] = 1024;
    root["height"] = 768;
    root["style"] = {{"bgColor", "0x00000000"}};  // TRANSPARENT!

    // Children array - ONLY TEXTURED BUTTONS
    nlohmann::json children = nlohmann::json::array();

    // Title label
    children.push_back({
        {"type", "label"},
        {"id", "title"},
        {"x", 350}, {"y", 20},
        {"width", 400}, {"height", 50},
        {"text", "UIModule Showcase"},
        {"style", {{"fontSize", 36}, {"color", "0xFFFFFFFF"}}}
    });

    // Subtitle
    children.push_back({
        {"type", "label"},
        {"id", "subtitle"},
        {"x", 380}, {"y", 65},
        {"width", 300}, {"height", 30},
        {"text", "Interactive Widget Demo"},
        {"style", {{"fontSize", 16}, {"color", "0x888888FF"}}}
    });

    // === LEFT COLUMN: Buttons Panel ===
    children.push_back({
        {"type", "panel"},
        {"id", "buttons_panel"},
        {"x", 30}, {"y", 120},
        {"width", 300}, {"height", 280},
        {"style", {{"bgColor", "0x2d3436FF"}}}
    });

    children.push_back({
        {"type", "label"},
        {"id", "buttons_title"},
        {"x", 45}, {"y", 130},
        {"width", 200}, {"height", 30},
        {"text", "Buttons"},
        {"style", {{"fontSize", 20}, {"color", "0xFFFFFFFF"}}}
    });

    // Button 1 - Primary (with very contrasting hover for testing)
    children.push_back({
        {"type", "button"},
        {"id", "btn_primary"},
        {"x", 50}, {"y", 170},
        {"width", 260}, {"height", 45},
        {"text", "Primary Action"},
        {"onClick", "action_primary"},
        {"style", {
            {"normal", {{"bgColor", "0x0984e3FF"}, {"textColor", "0xFFFFFFFF"}}},
            {"hover", {{"bgColor", "0xFF0000FF"}, {"textColor", "0xFFFFFFFF"}}},  // BRIGHT RED
            {"pressed", {{"bgColor", "0x00FF00FF"}, {"textColor", "0xFFFFFFFF"}}}  // BRIGHT GREEN
        }}
    });

    // Button 2 - Success
    children.push_back({
        {"type", "button"},
        {"id", "btn_success"},
        {"x", 50}, {"y", 225},
        {"width", 260}, {"height", 45},
        {"text", "Success Button"},
        {"onClick", "action_success"},
        {"style", {
            {"normal", {{"bgColor", "0x00b894FF"}, {"textColor", "0xFFFFFFFF"}}},
            {"hover", {{"bgColor", "0x55efc4FF"}, {"textColor", "0xFFFFFFFF"}}},
            {"pressed", {{"bgColor", "0x009874FF"}, {"textColor", "0xFFFFFFFF"}}}
        }}
    });

    // Button 3 - Warning
    children.push_back({
        {"type", "button"},
        {"id", "btn_warning"},
        {"x", 50}, {"y", 280},
        {"width", 260}, {"height", 45},
        {"text", "Warning Button"},
        {"onClick", "action_warning"},
        {"style", {
            {"normal", {{"bgColor", "0xfdcb6eFF"}, {"textColor", "0x2d3436FF"}}},
            {"hover", {{"bgColor", "0xffeaa7FF"}, {"textColor", "0x2d3436FF"}}},
            {"pressed", {{"bgColor", "0xd4a84eFF"}, {"textColor", "0x2d3436FF"}}}
        }}
    });

    // Button 4 - Danger
    children.push_back({
        {"type", "button"},
        {"id", "btn_danger"},
        {"x", 50}, {"y", 335},
        {"width", 260}, {"height", 45},
        {"text", "Danger Button"},
        {"onClick", "action_danger"},
        {"style", {
            {"normal", {{"bgColor", "0xd63031FF"}, {"textColor", "0xFFFFFFFF"}}},
            {"hover", {{"bgColor", "0xff7675FF"}, {"textColor", "0xFFFFFFFF"}}},
            {"pressed", {{"bgColor", "0xa62021FF"}, {"textColor", "0xFFFFFFFF"}}}
        }}
    });

    // === TEXTURED BUTTONS PANEL === (TRANSPARENT FOR TESTING)
    children.push_back({
        {"type", "panel"},
        {"id", "textured_buttons_panel"},
        {"x", 350}, {"y", 120},
        {"width", 300}, {"height", 280},
        {"style", {{"bgColor", "0x00000000"}}}  // TRANSPARENT!
    });

    children.push_back({
        {"type", "label"},
        {"id", "textured_buttons_title"},
        {"x", 365}, {"y", 130},
        {"width", 250}, {"height", 30},
        {"text", "Sprite Buttons"},
        {"style", {{"fontSize", 20}, {"color", "0xFFFFFFFF"}}}
    });

    // Textured Button 1 - Car (HUGE LIKE WORKING TEST!)
    children.push_back({
        {"type", "button"},
        {"id", "btn_car"},
        {"x", 50}, {"y", 120},
        {"width", 400}, {"height", 200},  // Same as test_button_with_png!
        {"text", ""},
        {"onClick", "sprite_car"},
        {"style", {
            {"normal", {{"textureId", 1}, {"bgColor", "0xFFFFFFFF"}, {"textColor", "0x000000FF"}}},
            {"hover", {{"textureId", 1}, {"bgColor", "0xFFFF00FF"}, {"textColor", "0x000000FF"}}},  // Yellow tint
            {"pressed", {{"textureId", 1}, {"bgColor", "0x888888FF"}, {"textColor", "0x000000FF"}}}  // Dark tint
        }}
    });

    // Textured Button 2 - Eyes (HUGE!)
    children.push_back({
        {"type", "button"},
        {"id", "btn_eyes"},
        {"x", 470}, {"y", 120},
        {"width", 250}, {"height", 200},  // Much bigger!
        {"text", ""},
        {"onClick", "sprite_eyes"},
        {"style", {
            {"normal", {{"textureId", 2}, {"bgColor", "0xFFFFFFFF"}, {"textColor", "0x000000FF"}}},
            {"hover", {{"textureId", 2}, {"bgColor", "0x00FFFFFF"}, {"textColor", "0x000000FF"}}},  // Cyan tint
            {"pressed", {{"textureId", 2}, {"bgColor", "0x888888FF"}, {"textColor", "0x000000FF"}}}
        }}
    });

    // Textured Button 3 - Icon (HUGE!)
    children.push_back({
        {"type", "button"},
        {"id", "btn_icon"},
        {"x", 50}, {"y", 340},
        {"width", 250}, {"height", 200},  // Much bigger!
        {"text", ""},
        {"onClick", "sprite_icon"},
        {"style", {
            {"normal", {{"textureId", 3}, {"bgColor", "0xFFFFFFFF"}, {"textColor", "0x000000FF"}}},
            {"hover", {{"textureId", 3}, {"bgColor", "0xFF00FFFF"}, {"textColor", "0x000000FF"}}},  // Magenta tint
            {"pressed", {{"textureId", 3}, {"bgColor", "0x888888FF"}, {"textColor", "0x000000FF"}}}
        }}
    });

    // Info label for textured buttons
    children.push_back({
        {"type", "label"},
        {"x", 370}, {"y", 340},
        {"width", 260}, {"height", 50},
        {"text", "Retained mode:\nTextures only sent once!"},
        {"style", {{"fontSize", 12}, {"color", "0xAAAAAAFF"}}}
    });

    // === MIDDLE COLUMN: Inputs Panel ===
    children.push_back({
        {"type", "panel"},
        {"id", "inputs_panel"},
        {"x", 362}, {"y", 120},
        {"width", 300}, {"height", 280},
        {"style", {{"bgColor", "0x2d3436FF"}}}
    });

    children.push_back({
        {"type", "label"},
        {"id", "inputs_title"},
        {"x", 377}, {"y", 130},
        {"width", 200}, {"height", 30},
        {"text", "Input Widgets"},
        {"style", {{"fontSize", 20}, {"color", "0xFFFFFFFF"}}}
    });

    // Checkboxes
    children.push_back({
        {"type", "checkbox"},
        {"id", "chk_option1"},
        {"x", 382}, {"y", 175},
        {"width", 200}, {"height", 24},
        {"checked", false},
        {"text", "Enable feature A"}
    });

    children.push_back({
        {"type", "checkbox"},
        {"id", "chk_option2"},
        {"x", 382}, {"y", 210},
        {"width", 200}, {"height", 24},
        {"checked", true},
        {"text", "Enable feature B"}
    });

    // Slider
    children.push_back({
        {"type", "label"},
        {"id", "slider_label"},
        {"x", 382}, {"y", 255},
        {"width", 200}, {"height", 20},
        {"text", "Volume: 50%"},
        {"style", {{"fontSize", 14}, {"color", "0xAAAAAAFF"}}}
    });

    children.push_back({
        {"type", "slider"},
        {"id", "volume_slider"},
        {"x", 382}, {"y", 280},
        {"width", 260}, {"height", 24},
        {"min", 0.0},
        {"max", 100.0},
        {"value", 50.0}
    });

    // Text input
    children.push_back({
        {"type", "label"},
        {"id", "textinput_label"},
        {"x", 382}, {"y", 320},
        {"width", 200}, {"height", 20},
        {"text", "Username:"},
        {"style", {{"fontSize", 14}, {"color", "0xAAAAAAFF"}}}
    });

    children.push_back({
        {"type", "textinput"},
        {"id", "username_input"},
        {"x", 382}, {"y", 345},
        {"width", 260}, {"height", 35},
        {"placeholder", "Enter username..."}
    });

    // === RIGHT COLUMN: Progress Panel ===
    children.push_back({
        {"type", "panel"},
        {"id", "progress_panel"},
        {"x", 694}, {"y", 120},
        {"width", 300}, {"height", 280},
        {"style", {{"bgColor", "0x2d3436FF"}}}
    });

    children.push_back({
        {"type", "label"},
        {"id", "progress_title"},
        {"x", 709}, {"y", 130},
        {"width", 200}, {"height", 30},
        {"text", "Progress Bars"},
        {"style", {{"fontSize", 20}, {"color", "0xFFFFFFFF"}}}
    });

    // Progress bars
    children.push_back({
        {"type", "label"},
        {"x", 714}, {"y", 170},
        {"width", 200}, {"height", 20},
        {"text", "Loading..."},
        {"style", {{"fontSize", 14}, {"color", "0xAAAAAAFF"}}}
    });

    children.push_back({
        {"type", "progressbar"},
        {"id", "progress_loading"},
        {"x", 714}, {"y", 195},
        {"width", 260}, {"height", 20},
        {"value", 0.35}
    });

    children.push_back({
        {"type", "label"},
        {"x", 714}, {"y", 230},
        {"width", 200}, {"height", 20},
        {"text", "Download: 75%"},
        {"style", {{"fontSize", 14}, {"color", "0xAAAAAAFF"}}}
    });

    children.push_back({
        {"type", "progressbar"},
        {"id", "progress_download"},
        {"x", 714}, {"y", 255},
        {"width", 260}, {"height", 20},
        {"value", 0.75}
    });

    children.push_back({
        {"type", "label"},
        {"x", 714}, {"y", 290},
        {"width", 200}, {"height", 20},
        {"text", "Health: 45%"},
        {"style", {{"fontSize", 14}, {"color", "0xAAAAAAFF"}}}
    });

    children.push_back({
        {"type", "progressbar"},
        {"id", "progress_health"},
        {"x", 714}, {"y", 315},
        {"width", 260}, {"height", 25},
        {"value", 0.45}
    });

    // === BOTTOM: Info Panel ===
    children.push_back({
        {"type", "panel"},
        {"id", "info_panel"},
        {"x", 30}, {"y", 420},
        {"width", 964}, {"height", 320},
        {"style", {{"bgColor", "0x2d3436FF"}}}
    });

    children.push_back({
        {"type", "label"},
        {"id", "info_title"},
        {"x", 45}, {"y", 430},
        {"width", 400}, {"height", 30},
        {"text", "UI Showcase - Click buttons and interact!"},
        {"style", {{"fontSize", 20}, {"color", "0xFFFFFFFF"}}}
    });

    children.push_back({
        {"type", "label"},
        {"x", 45}, {"y", 470},
        {"width", 900}, {"height", 250},
        {"text", "This showcase demonstrates UIModule widgets:\n- Panels (containers)\n- Labels (text)\n- Buttons (4 styles with hover/press states)\n- Checkboxes (toggles)\n- Sliders (value input)\n- Text Input (keyboard entry)\n- Progress Bars (3 examples)"},
        {"style", {{"fontSize", 16}, {"color", "0xAAAAAAFF"}}}
    });

    root["children"] = children;
    return root;
}

class UIShowcase {
public:
    UIShowcase() = default;

    bool init(SDL_Window* window) {
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        SDL_GetWindowWMInfo(window, &wmi);

        m_logger = spdlog::stdout_color_mt("UIShowcase");
        spdlog::set_level(spdlog::level::info);

        m_logger->info("=== UIModule Complete Showcase ===");

        // Create IIO instances - keep shared_ptr alive, use IIO* abstract interface
        // renderer: subscribes to render:* topics (from UI and game)
        // ui: subscribes to input:* topics, publishes ui:* events
        // input: publishes input:* topics
        // game: subscribes to ui:* events, can publish render:* directly
        m_rendererIOPtr = IntraIOManager::getInstance().createInstance("renderer");
        m_uiIOPtr = IntraIOManager::getInstance().createInstance("ui_module");
        m_inputIOPtr = IntraIOManager::getInstance().createInstance("input");
        m_gameIOPtr = IntraIOManager::getInstance().createInstance("game");
        m_rendererIO = m_rendererIOPtr.get();
        m_uiIO = m_uiIOPtr.get();
        m_inputIO = m_inputIOPtr.get();
        m_gameIO = m_gameIOPtr.get();

        // Create and configure BgfxRenderer with textures
        m_renderer = std::make_unique<BgfxRendererModule>();
        {
            JsonDataNode config("config");
            config.setDouble("nativeWindowHandle",
                static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
            config.setInt("windowWidth", 1024);
            config.setInt("windowHeight", 768);
            // config.setString("backend", "d3d11");  // LET BGFX CHOOSE LIKE test_button_with_png!
            config.setBool("vsync", true);
            // Load textures for sprite buttons
            config.setString("texture1", "../../assets/textures/5oxaxt1vo2f91.jpg");  // Car
            config.setString("texture2", "../../assets/textures/1f440.png");  // Eyes emoji
            config.setString("texture3", "../../assets/textures/IconDesigner.png");  // Icon
            m_renderer->setConfiguration(config, m_rendererIO, nullptr);
        }
        m_logger->info("✓ Loaded 3 textures for sprite buttons (IDs: 1, 2, 3)");

        // Create and configure UIModule with inline layout
        m_uiModule = std::make_unique<UIModule>();
        {
            nlohmann::json layoutJson = createUILayout();
            JsonDataNode config("config", layoutJson);
            config.setInt("windowWidth", 1024);
            config.setInt("windowHeight", 768);
            config.setInt("baseLayer", 1000);

            // Add layout as child
            auto layoutNode = std::make_unique<JsonDataNode>("layout", layoutJson);
            config.setChild("layout", std::move(layoutNode));

            m_uiModule->setConfiguration(config, m_uiIO, nullptr);
        }

        // Subscribe game to UI events
        m_gameIO->subscribe("ui:action");
        m_gameIO->subscribe("ui:click");
        m_gameIO->subscribe("ui:value_changed");
        m_gameIO->subscribe("ui:text_changed");
        m_gameIO->subscribe("ui:text_submit");
        m_gameIO->subscribe("ui:hover");

        m_logger->info("Modules initialized");
        m_logger->info("Controls: Mouse to interact, ESC to exit");

        return true;
    }

    void handleSDLEvent(SDL_Event& e) {
        // Convert SDL events to IIO input messages
        // IMPORTANT: Publish from m_gameIO (not m_inputIO or m_uiIO) because IIO doesn't deliver to self
        if (e.type == SDL_MOUSEMOTION) {
            auto msg = std::make_unique<JsonDataNode>("mouse");
            msg->setDouble("x", static_cast<double>(e.motion.x));
            msg->setDouble("y", static_cast<double>(e.motion.y));
            m_gameIO->publish("input:mouse:move", std::move(msg));
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            auto msg = std::make_unique<JsonDataNode>("mouse");
            msg->setInt("button", e.button.button - 1);  // SDL uses 1-based
            msg->setBool("pressed", e.type == SDL_MOUSEBUTTONDOWN);
            msg->setDouble("x", static_cast<double>(e.button.x));
            msg->setDouble("y", static_cast<double>(e.button.y));
            m_gameIO->publish("input:mouse:button", std::move(msg));
        }
        else if (e.type == SDL_MOUSEWHEEL) {
            auto msg = std::make_unique<JsonDataNode>("wheel");
            msg->setDouble("delta", static_cast<double>(e.wheel.y));
            m_gameIO->publish("input:mouse:wheel", std::move(msg));
        }
        else if (e.type == SDL_KEYDOWN) {
            // Only publish special keys (non-printable), printable chars come from SDL_TEXTINPUT
            int keyCode = e.key.keysym.sym;
            bool isSpecialKey = (keyCode == SDLK_BACKSPACE || keyCode == SDLK_DELETE ||
                                keyCode == SDLK_RETURN || keyCode == SDLK_LEFT ||
                                keyCode == SDLK_RIGHT || keyCode == SDLK_HOME ||
                                keyCode == SDLK_END || keyCode == SDLK_UP ||
                                keyCode == SDLK_DOWN || keyCode == SDLK_TAB);

            if (isSpecialKey) {
                auto msg = std::make_unique<JsonDataNode>("key");
                msg->setInt("keyCode", keyCode);
                msg->setBool("pressed", true);
                msg->setInt("char", 0);
                m_gameIO->publish("input:keyboard", std::move(msg));
            }
        }
        else if (e.type == SDL_TEXTINPUT) {
            // Printable characters come here
            auto msg = std::make_unique<JsonDataNode>("key");
            msg->setInt("keyCode", 0);
            msg->setBool("pressed", true);
            msg->setInt("char", static_cast<int>(e.text.text[0]));
            m_gameIO->publish("input:keyboard", std::move(msg));
        }
    }

    void update(float dt) {
        m_time += dt;
        m_frameCount++;

        // Process UI events from game perspective
        processUIEvents();

        // Animate progress bars
        animateProgressBars(dt);
    }

    void render() {
        // Set camera for full window
        {
            auto cam = std::make_unique<JsonDataNode>("camera");
            cam->setDouble("x", 0.0);
            cam->setDouble("y", 0.0);
            cam->setDouble("zoom", 1.0);
            cam->setInt("viewportX", 0);
            cam->setInt("viewportY", 0);
            cam->setInt("viewportW", 1024);
            cam->setInt("viewportH", 768);
            m_gameIO->publish("render:camera", std::move(cam));
        }

        // Process modules in order: UI first (to handle input and prepare render commands)
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        input.setInt("frameCount", m_frameCount);

        m_uiModule->process(input);
        m_renderer->process(input);
    }

    void shutdown() {
        m_uiModule->shutdown();
        m_renderer->shutdown();

        IntraIOManager::getInstance().removeInstance("renderer");
        IntraIOManager::getInstance().removeInstance("ui_module");
        IntraIOManager::getInstance().removeInstance("input");
        IntraIOManager::getInstance().removeInstance("game");

        m_logger->info("UIShowcase shutdown complete");
    }

    int getFrameCount() const { return m_frameCount; }

private:
    void processUIEvents() {
        while (m_gameIO->hasMessages() > 0) {
            auto msg = m_gameIO->pullMessage();

            std::string logEntry;

            if (msg.topic == "ui:action") {
                std::string action = msg.data->getString("action", "");
                std::string widgetId = msg.data->getString("widgetId", "");
                logEntry = "Action: " + action + " (" + widgetId + ")";

                // Handle specific actions
                if (action == "action_primary") {
                    m_logger->info("Primary button clicked!");
                }
                else if (action == "action_danger") {
                    m_logger->warn("Danger button clicked!");
                }
                else if (action == "sprite_car") {
                    m_logger->info("🚗 Car sprite button clicked! (Texture ID: 1)");
                }
                else if (action == "sprite_eyes") {
                    m_logger->info("👀 Eyes sprite button clicked! (Texture ID: 2)");
                }
                else if (action == "sprite_icon") {
                    m_logger->info("🎨 Icon sprite button clicked! (Texture ID: 3)");
                }
            }
            else if (msg.topic == "ui:click") {
                std::string widgetId = msg.data->getString("widgetId", "");
                double x = msg.data->getDouble("x", 0);
                double y = msg.data->getDouble("y", 0);
                logEntry = "Click: " + widgetId + " at (" +
                           std::to_string(static_cast<int>(x)) + "," +
                           std::to_string(static_cast<int>(y)) + ")";
            }
            else if (msg.topic == "ui:value_changed") {
                std::string widgetId = msg.data->getString("widgetId", "");
                if (widgetId == "volume_slider") {
                    double value = msg.data->getDouble("value", 0);
                    logEntry = "Volume: " + std::to_string(static_cast<int>(value)) + "%";
                }
                else if (widgetId.find("chk_") == 0) {
                    bool checked = msg.data->getBool("checked", false);
                    logEntry = widgetId + " = " + (checked ? "ON" : "OFF");
                }
            }
            else if (msg.topic == "ui:text_submit") {
                std::string widgetId = msg.data->getString("widgetId", "");
                std::string text = msg.data->getString("text", "");
                logEntry = "Submit: " + widgetId + " = \"" + text + "\"";
            }
            else if (msg.topic == "ui:hover") {
                std::string widgetId = msg.data->getString("widgetId", "");
                bool enter = msg.data->getBool("enter", false);
                if (enter && !widgetId.empty()) {
                    logEntry = "Hover: " + widgetId;
                }
            }

            // Add to log if we have an entry
            if (!logEntry.empty()) {
                addLogEntry(logEntry);
            }
        }
    }

    void addLogEntry(const std::string& entry) {
        // Shift log entries up
        for (int i = 7; i > 0; --i) {
            m_logEntries[i] = m_logEntries[i - 1];
        }
        m_logEntries[0] = entry;

        m_logger->info("UI Event: {}", entry);
    }

    void animateProgressBars(float dt) {
        // Animate loading progress (loops)
        m_loadingProgress += dt * 0.3f;
        if (m_loadingProgress > 1.0f) {
            m_loadingProgress = 0.0f;
        }

        // We would update progress bars here if we had a way to modify widgets
        // For now, this is just simulation - the actual values are set in layout
    }

    std::shared_ptr<spdlog::logger> m_logger;
    std::unique_ptr<BgfxRendererModule> m_renderer;
    std::unique_ptr<UIModule> m_uiModule;
    // Keep shared_ptr alive
    std::shared_ptr<IntraIO> m_rendererIOPtr;
    std::shared_ptr<IntraIO> m_uiIOPtr;
    std::shared_ptr<IntraIO> m_inputIOPtr;
    std::shared_ptr<IntraIO> m_gameIOPtr;
    // Abstract IIO* interface
    IIO* m_rendererIO = nullptr;
    IIO* m_uiIO = nullptr;
    IIO* m_inputIO = nullptr;
    IIO* m_gameIO = nullptr;

    float m_time = 0.0f;
    int m_frameCount = 0;

    // Progress animation
    float m_loadingProgress = 0.0f;

    // Event log
    std::string m_logEntries[8];
};

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Enable text input for text fields
    SDL_StartTextInput();

    SDL_Window* window = SDL_CreateWindow(
        "UIModule Showcase",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    UIShowcase showcase;
    if (!showcase.init(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    Uint64 lastTime = SDL_GetPerformanceCounter();

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
            showcase.handleSDLEvent(e);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - lastTime) / SDL_GetPerformanceFrequency();
        lastTime = now;

        showcase.update(dt);
        showcase.render();

        SDL_Delay(1);
    }

    SDL_StopTextInput();

    int frames = showcase.getFrameCount();
    showcase.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Rendered " << frames << " frames" << std::endl;
    return 0;
}
