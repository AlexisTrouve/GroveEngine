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
#include <stdexcept>

using namespace grove;

/**
 * Test 06: Error Recovery
 *
 * Objectif: Valider que le système peut détecter et récupérer automatiquement
 *           d'un crash de module via hot-reload.
 *
 * Scénario:
 * 1. Charger ErrorRecoveryModule avec crash planifié à frame 60
 * 2. Lancer execution jusqu'au crash
 * 3. Détecter le crash (exception)
 * 4. Trigger hot-reload automatique
 * 5. Vérifier que le module récupère (auto-recovery)
 * 6. Continuer execution normalement
 *
 * Métriques:
 * - Crash detection time
 * - Recovery success rate
 * - State preservation après recovery
 * - Stabilité du moteur
 */

int main() {
    TestReporter reporter("Error Recovery");
    TestMetrics metrics;

    std::cout << "================================================================================\n";
    std::cout << "TEST: Error Recovery - Crash Detection & Auto-Recovery\n";
    std::cout << "================================================================================\n\n";

    // === SETUP ===
    std::cout << "Setup: Loading ErrorRecoveryModule with crash trigger...\n";

    ModuleLoader loader;
    auto moduleSystem = std::make_unique<SequentialModuleSystem>();

    // Charger module
    std::string modulePath = "./libErrorRecoveryModule.so";
    auto module = loader.load(modulePath, "ErrorRecoveryModule", false);

    // Config: crash à frame 60, type runtime_error
    nlohmann::json configJson;
    configJson["crashAtFrame"] = 60;
    configJson["crashType"] = 0; // runtime_error
    configJson["enableAutoRecovery"] = true;
    configJson["versionTag"] = "v1.0";
    auto config = std::make_unique<JsonDataNode>("config", configJson);

    module->setConfiguration(*config, nullptr, nullptr);
    moduleSystem->registerModule("ErrorRecoveryModule", std::move(module));

    std::cout << "  ✓ Module loaded with crash trigger at frame 60\n\n";

    // === PHASE 1: Run until crash ===
    std::cout << "Phase 1: Running until crash (target frame: 60)...\n";

    bool crashDetected = false;
    int crashFrame = -1;
    auto crashDetectionStart = std::chrono::high_resolution_clock::now();

    for (int frame = 1; frame <= 100; frame++) {
        try {
            auto frameStart = std::chrono::high_resolution_clock::now();

            moduleSystem->processModules(1.0f / 60.0f);

            auto frameEnd = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
            metrics.recordFPS(1000.0f / frameTime);

            if (frame % 20 == 0) {
                std::cout << "  Frame " << frame << "/100 - OK\n";
            }

        } catch (const std::exception& e) {
            // CRASH DÉTECTÉ !
            auto crashDetectionEnd = std::chrono::high_resolution_clock::now();
            float detectionTime = std::chrono::duration<float, std::milli>(
                crashDetectionEnd - crashDetectionStart).count();

            crashDetected = true;
            crashFrame = frame;

            std::cout << "\n💥 CRASH DETECTED at frame " << frame << "\n";
            std::cout << "   Exception: " << e.what() << "\n";
            std::cout << "   Detection time: " << detectionTime << "ms\n\n";

            metrics.recordCrash("runtime_error at frame " + std::to_string(frame));
            reporter.addMetric("crash_detection_time_ms", detectionTime);

            break;
        }
    }

    ASSERT_TRUE(crashDetected, "Crash should have been detected");
    ASSERT_EQ(crashFrame, 60, "Crash should occur at frame 60");
    reporter.addAssertion("crash_detected", crashDetected);
    reporter.addAssertion("crash_at_expected_frame", crashFrame == 60);

    // === PHASE 2: Extract state before recovery ===
    std::cout << "Phase 2: Extracting state before recovery...\n";

    auto crashedModule = moduleSystem->extractModule();
    auto preRecoveryState = crashedModule->getState();

    auto* jsonNodeBefore = dynamic_cast<JsonDataNode*>(preRecoveryState.get());
    if (!jsonNodeBefore) {
        std::cerr << "❌ Failed to extract state before recovery\n";
        return 1;
    }

    const auto& stateBefore = jsonNodeBefore->getJsonData();
    int frameCountBefore = stateBefore.value("frameCount", 0);
    int crashCountBefore = stateBefore.value("crashCount", 0);
    bool hasCrashedBefore = stateBefore.value("hasCrashed", false);

    std::cout << "  State before recovery:\n";
    std::cout << "    Frame count: " << frameCountBefore << "\n";
    std::cout << "    Crash count: " << crashCountBefore << "\n";
    std::cout << "    Has crashed: " << (hasCrashedBefore ? "YES" : "NO") << "\n\n";

    ASSERT_TRUE(hasCrashedBefore, "Module should be in crashed state");

    // === PHASE 3: Trigger hot-reload (recovery) ===
    std::cout << "Phase 3: Triggering hot-reload for recovery...\n";

    auto recoveryStart = std::chrono::high_resolution_clock::now();

    // Hot-reload via ModuleLoader
    auto recoveredModule = loader.reload(std::move(crashedModule));

    auto recoveryEnd = std::chrono::high_resolution_clock::now();
    float recoveryTime = std::chrono::duration<float, std::milli>(recoveryEnd - recoveryStart).count();

    std::cout << "  ✓ Hot-reload completed in " << recoveryTime << "ms\n";

    metrics.recordReloadTime(recoveryTime);
    reporter.addMetric("recovery_time_ms", recoveryTime);

    // Ré-enregistrer module récupéré
    moduleSystem->registerModule("ErrorRecoveryModule", std::move(recoveredModule));

    // === PHASE 4: Verify recovery ===
    std::cout << "\nPhase 4: Verifying recovery...\n";

    auto recoveredModuleRef = moduleSystem->extractModule();
    auto postRecoveryState = recoveredModuleRef->getState();

    auto* jsonNodeAfter = dynamic_cast<JsonDataNode*>(postRecoveryState.get());
    if (!jsonNodeAfter) {
        std::cerr << "❌ Failed to extract state after recovery\n";
        return 1;
    }

    const auto& stateAfter = jsonNodeAfter->getJsonData();
    int frameCountAfter = stateAfter.value("frameCount", 0);
    int crashCountAfter = stateAfter.value("crashCount", 0);
    int recoveryCountAfter = stateAfter.value("recoveryCount", 0);
    bool hasCrashedAfter = stateAfter.value("hasCrashed", false);
    int crashAtFrameAfter = stateAfter.value("crashAtFrame", -1);

    std::cout << "  State after recovery:\n";
    std::cout << "    Frame count: " << frameCountAfter << "\n";
    std::cout << "    Crash count: " << crashCountAfter << "\n";
    std::cout << "    Recovery count: " << recoveryCountAfter << "\n";
    std::cout << "    Has crashed: " << (hasCrashedAfter ? "YES" : "NO") << "\n";
    std::cout << "    Crash trigger: " << crashAtFrameAfter << "\n\n";

    // Vérifications de recovery
    ASSERT_EQ(frameCountAfter, frameCountBefore, "Frame count should be preserved");
    ASSERT_FALSE(hasCrashedAfter, "Module should no longer be in crashed state");
    ASSERT_EQ(recoveryCountAfter, 1, "Recovery count should be 1");
    ASSERT_EQ(crashAtFrameAfter, -1, "Crash trigger should be disabled");

    reporter.addAssertion("frame_count_preserved", frameCountAfter == frameCountBefore);
    reporter.addAssertion("crash_state_cleared", !hasCrashedAfter);
    reporter.addAssertion("recovery_count_incremented", recoveryCountAfter == 1);
    reporter.addAssertion("crash_trigger_disabled", crashAtFrameAfter == -1);

    std::cout << "  ✅ RECOVERY SUCCESSFUL - Module is healthy again\n\n";

    // Ré-enregistrer pour phase 5
    moduleSystem->registerModule("ErrorRecoveryModule", std::move(recoveredModuleRef));

    // === PHASE 5: Continue execution (stability check) ===
    std::cout << "Phase 5: Stability check - Running 120 more frames...\n";

    bool stableExecution = true;
    int framesAfterRecovery = 0;

    for (int frame = 1; frame <= 120; frame++) {
        try {
            auto frameStart = std::chrono::high_resolution_clock::now();

            moduleSystem->processModules(1.0f / 60.0f);

            auto frameEnd = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
            metrics.recordFPS(1000.0f / frameTime);

            framesAfterRecovery++;

            if (frame % 30 == 0) {
                std::cout << "  Frame " << frame << "/120 - Stable\n";
            }

        } catch (const std::exception& e) {
            std::cout << "\n❌ UNEXPECTED CRASH after recovery at frame " << frame << "\n";
            std::cout << "   Exception: " << e.what() << "\n";
            stableExecution = false;
            break;
        }
    }

    ASSERT_TRUE(stableExecution, "Module should execute stably after recovery");
    ASSERT_EQ(framesAfterRecovery, 120, "Should complete all 120 frames");

    reporter.addAssertion("stable_after_recovery", stableExecution);
    reporter.addMetric("frames_after_recovery", static_cast<float>(framesAfterRecovery));

    std::cout << "  ✅ Stability verified - " << framesAfterRecovery << " frames executed without issues\n\n";

    // === VÉRIFICATIONS FINALES ===
    std::cout << "Final verifications...\n";

    // Memory growth
    size_t memGrowth = metrics.getMemoryGrowth();
    float memGrowthMB = memGrowth / (1024.0f * 1024.0f);
    ASSERT_LT(memGrowthMB, 10.0f, "Memory growth should be < 10MB");
    reporter.addMetric("memory_growth_mb", memGrowthMB);

    // FPS (moins strict pour test de recovery - focus sur stability)
    float minFPS = metrics.getFPSMin();
    ASSERT_GT(minFPS, 5.0f, "Min FPS should be > 5 (recovery test allows slower frames)");
    reporter.addMetric("fps_min", minFPS);
    reporter.addMetric("fps_avg", metrics.getFPSAvg());

    // Recovery time threshold
    ASSERT_LT(recoveryTime, 500.0f, "Recovery time should be < 500ms");

    // Crash count
    int totalCrashes = metrics.getCrashCount();
    ASSERT_EQ(totalCrashes, 1, "Should have exactly 1 controlled crash");
    reporter.addMetric("total_crashes", static_cast<float>(totalCrashes));

    // === RAPPORTS ===
    std::cout << "\n";
    std::cout << "Summary:\n";
    std::cout << "  🎯 Crash detected at frame " << crashFrame << " (expected: 60)\n";
    std::cout << "  🔄 Recovery time: " << recoveryTime << "ms\n";
    std::cout << "  ✅ Stable execution: " << framesAfterRecovery << " frames after recovery\n";
    std::cout << "  💾 Memory growth: " << memGrowthMB << " MB\n";
    std::cout << "  📊 FPS: min=" << minFPS << ", avg=" << metrics.getFPSAvg() << "\n\n";

    metrics.printReport();
    reporter.printFinalReport();

    return reporter.getExitCode();
}
