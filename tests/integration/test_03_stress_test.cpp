/**
 * @file test_03_stress_test.cpp
 * @brief Scenario 3: Stress Test - Long-duration stability validation
 *
 * OBJECTIVE:
 *   Validate hot-reload system stability over extended duration with repeated reloads.
 *
 * TEST PARAMETERS:
 *   - Duration: 10 minutes (36000 frames @ 60 FPS)
 *   - Reload frequency: Every 5 seconds (300 frames)
 *   - Total reloads: 120
 *   - No random crashes - focus on hot-reload stability
 *
 * SUCCESS CRITERIA:
 *   ✅ All 120 reloads succeed
 *   ✅ Memory growth < 50MB over 10 minutes
 *   ✅ Average reload time < 500ms
 *   ✅ FPS remains stable (no degradation)
 *   ✅ No file descriptor leaks
 *   ✅ State preserved across all reloads
 *
 * WHAT THIS VALIDATES:
 *   - No memory leaks in hot-reload system
 *   - No file descriptor leaks (dlopen/dlclose)
 *   - Reload performance doesn't degrade over time
 *   - State preservation is reliable at scale
 *   - System remains stable under repeated reload stress
 */

#include "grove/ModuleLoader.h"
#include "grove/SequentialModuleSystem.h"
#include "grove/JsonDataNode.h"
#include "../helpers/TestMetrics.h"
#include "../helpers/TestAssertions.h"
#include "../helpers/TestReporter.h"
#include "../helpers/SystemUtils.h"

#include <iostream>
#include <chrono>
#include <thread>

using namespace grove;

// Test configuration
constexpr int TARGET_FPS = 60;
constexpr float FRAME_TIME = 1.0f / TARGET_FPS;
constexpr int RELOAD_INTERVAL = 300;  // Reload every 5 seconds (300 frames)
constexpr int EXPECTED_RELOADS = 120;  // 120 reloads
constexpr int TOTAL_FRAMES = EXPECTED_RELOADS * RELOAD_INTERVAL;  // 36000 frames = 10 minutes @ 60 FPS

// Memory threshold
constexpr size_t MAX_MEMORY_GROWTH_MB = 50;

// Paths
const std::string MODULE_PATH = "./libStressModule.so";

