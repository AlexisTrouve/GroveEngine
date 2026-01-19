/**
 * Test: UIButton avec texture PNG chargée depuis un fichier
 * Ce test montre qu'on peut mettre une VRAIE image sur un bouton
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <iostream>
#include <memory>
#include <fstream>

#include "BgfxRendererModule.h"
#include "UIModule/UIModule.h"
#include "../modules/BgfxRenderer/Resources/ResourceCache.h"
#include "../modules/BgfxRenderer/RHI/RHITypes.h"
#include "../modules/BgfxRenderer/RHI/RHIDevice.h"
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <bgfx/bgfx.h>
#include <vector>

using namespace grove;

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    auto logger = spdlog::stdout_color_mt("TexturedButtonTest");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed" << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Textured Button Test - Gradient",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600, SDL_WINDOW_SHOWN
    );

    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    SDL_GetWindowWMInfo(window, &wmi);

    // Create IIO instances - IMPORTANT: game publishes input, ui subscribes and publishes render commands
    auto gameIO = IntraIOManager::getInstance().createInstance("game");
    auto uiIO = IntraIOManager::getInstance().createInstance("ui");
    auto rendererIO = IntraIOManager::getInstance().createInstance("renderer");

    gameIO->subscribe("ui:hover", [](const Message& msg) {
        // Hover events (not logged to avoid spam)
    });
    gameIO->subscribe("ui:click", [&logger](const Message& msg) {
        logger->info("🖱️  BOUTON CLICKÉ!");
    });
    gameIO->subscribe("ui:action", [&logger](const Message& msg) {
        logger->info("🖱️  ACTION!");
    });

    // Initialize BgfxRenderer WITH 3 TEXTURES loaded via config
    auto renderer = std::make_unique<BgfxRendererModule>();
    {
        JsonDataNode config("config");
        config.setDouble("nativeWindowHandle",
            static_cast<double>(reinterpret_cast<uintptr_t>(wmi.info.win.window)));
        config.setInt("windowWidth", 800);
        config.setInt("windowHeight", 600);
        // Load 3 textures
        config.setString("texture1", "../../assets/textures/5oxaxt1vo2f91.jpg");  // Car
        config.setString("texture2", "../../assets/textures/1f440.png");  // Eyes
        config.setString("texture3", "../../assets/textures/IconDesigner.png");  // Icon

        renderer->setConfiguration(config, rendererIO.get(), nullptr);
    }

    logger->info("✓ Loaded 3 textures (IDs: 1, 2, 3)");

    // Initialize UIModule with 3 TEXTURED BUTTONS
    auto ui = std::make_unique<UIModule>();
    {
        JsonDataNode config("config");
        config.setInt("windowWidth", 800);
        config.setInt("windowHeight", 600);

        nlohmann::json layoutJson = {
            {"id", "root"},
            {"type", "panel"},
            {"x", 0}, {"y", 0},
            {"width", 800}, {"height", 600},  // Full screen invisible panel (just container)
            {"style", {
                {"bgColor", "0x00000000"}  // Fully transparent - just a container
            }},
            {"children", {
                {
                    {"id", "btn_car"},
                    {"type", "button"},
                    {"x", 50},
                    {"y", 50},
                    {"width", 400},
                    {"height", 200},
                    {"text", ""},
                    {"onClick", "car_action"},
                    {"style", {
                        {"normal", {{"textureId", 1}, {"bgColor", "0xFFFFFFFF"}}},
                        {"hover", {{"textureId", 1}, {"bgColor", "0xFFFF00FF"}}},
                        {"pressed", {{"textureId", 1}, {"bgColor", "0x888888FF"}}}
                    }}
                },
                {
                    {"id", "btn_eyes"},
                    {"type", "button"},
                    {"x", 50},
                    {"y", 270},
                    {"width", 250},
                    {"height", 200},
                    {"text", ""},
                    {"onClick", "eyes_action"},
                    {"style", {
                        {"normal", {{"textureId", 2}, {"bgColor", "0xFFFFFFFF"}}},
                        {"hover", {{"textureId", 2}, {"bgColor", "0x00FFFFFF"}}},
                        {"pressed", {{"textureId", 2}, {"bgColor", "0x888888FF"}}}
                    }}
                },
                {
                    {"id", "btn_icon"},
                    {"type", "button"},
                    {"x", 320},
                    {"y", 270},
                    {"width", 250},
                    {"height", 200},
                    {"text", ""},
                    {"onClick", "icon_action"},
                    {"style", {
                        {"normal", {{"textureId", 3}, {"bgColor", "0xFFFFFFFF"}}},
                        {"hover", {{"textureId", 3}, {"bgColor", "0xFF00FFFF"}}},
                        {"pressed", {{"textureId", 3}, {"bgColor", "0x888888FF"}}}
                    }}
                }
            }}
        };

        auto layoutNode = std::make_unique<JsonDataNode>("layout", layoutJson);
        config.setChild("layout", std::move(layoutNode));

        ui->setConfiguration(config, uiIO.get(), nullptr);
        logger->info("✓ UIModule configured with 3 textured buttons!");
    }

    logger->info("\n╔════════════════════════════════════════╗");
    logger->info("║  3 BOUTONS AVEC TEXTURES               ║");
    logger->info("╠════════════════════════════════════════╣");
    logger->info("║  Button 1: Car (textureId=1)           ║");
    logger->info("║  Button 2: Eyes (textureId=2)          ║");
    logger->info("║  Button 3: Icon (textureId=3)          ║");
    logger->info("║  Press ESC to exit                     ║");
    logger->info("╚════════════════════════════════════════╝\n");

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }

            // Forward mouse events
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
            }
        }

        // Dispatch UI events (callbacks handle logging)
        while (gameIO->hasMessages() > 0) {
            gameIO->pullAndDispatch();
        }

        // Update modules
        JsonDataNode input("input");
        input.setDouble("deltaTime", 0.016);
        ui->process(input);
        renderer->process(input);

        SDL_Delay(16);
    }

    logger->info("Cleaning up...");

    // Textures are managed by ResourceCache, will be cleaned up in renderer->shutdown()
    ui->shutdown();
    renderer->shutdown();

    IntraIOManager::getInstance().removeInstance("game");
    IntraIOManager::getInstance().removeInstance("ui");
    IntraIOManager::getInstance().removeInstance("renderer");

    SDL_DestroyWindow(window);
    SDL_Quit();

    logger->info("Test complete!");
    return 0;
}
