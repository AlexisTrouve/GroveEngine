/**
 * Test: Single UIButton with interaction
 * - Bouton rouge
 * - Feedback visuel hover (orange)
 * - Feedback visuel pressed (rouge foncé)
 * - Logs des événements
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <memory>

#include "BgfxRendererModule.h"
#include "UIModule/UIModule.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace grove;

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    auto logger = spdlog::stdout_color_mt("ButtonTest");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed" << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Single Button Test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600, SDL_WINDOW_SHOWN
    );

    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    SDL_GetWindowWMInfo(window, &wmi);

    // Create IIO instances
    auto rendererIO = IntraIOManager::getInstance().createInstance("renderer");
    auto uiIO = IntraIOManager::getInstance().createInstance("ui");
    auto gameIO = IntraIOManager::getInstance().createInstance("game");

    // Subscribe to UI events for logging
    gameIO->subscribe("ui:hover");
    gameIO->subscribe("ui:click");
    gameIO->subscribe("ui:action");

    // Initialize BgfxRenderer
    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode config("config");
        config.setDouble("nativeWindowHandle",
            static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        config.setInt("windowWidth", 800);
        config.setInt("windowHeight", 600);
        renderer->setConfiguration(config, rendererIO.get(), nullptr);
    }

    // Initialize UIModule with ONE button - proper style structure
    auto ui = std::make_unique<UIModule>();
    {
        JsonDataNode config("config");
        config.setInt("windowWidth", 800);
        config.setInt("windowHeight", 600);

        /*
         * COMMENT CA MARCHE:
         *
         * 1. UIModule charge le layout JSON et crée les widgets
         * 2. Chaque frame:
         *    - UIModule reçoit les events input (mouse, keyboard) via IIO
         *    - UIModule update les widgets (hover detection, click handling)
         *    - UIModule appelle render() sur chaque widget
         *    - UIButton::render() publie un sprite via IIO topic "render:sprite"
         *    - BgfxRenderer reçoit le sprite et le dessine
         *
         * 3. Les styles du bouton:
         *    - normal: état par défaut
         *    - hover: quand la souris est dessus
         *    - pressed: quand on clique
         *    - disabled: quand enabled=false
         *
         * 4. Les événements publiés par UIModule:
         *    - ui:hover - quand on entre/sort d'un widget
         *    - ui:click - quand on clique
         *    - ui:action - quand un bouton avec onClick est cliqué
         */

        nlohmann::json layoutJson = {
            {"id", "root"},
            {"type", "panel"},
            {"x", 0}, {"y", 0},
            {"width", 800}, {"height", 600},
            {"style", {
                {"bgColor", "0x1A1A2EFF"}  // Fond sombre
            }},
            {"children", {
                {
                    {"id", "btn_test"},
                    {"type", "button"},
                    {"x", 250},
                    {"y", 250},
                    {"width", 300},
                    {"height", 100},
                    {"text", "CLICK ME!"},
                    {"onClick", "test_action"},
                    {"style", {
                        {"fontSize", 24.0},
                        {"normal", {
                            {"bgColor", "0xE74C3CFF"},     // Rouge
                            {"textColor", "0xFFFFFFFF"},   // Blanc
                            {"borderRadius", 8.0}
                        }},
                        {"hover", {
                            {"bgColor", "0xF39C12FF"},     // Orange (hover)
                            {"textColor", "0xFFFFFFFF"},
                            {"borderRadius", 8.0}
                        }},
                        {"pressed", {
                            {"bgColor", "0xC0392BFF"},     // Rouge foncé (pressed)
                            {"textColor", "0xFFFFFFFF"},
                            {"borderRadius", 8.0}
                        }}
                    }}
                }
            }}
        };

        auto layoutNode = std::make_unique<JsonDataNode>("layout", layoutJson);
        config.setChild("layout", std::move(layoutNode));

        ui->setConfiguration(config, uiIO.get(), nullptr);
    }

    logger->info("=== SINGLE BUTTON TEST ===");
    logger->info("- Bouton ROUGE au centre");
    logger->info("- Hover = ORANGE");
    logger->info("- Click = ROUGE FONCE");
    logger->info("- Les events sont loggés ci-dessous");
    logger->info("Press ESC to exit\n");

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            // Forward mouse events to UI via IIO
            // IMPORTANT: Publish from gameIO (not uiIO) because IIO doesn't deliver to self
            if (e.type == SDL_MOUSEMOTION) {
                auto mouseMsg = std::make_unique<JsonDataNode>("mouse");
                mouseMsg->setDouble("x", static_cast<double>(e.motion.x));
                mouseMsg->setDouble("y", static_cast<double>(e.motion.y));
                gameIO->publish("input:mouse:move", std::move(mouseMsg));
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                auto mouseMsg = std::make_unique<JsonDataNode>("mouse");
                mouseMsg->setInt("button", e.button.button);
                mouseMsg->setBool("pressed", e.type == SDL_MOUSEBUTTONDOWN);
                mouseMsg->setDouble("x", static_cast<double>(e.button.x));
                mouseMsg->setDouble("y", static_cast<double>(e.button.y));
                gameIO->publish("input:mouse:button", std::move(mouseMsg));

                // Log press/release
                logger->info("[INPUT] Mouse {} at ({}, {})",
                    e.type == SDL_MOUSEBUTTONDOWN ? "PRESSED" : "RELEASED",
                    e.button.x, e.button.y);
            }
        }

        // Check for UI events
        while (gameIO->hasMessages() > 0) {
            auto msg = gameIO->pullMessage();

            if (msg.topic == "ui:hover") {
                std::string widgetId = msg.data->getString("widgetId", "");
                bool enter = msg.data->getBool("enter", false);
                logger->info("[UI EVENT] HOVER {} widget '{}'",
                    enter ? "ENTER" : "LEAVE", widgetId);
            }
            else if (msg.topic == "ui:click") {
                std::string widgetId = msg.data->getString("widgetId", "");
                logger->info("[UI EVENT] CLICK on widget '{}'", widgetId);
            }
            else if (msg.topic == "ui:action") {
                std::string action = msg.data->getString("action", "");
                std::string widgetId = msg.data->getString("widgetId", "");
                logger->info("[UI EVENT] ACTION '{}' from widget '{}'", action, widgetId);
            }
        }

        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);

        // Process UI (publishes sprites)
        ui->process(input);

        // Render
        renderer->process(input);

        SDL_Delay(16);
    }

    ui->shutdown();
    renderer->shutdown();
    IntraIOManager::getInstance().removeInstance("renderer");
    IntraIOManager::getInstance().removeInstance("ui");
    IntraIOManager::getInstance().removeInstance("game");

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