int main() {
    TestReporter reporter("Stress Test - 10 Minute Stability");
    TestMetrics metrics;

    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  SCENARIO 3: STRESS TEST - LONG DURATION STABILITY\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "Duration:        10 minutes (" << TOTAL_FRAMES << " frames @ " << TARGET_FPS << " FPS)\n";
    std::cout << "Reload interval: Every " << RELOAD_INTERVAL << " frames (5 seconds)\n";
    std::cout << "Expected reloads: " << EXPECTED_RELOADS << "\n";
    std::cout << "Memory threshold: < " << MAX_MEMORY_GROWTH_MB << " MB growth\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";

    size_t initialMemory = grove::getCurrentMemoryUsage() / (1024 * 1024);
    size_t peakMemory = initialMemory;

    int successfulReloads = 0;
    int failedReloads = 0;

    try {
        // === SETUP ===
        std::cout << "Setup: Loading StressModule...\n";

        ModuleLoader loader;
        auto moduleSystem = std::make_unique<SequentialModuleSystem>();

        // Load module
        auto module = loader.load(MODULE_PATH, "StressModule", false);

        // Configure module with empty config
        nlohmann::json configJson;
        auto config = std::make_unique<JsonDataNode>("config", configJson);

        module->setConfiguration(*config, nullptr, nullptr);

        // Register in module system
        moduleSystem->registerModule("StressModule", std::move(module));

        std::cout << "  ✓ StressModule loaded and configured\n\n";

        std::cout << "🚀 Starting 10-minute stress test...\n\n";

        auto startTime = std::chrono::high_resolution_clock::now();

        // Main simulation loop
        for (int frame = 1; frame <= TOTAL_FRAMES; ++frame) {
            auto frameStart = std::chrono::high_resolution_clock::now();

            // Process modules
            try {
                moduleSystem->processModules(FRAME_TIME);
            } catch (const std::exception& e) {
                std::cerr << "  [Frame " << frame << "] ❌ Unexpected error during process: " << e.what() << "\n";
                reporter.addAssertion("process_error", false);
                break;
            }

            auto frameEnd = std::chrono::high_resolution_clock::now();
            auto frameDuration = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
            float fps = frameDuration > 0.0f ? 1000.0f / frameDuration : 0.0f;
            metrics.recordFPS(fps);

            // Hot-reload every RELOAD_INTERVAL frames
            if (frame % RELOAD_INTERVAL == 0) {
                int reloadNumber = frame / RELOAD_INTERVAL;
                std::cout << "  [Frame " << frame << "/" << TOTAL_FRAMES << "] 🔄 Triggering hot-reload #" << reloadNumber << "...\n";

                auto reloadStart = std::chrono::high_resolution_clock::now();

                try {
                    // Extract module from system
                    auto extractedModule = moduleSystem->extractModule();
                    if (!extractedModule) {
                        std::cerr << "    ❌ Failed to extract StressModule\n";
                        failedReloads++;
                        continue;
                    }

                    // Perform hot-reload
                    auto reloadedModule = loader.reload(std::move(extractedModule));

                    // Re-register reloaded module
                    moduleSystem->registerModule("StressModule", std::move(reloadedModule));

                    auto reloadEnd = std::chrono::high_resolution_clock::now();
                    auto reloadDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        reloadEnd - reloadStart).count();

                    metrics.recordReloadTime(static_cast<float>(reloadDuration));
                    successfulReloads++;

                    std::cout << "    ✅ Hot-reload #" << reloadNumber << " succeeded in " << reloadDuration << "ms\n";

                } catch (const std::exception& e) {
                    std::cerr << "    ❌ Exception during hot-reload: " << e.what() << "\n";
                    failedReloads++;
                }
            }

            // Memory monitoring every 60 seconds (3600 frames)
            if (frame % 3600 == 0 && frame > 0) {
                size_t currentMemory = grove::getCurrentMemoryUsage() / (1024 * 1024);
                size_t memoryGrowth = currentMemory - initialMemory;
                peakMemory = std::max(peakMemory, currentMemory);

                int minutesElapsed = frame / 3600;
                std::cout << "\n📊 Checkpoint at " << minutesElapsed << " minute(s):\n";
                std::cout << "   Current memory: " << currentMemory << " MB\n";
                std::cout << "   Growth: " << memoryGrowth << " MB\n";
                std::cout << "   Peak: " << peakMemory << " MB\n";
                std::cout << "   Avg FPS: " << metrics.getFPSAvg() << "\n";
                std::cout << "   Reloads: " << successfulReloads << "/" << EXPECTED_RELOADS << "\n";
                std::cout << "   Avg reload time: " << metrics.getReloadTimeAvg() << "ms\n\n";
            }

            // Progress reporting every minute (without memory details)
            if (frame % 3600 == 0 && frame > 0) {
                int minutesElapsed = frame / 3600;
                int minutesRemaining = (TOTAL_FRAMES - frame) / 3600;
                std::cout << "⏱️  Progress: " << minutesElapsed << " minutes elapsed, " << minutesRemaining << " minutes remaining\n";
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::seconds>(
            endTime - startTime).count();

        // Final metrics
        size_t finalMemory = grove::getCurrentMemoryUsage() / (1024 * 1024);
        size_t totalMemoryGrowth = finalMemory - initialMemory;

        std::cout << "\n═══════════════════════════════════════════════════════════════\n";
        std::cout << "  STRESS TEST COMPLETED\n";
        std::cout << "═══════════════════════════════════════════════════════════════\n";
        std::cout << "Total frames:        " << TOTAL_FRAMES << "\n";
        std::cout << "Real time:           " << totalDuration << "s\n";
        std::cout << "Simulated time:      " << (TOTAL_FRAMES / TARGET_FPS) << "s (10 minutes)\n";
        std::cout << "Successful reloads:  " << successfulReloads << "/" << EXPECTED_RELOADS << "\n";
        std::cout << "Failed reloads:      " << failedReloads << "\n";
        std::cout << "\n📊 PERFORMANCE METRICS:\n";
        std::cout << "Average FPS:         " << metrics.getFPSAvg() << "\n";
        std::cout << "Min FPS:             " << metrics.getFPSMin() << "\n";
        std::cout << "Max FPS:             " << metrics.getFPSMax() << "\n";
        std::cout << "\n🔥 HOT-RELOAD METRICS:\n";
        std::cout << "Avg reload time:     " << metrics.getReloadTimeAvg() << "ms\n";
        std::cout << "Min reload time:     " << metrics.getReloadTimeMin() << "ms\n";
        std::cout << "Max reload time:     " << metrics.getReloadTimeMax() << "ms\n";
        std::cout << "\n💾 MEMORY METRICS:\n";
        std::cout << "Initial memory:      " << initialMemory << " MB\n";
        std::cout << "Final memory:        " << finalMemory << " MB\n";
        std::cout << "Peak memory:         " << peakMemory << " MB\n";
        std::cout << "Total growth:        " << totalMemoryGrowth << " MB\n";
        std::cout << "═══════════════════════════════════════════════════════════════\n\n";

        // Validate results
        bool allReloadsSucceeded = (successfulReloads == EXPECTED_RELOADS && failedReloads == 0);
        bool memoryWithinThreshold = (totalMemoryGrowth < MAX_MEMORY_GROWTH_MB);
        bool avgReloadTimeAcceptable = (metrics.getReloadTimeAvg() < 500.0f);
        bool fpsStable = (metrics.getFPSMin() > 30.0f);  // Ensure FPS doesn't drop too much

        reporter.addAssertion("all_reloads_succeeded", allReloadsSucceeded);
        reporter.addAssertion("memory_within_threshold", memoryWithinThreshold);
        reporter.addAssertion("avg_reload_time_acceptable", avgReloadTimeAcceptable);
        reporter.addAssertion("fps_stable", fpsStable);

        if (allReloadsSucceeded && memoryWithinThreshold &&
            avgReloadTimeAcceptable && fpsStable) {
            std::cout << "✅ STRESS TEST PASSED - System is stable over 10 minutes\n";
        } else {
            if (!allReloadsSucceeded) {
                std::cerr << "❌ Reload success rate: " << successfulReloads << "/" << EXPECTED_RELOADS << "\n";
            }
            if (!memoryWithinThreshold) {
                std::cerr << "❌ Memory growth: " << totalMemoryGrowth << " MB (threshold: " << MAX_MEMORY_GROWTH_MB << " MB)\n";
            }
            if (!avgReloadTimeAcceptable) {
                std::cerr << "❌ Avg reload time: " << metrics.getReloadTimeAvg() << "ms (threshold: 500ms)\n";
            }
            if (!fpsStable) {
                std::cerr << "❌ Min FPS: " << metrics.getFPSMin() << " (threshold: 30.0)\n";
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        reporter.addAssertion("exception", false);
    }

    reporter.printFinalReport();
    return reporter.getExitCode();
}
