/**
 * Progressive Test - Test each component step by step
 * To find where test_full_stack_interactive crashes
 */

#include <SDL.h>
#include <fstream>
#include <iostream>
#include <string>

// Test GroveEngine components progressively
#define TEST_SPDLOG 1
#define TEST_IIO 1
#define TEST_MODULE_LOAD 0  // DISABLED for debugging

#if TEST_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#endif

#if TEST_IIO
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#endif

#if TEST_MODULE_LOAD
#include <grove/ModuleLoader.h>
#include <grove/JsonDataNode.h>
#endif

#undef main

void writeLog(std::ofstream& log, const std::string& msg) {
    log << msg << std::endl;
    log.flush();
    std::cout << msg << std::endl;
}

int main(int argc, char* argv[]) {
    std::ofstream log("progressive_test.log");

    writeLog(log, "=== Progressive Component Test ===");
    writeLog(log, "Step 1: Basic C++ works");

    // Test 1: SDL
    writeLog(log, "Step 2: Testing SDL...");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        writeLog(log, "ERROR: SDL_Init failed");
        return 1;
    }
    writeLog(log, "  -> SDL_Init: OK");

    SDL_Window* window = SDL_CreateWindow("Test", 100, 100, 800, 600, SDL_WINDOW_HIDDEN);
    if (!window) {
        writeLog(log, "ERROR: SDL_CreateWindow failed");
        SDL_Quit();
        return 1;
    }
    writeLog(log, "  -> SDL_CreateWindow: OK");

#if TEST_SPDLOG
    // Test 2: spdlog
    writeLog(log, "Step 3: Testing spdlog...");
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("spdlog_test.log", true);

        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("TestLogger", sinks.begin(), sinks.end());

        logger->info("spdlog test message");
        writeLog(log, "  -> spdlog: OK");
    } catch (const std::exception& e) {
        writeLog(log, std::string("ERROR: spdlog failed: ") + e.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif

#if TEST_IIO
    // Test 3: IIO
    writeLog(log, "Step 4: Testing IntraIOManager...");
    try {
        auto& ioManager = grove::IntraIOManager::getInstance();
        auto testIO = ioManager.createInstance("test");

        writeLog(log, "  -> IntraIOManager: OK");

        // Test pub/sub
        testIO->subscribe("test:topic", [](const grove::Message&) {});
        auto msg = std::make_unique<grove::JsonDataNode>("msg");
        msg->setString("data", "test");
        testIO->publish("test:topic", std::move(msg));

        if (testIO->hasMessages() > 0) {
            writeLog(log, "  -> IIO pub/sub: OK");
        } else {
            writeLog(log, "ERROR: IIO pub/sub failed");
        }

        ioManager.removeInstance("test");
    } catch (const std::exception& e) {
        writeLog(log, std::string("ERROR: IIO failed: ") + e.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif

#if TEST_MODULE_LOAD
    // Test 4: Module loading
    writeLog(log, "Step 5: Testing module loading...");

    // Try to load InputModule (smallest)
    try {
        auto& ioManager = grove::IntraIOManager::getInstance();
        auto moduleIO = ioManager.createInstance("module_test");

        grove::ModuleLoader loader;
        std::string modulePath = "./modules/InputModule.dll";

        writeLog(log, "  -> Attempting to load: " + modulePath);

        auto module = loader.load(modulePath, "input_test");

        if (module) {
            writeLog(log, "  -> Module loaded: OK");

            // Try configuration
            grove::JsonDataNode config("config");
            config.setString("backend", "sdl");

            module->setConfiguration(config, moduleIO.get(), nullptr);
            writeLog(log, "  -> Module configured: OK");

            // Cleanup
            module->shutdown();
            writeLog(log, "  -> Module shutdown: OK");
        } else {
            writeLog(log, "ERROR: Module is nullptr");
        }

        ioManager.removeInstance("module_test");
    } catch (const std::exception& e) {
        writeLog(log, std::string("ERROR: Module loading failed: ") + e.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif

    writeLog(log, "");
    writeLog(log, "=== ALL TESTS PASSED ===");
    writeLog(log, "If you see this, all components work individually!");

    SDL_DestroyWindow(window);
    SDL_Quit();

    log.close();

    std::cout << "\nSuccess! Check progressive_test.log for details.\n";
    std::cout << "Press Enter to exit...\n";
    std::cin.get();

    return 0;
}
