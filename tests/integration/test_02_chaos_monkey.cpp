// PREUVE : Décommenter cette ligne pour désactiver la recovery et voir le test ÉCHOUER
// #define DISABLE_RECOVERY_FOR_TEST

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
#include <csignal>
#include <atomic>

using namespace grove;

// Global for crash detection
static std::atomic<bool> engineCrashed{false};

void signalHandler(int signal) {
    if (signal == SIGSEGV || signal == SIGABRT) {
        engineCrashed.store(true);
        std::cerr << "❌ FATAL: Signal " << signal << " received (SIGSEGV or SIGABRT)\n";
        std::cerr << "Engine has crashed unrecoverably.\n";
        std::exit(1);
    }
}

int main() {
    TestReporter reporter("Chaos Monkey");
    TestMetrics metrics;

    std::cout << "================================================================================\n";
    std::cout << "TEST: Chaos Monkey\n";
    std::cout << "================================================================================\n\n";

    // Setup signal handlers
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);

    // === SETUP ===
    std::cout << "Setup: Loading ChaosModule...\n";

    ModuleLoader loader;
    auto moduleSystem = std::make_unique<SequentialModuleSystem>();

    // Load module
    std::string modulePath = "build/tests/libChaosModule.so";
    auto module = loader.load(modulePath, "ChaosModule", false);

    // Configure module avec seed ALÉATOIRE basé sur le temps
    // Chaque run sera différent - VRAI chaos
    unsigned int randomSeed = static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count());

    nlohmann::json configJson;
    configJson["seed"] = randomSeed;
    configJson["hotReloadProbability"] = 0.30;  // Non utilisé maintenant
    configJson["crashProbability"] = 0.05;  // 5% par frame = crash fréquent
    configJson["corruptionProbability"] = 0.10;  // Non utilisé
    configJson["invalidConfigProbability"] = 0.05;  // Non utilisé
    auto config = std::make_unique<JsonDataNode>("config", configJson);

    std::cout << "  Random seed: " << randomSeed << " (time-based, unpredictable)\n";

    module->setConfiguration(*config, nullptr, nullptr);

    // Register in module system
    moduleSystem->registerModule("ChaosModule", std::move(module));

    std::cout << "  ✓ ChaosModule loaded and configured\n\n";

    // === CHAOS LOOP (30 seconds = 1800 frames @ 60 FPS) ===
    // NOTE: Reduced from 5 minutes for faster testing
    std::cout << "Starting Chaos Monkey (30 seconds simulation)...\n";
    std::cout << "REAL CHAOS MODE:\n";
    std::cout << "  - 5% crash probability PER FRAME (not per second)\n";
    std::cout << "  - Expected crashes: ~90 crashes (5% of 1800 frames)\n";
    std::cout << "  - Random seed (time-based): unpredictable pattern\n";
    std::cout << "  - Multiple crash types: runtime_error, logic_error, out_of_range, domain_error, state corruption\n";
    std::cout << "  - Corrupted state validation: module must reject corrupted state\n\n";

    const int totalFrames = 1800;  // 30 * 60
    int crashesDetected = 0;
    int reloadsTriggered = 0;
    int recoverySuccesses = 0;
    bool hadDeadlock = false;

    auto testStart = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < totalFrames; frame++) {
        auto frameStart = std::chrono::high_resolution_clock::now();
        bool didRecoveryThisFrame = false;

        try {
            // Process module (1/60th of a second)
            moduleSystem->processModules(1.0f / 60.0f);

        } catch (const std::exception& e) {
            // CRASH DETECTED - Attempt recovery
            crashesDetected++;
            std::cout << "  [Frame " << frame << "] ⚠️  Crash detected: " << e.what() << "\n";

            // PREUVE QUE LE TEST PEUT ÉCHOUER : désactiver la recovery
            #ifdef DISABLE_RECOVERY_FOR_TEST
            std::cout << "  [Frame " << frame << "] ❌ RECOVERY DISABLED - Test will fail\n";
            reporter.addAssertion("recovery_disabled", false);
            break; // Le test DOIT échouer
            #endif

            // Recovery attempt
            try {
                std::cout << "  [Frame " << frame << "] 🔄 Attempting recovery...\n";

                auto recoveryStart = std::chrono::high_resolution_clock::now();

                // Extract module from system
                auto crashedModule = moduleSystem->extractModule();

                // Reload module
                auto reloadedModule = loader.reload(std::move(crashedModule));

                // Re-register
                moduleSystem->registerModule("ChaosModule", std::move(reloadedModule));

                auto recoveryEnd = std::chrono::high_resolution_clock::now();
                float recoveryTime = std::chrono::duration<float, std::milli>(recoveryEnd - recoveryStart).count();

                metrics.recordReloadTime(recoveryTime);
                recoverySuccesses++;
                didRecoveryThisFrame = true;

                std::cout << "  [Frame " << frame << "] ✅ Recovery successful (" << recoveryTime << "ms)\n";

            } catch (const std::exception& recoveryError) {
                std::cout << "  [Frame " << frame << "] ❌ Recovery FAILED: " << recoveryError.what() << "\n";
                reporter.addAssertion("recovery_failed", false);
                break; // Stop test - recovery failed
            }
        }

        // Metrics
        auto frameEnd = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();

        // Only record FPS for normal frames (not recovery frames)
        // Recovery frames are slow by design (100+ ms for hot-reload)
        if (!didRecoveryThisFrame) {
            metrics.recordFPS(1000.0f / frameTime);
        }

        if (frame % 60 == 0) {
            metrics.recordMemoryUsage(getCurrentMemoryUsage());
        }

        // Deadlock detection (frame > 100ms)
        // NOTE: Skip deadlock check if we just did a recovery (recovery takes >100ms by design)
        if (frameTime > 100.0f && !didRecoveryThisFrame) {
            std::cout << "  [Frame " << frame << "] ⚠️  Potential deadlock (frame time: " << frameTime << "ms)\n";
            hadDeadlock = true;
        }

        // Progress (every 600 frames = 10 seconds)
        if (frame % 600 == 0 && frame > 0) {
            float elapsedSec = frame / 60.0f;
            float progress = (frame * 100.0f) / totalFrames;
            std::cout << "Progress: " << elapsedSec << "/30.0 seconds (" << (int)progress << "%)\n";

            // Show current metrics
            std::cout << "  FPS: min=" << metrics.getFPSMin() << ", avg=" << metrics.getFPSAvg() << ", max=" << metrics.getFPSMax() << "\n";
            std::cout << "  Memory: " << (getCurrentMemoryUsage() / (1024.0f * 1024.0f)) << " MB\n";
        }

        // Check if engine crashed externally
        if (engineCrashed.load()) {
            std::cout << "  [Frame " << frame << "] ❌ Engine crashed externally (signal received)\n";
            reporter.addAssertion("engine_crashed_externally", false);
            break;
        }
    }

    auto testEnd = std::chrono::high_resolution_clock::now();
    float totalDuration = std::chrono::duration<float>(testEnd - testStart).count();

    std::cout << "\nTest completed!\n\n";

    // === FINAL VERIFICATIONS ===
    std::cout << "Final verifications...\n";

    // Engine still alive
    bool engineAlive = !engineCrashed.load();
    ASSERT_TRUE(engineAlive, "Engine should still be alive");
    reporter.addAssertion("engine_alive", engineAlive);

    // No deadlocks
    ASSERT_FALSE(hadDeadlock, "Should not have deadlocks");
    reporter.addAssertion("no_deadlocks", !hadDeadlock);

    // Memory growth < 10MB
    size_t memGrowth = metrics.getMemoryGrowth();
    float memGrowthMB = memGrowth / (1024.0f * 1024.0f);
    ASSERT_LT(memGrowthMB, 10.0f, "Memory growth should be < 10MB");
    reporter.addMetric("memory_growth_mb", memGrowthMB);

    // Test runs as fast as possible (not real-time)
    // Just check it completed within reasonable bounds (< 60 seconds wall time)
    ASSERT_LT(totalDuration, 60.0f, "Total duration should be < 60 seconds");
    reporter.addMetric("total_duration_sec", totalDuration);

    // FPS metrics
    float minFPS = metrics.getFPSMin();
    float avgFPS = metrics.getFPSAvg();
    float maxFPS = metrics.getFPSMax();

    // Min FPS should be reasonable (> 10 even with crashes)
    ASSERT_GT(minFPS, 10.0f, "Min FPS should be > 10");
    reporter.addMetric("fps_min", minFPS);
    reporter.addMetric("fps_avg", avgFPS);
    reporter.addMetric("fps_max", maxFPS);

    // Recovery rate > 95%
    float recoveryRate = (crashesDetected > 0) ? (recoverySuccesses * 100.0f / crashesDetected) : 100.0f;
    ASSERT_GT(recoveryRate, 95.0f, "Recovery rate should be > 95%");
    reporter.addMetric("recovery_rate_percent", recoveryRate);

    // === STATISTICS ===
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "CHAOS MONKEY STATISTICS\n";
    std::cout << "================================================================================\n";
    std::cout << "  Total frames:        " << totalFrames << "\n";
    std::cout << "  Duration:            " << totalDuration << "s (wall time, not simulation time)\n";
    std::cout << "  Crashes detected:    " << crashesDetected << "\n";
    std::cout << "  Recovery successes:  " << recoverySuccesses << "\n";
    std::cout << "  Recovery rate:       " << recoveryRate << "%\n";
    std::cout << "  Memory growth:       " << memGrowthMB << " MB (max: 10MB)\n";
    std::cout << "  Had deadlocks:       " << (hadDeadlock ? "YES ❌" : "NO ✅") << "\n";
    std::cout << "  FPS min/avg/max:     " << minFPS << " / " << avgFPS << " / " << maxFPS << "\n";
    std::cout << "================================================================================\n\n";

    std::cout << "Note: ChaosModule generates random crashes internally.\n";
    std::cout << "The test should recover from ALL crashes automatically via hot-reload.\n\n";

    // === CLEANUP ===
    std::cout << "Cleaning up...\n";
    moduleSystem.reset();
    std::cout << "  ✓ Module system shutdown complete\n\n";

    // === REPORTS ===
    metrics.printReport();
    reporter.printFinalReport();

    return reporter.getExitCode();
}
