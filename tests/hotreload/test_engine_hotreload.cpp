#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <signal.h>
#include <grove/DebugEngine.h>
#include <grove/EngineFactory.h>
#include "FileWatcher.h"

using namespace grove;

// Global flag for clean shutdown
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    std::cout << "\n🛑 Received signal " << signal << " - shutting down gracefully..." << std::endl;
    g_running.store(false);
}

int main(int argc, char** argv) {
    std::cout << "======================================" << std::endl;
    std::cout << "🏭 GROVE ENGINE HOT-RELOAD TEST" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "This test demonstrates:" << std::endl;
    std::cout << "  1. Real DebugEngine with SequentialModuleSystem" << std::endl;
    std::cout << "  2. Automatic .so file change detection" << std::endl;
    std::cout << "  3. Zero-downtime hot-reload" << std::endl;
    std::cout << "  4. State preservation across reloads" << std::endl;
    std::cout << "======================================\n" << std::endl;

    // Install signal handler for clean shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        // Configuration
        std::string modulePath = argc > 1 ? argv[1] : "./libTestModule.so";
        std::string moduleName = "TestModule";
        float targetFPS = 60.0f;
        float frameTime = 1.0f / targetFPS;

        std::cout << "📋 Configuration:" << std::endl;
        std::cout << "  Module path: " << modulePath << std::endl;
        std::cout << "  Target FPS: " << targetFPS << std::endl;
        std::cout << "  Hot-reload: ENABLED\n" << std::endl;

        // Step 1: Create DebugEngine
        std::cout << "🏗️ Step 1/4: Creating DebugEngine..." << std::endl;
        auto engine = EngineFactory::createEngine(EngineType::DEBUG);
        auto* debugEngine = dynamic_cast<DebugEngine*>(engine.get());

        if (!debugEngine) {
            std::cerr << "❌ Failed to cast to DebugEngine" << std::endl;
            return 1;
        }

        std::cout << "✅ DebugEngine created\n" << std::endl;

        // Step 2: Initialize engine
        std::cout << "🚀 Step 2/4: Initializing engine..." << std::endl;
        engine->initialize();
        std::cout << "✅ Engine initialized\n" << std::endl;

        // Step 3: Register module with hot-reload support
        std::cout << "📦 Step 3/4: Registering module from .so file..." << std::endl;
        debugEngine->registerModuleFromFile(moduleName, modulePath, ModuleSystemType::SEQUENTIAL);
        std::cout << "✅ Module registered\n" << std::endl;

        // Step 4: Setup file watcher for hot-reload
        std::cout << "👁️ Step 4/4: Setting up file watcher..." << std::endl;
        FileWatcher watcher;
        watcher.watch(modulePath);
        std::cout << "✅ Watching for changes to: " << modulePath << "\n" << std::endl;

        std::cout << "======================================" << std::endl;
        std::cout << "🏃 ENGINE RUNNING" << std::endl;
        std::cout << "======================================" << std::endl;
        std::cout << "Instructions:" << std::endl;
        std::cout << "  - Recompile TestModule.cpp to trigger hot-reload" << std::endl;
        std::cout << "  - Press Ctrl+C to exit" << std::endl;
        std::cout << "  - Watch for state preservation!\n" << std::endl;

        // Main loop
        int frameCount = 0;
        auto startTime = std::chrono::high_resolution_clock::now();
        auto lastStatusTime = startTime;

        while (g_running.load()) {
            auto frameStart = std::chrono::high_resolution_clock::now();

            // Check for module file changes
            if (watcher.hasChanged(modulePath)) {
                std::cout << "\n🔥 DETECTED CHANGE in " << modulePath << std::endl;
                std::cout << "🔄 Triggering hot-reload..." << std::endl;

                try {
                    auto reloadStart = std::chrono::high_resolution_clock::now();

                    debugEngine->reloadModule(moduleName);

                    auto reloadEnd = std::chrono::high_resolution_clock::now();
                    float reloadTime = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();

                    std::cout << "✅ Hot-reload completed in " << reloadTime << "ms" << std::endl;
                    std::cout << "📊 State should be preserved - check counter continues!\n" << std::endl;

                    // Reset watcher to avoid re-detecting the same change
                    watcher.reset(modulePath);

                } catch (const std::exception& e) {
                    std::cerr << "❌ Hot-reload failed: " << e.what() << std::endl;
                }
            }

            // Process one engine frame
            engine->step(frameTime);

            frameCount++;

            // Print status every 2 seconds
            auto currentTime = std::chrono::high_resolution_clock::now();
            float elapsedSinceStatus = std::chrono::duration<float>(currentTime - lastStatusTime).count();

            if (elapsedSinceStatus >= 2.0f) {
                float totalElapsed = std::chrono::duration<float>(currentTime - startTime).count();
                float actualFPS = frameCount / totalElapsed;

                std::cout << "📊 Status: Frame " << frameCount
                         << " | Runtime: " << static_cast<int>(totalElapsed) << "s"
                         << " | FPS: " << static_cast<int>(actualFPS) << std::endl;

                // Dump module state every 2 seconds
                std::cout << "\n📊 Dumping module state:\n" << std::endl;
                debugEngine->dumpModuleState(moduleName);
                std::cout << std::endl;

                lastStatusTime = currentTime;
            }

            // Frame rate limiting
            auto frameEnd = std::chrono::high_resolution_clock::now();
            float frameDuration = std::chrono::duration<float>(frameEnd - frameStart).count();

            if (frameDuration < frameTime) {
                std::this_thread::sleep_for(
                    std::chrono::duration<float>(frameTime - frameDuration)
                );
            }
        }

        // Shutdown
        std::cout << "\n======================================" << std::endl;
        std::cout << "🛑 SHUTTING DOWN" << std::endl;
        std::cout << "======================================" << std::endl;

        auto endTime = std::chrono::high_resolution_clock::now();
        float totalRuntime = std::chrono::duration<float>(endTime - startTime).count();

        std::cout << "📊 Final Statistics:" << std::endl;
        std::cout << "  Total frames: " << frameCount << std::endl;
        std::cout << "  Total runtime: " << totalRuntime << "s" << std::endl;
        std::cout << "  Average FPS: " << (frameCount / totalRuntime) << std::endl;

        engine->shutdown();
        std::cout << "\n✅ Engine shut down cleanly" << std::endl;

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
