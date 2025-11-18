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
#include <numeric>
#include <fstream>

using namespace grove;

/**
 * Test 07: Limite Tests
 *
 * Objectif: Valider la robustesse du système face aux conditions extrêmes:
 *           - Large state (100MB+)
 *           - Long initialization
 *           - Timeouts
 *           - Memory pressure
 *           - State corruption detection
 */

int main() {
    TestReporter reporter("Limite Tests");
    TestMetrics metrics;

    std::cout << "================================================================================\n";
    std::cout << "TEST: Limite Tests - Extreme Conditions & Edge Cases\n";
    std::cout << "================================================================================\n\n";

    // ========================================================================
    // TEST 1: Large State Serialization
    // ========================================================================
    std::cout << "=== TEST 1: Large State Serialization ===\n\n";

    ModuleLoader loader;
    auto moduleSystem = std::make_unique<SequentialModuleSystem>();

    std::string modulePath = "./libHeavyStateModule.so";
    auto module = loader.load(modulePath, "HeavyStateModule", false);

    // Config: particules réduites pour test rapide, mais assez pour être significatif
    nlohmann::json configJson;
    configJson["version"] = "v1.0";
    configJson["particleCount"] = 100000;  // 100k au lieu de 1M pour test rapide
    configJson["terrainSize"] = 1000;      // 1000x1000 au lieu de 10000x10000
    configJson["initDuration"] = 2.0f;     // 2s au lieu de 8s
    configJson["initTimeout"] = 5.0f;

    auto config = std::make_unique<JsonDataNode>("config", configJson);

    // Mesurer temps d'initialisation
    std::cout << "Initializing module...\n";
    auto initStart = std::chrono::high_resolution_clock::now();

    module->setConfiguration(*config, nullptr, nullptr);

    auto initEnd = std::chrono::high_resolution_clock::now();
    float initTime = std::chrono::duration<float>(initEnd - initStart).count();

    std::cout << "  Init time: " << initTime << "s\n";
    ASSERT_GT(initTime, 1.5f, "Init should take at least 1.5s (simulated heavy init)");
    ASSERT_LT(initTime, 3.0f, "Init should not exceed 3s");
    reporter.addMetric("init_time_s", initTime);

    moduleSystem->registerModule("HeavyStateModule", std::move(module));

    // Exécuter quelques frames
    std::cout << "Running 300 frames...\n";
    for (int i = 0; i < 300; i++) {
        moduleSystem->processModules(1.0f / 60.0f);
    }

    // Mesurer temps de getState()
    std::cout << "Extracting state (getState)...\n";
    auto getStateStart = std::chrono::high_resolution_clock::now();

    auto heavyModule = moduleSystem->extractModule();
    auto state = heavyModule->getState();

    auto getStateEnd = std::chrono::high_resolution_clock::now();
    float getStateTime = std::chrono::duration<float, std::milli>(getStateEnd - getStateStart).count();

    std::cout << "  getState time: " << getStateTime << "ms\n";
    ASSERT_LT(getStateTime, 2000.0f, "getState() should be < 2000ms");
    reporter.addMetric("getstate_time_ms", getStateTime);

    // Estimer taille de l'état
    auto* jsonNode = dynamic_cast<JsonDataNode*>(state.get());
    if (jsonNode) {
        std::string stateStr = jsonNode->getJsonData().dump();
        size_t stateSize = stateStr.size();
        float stateSizeMB = stateSize / 1024.0f / 1024.0f;
        std::cout << "  State size: " << stateSizeMB << " MB\n";
        reporter.addMetric("state_size_mb", stateSizeMB);
    }

    // Recharger le module (simuler hot-reload)
    std::cout << "Reloading module...\n";
    auto reloadStart = std::chrono::high_resolution_clock::now();

    // setState() est appelé automatiquement par reload()
    auto moduleReloaded = loader.reload(std::move(heavyModule));

    auto reloadEnd = std::chrono::high_resolution_clock::now();
    float reloadTime = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();
    std::cout << "  Total reload time: " << reloadTime << "ms\n";
    reporter.addMetric("reload_time_ms", reloadTime);

    // Ré-enregistrer
    moduleSystem->registerModule("HeavyStateModule", std::move(moduleReloaded));

    // Vérifier intégrité après reload
    auto heavyModuleAfter = moduleSystem->extractModule();
    auto stateAfter = heavyModuleAfter->getState();
    auto* jsonNodeAfter = dynamic_cast<JsonDataNode*>(stateAfter.get());
    if (jsonNodeAfter) {
        const auto& dataAfter = jsonNodeAfter->getJsonData();
        int particleCount = dataAfter["config"]["particleCount"];
        ASSERT_EQ(particleCount, 100000, "Should have 100k particles after reload");
        reporter.addAssertion("particles_preserved", particleCount == 100000);
        std::cout << "  ✓ Particles preserved: " << particleCount << "\n";
    }

    // Ré-enregistrer pour continuer
    moduleSystem->registerModule("HeavyStateModule", std::move(heavyModuleAfter));

    // Continuer exécution
    std::cout << "Running 300 more frames post-reload...\n";
    for (int i = 0; i < 300; i++) {
        moduleSystem->processModules(1.0f / 60.0f);
    }

    std::cout << "\n✅ TEST 1 PASSED\n\n";

    // ========================================================================
    // TEST 2: Long Initialization Timeout
    // ========================================================================
    std::cout << "=== TEST 2: Long Initialization Timeout ===\n\n";

    auto moduleSystem2 = std::make_unique<SequentialModuleSystem>();
    auto moduleTimeout = loader.load(modulePath, "HeavyStateModule", false);

    // Config avec init long + timeout court (va échouer)
    nlohmann::json configTimeout;
    configTimeout["version"] = "v1.0";
    configTimeout["particleCount"] = 100000;
    configTimeout["terrainSize"] = 1000;
    configTimeout["initDuration"] = 4.0f;   // Init va prendre 4s
    configTimeout["initTimeout"] = 3.0f;    // Timeout à 3s (trop court)

    auto configTimeoutNode = std::make_unique<JsonDataNode>("config", configTimeout);

    bool timedOut = false;
    std::cout << "Attempting init with timeout=3s (duration=4s)...\n";
    try {
        moduleTimeout->setConfiguration(*configTimeoutNode, nullptr, nullptr);
    } catch (const std::exception& e) {
        std::string msg = e.what();
        if (msg.find("timeout") != std::string::npos ||
            msg.find("Timeout") != std::string::npos ||
            msg.find("exceeded") != std::string::npos) {
            timedOut = true;
            std::cout << "  ✓ Timeout detected: " << msg << "\n";
        } else {
            std::cout << "  ✗ Unexpected error: " << msg << "\n";
        }
    }

    ASSERT_TRUE(timedOut, "Should timeout when init > timeout threshold");
    reporter.addAssertion("timeout_detection", timedOut);

    // Réessayer avec timeout suffisant
    std::cout << "\nRetrying with adequate timeout=6s...\n";
    auto moduleTimeout2 = loader.load(modulePath, "HeavyStateModule", false);

    nlohmann::json configOk;
    configOk["version"] = "v1.0";
    configOk["particleCount"] = 50000;
    configOk["terrainSize"] = 500;
    configOk["initDuration"] = 2.0f;
    configOk["initTimeout"] = 5.0f;

    auto configOkNode = std::make_unique<JsonDataNode>("config", configOk);

    bool success = true;
    try {
        moduleTimeout2->setConfiguration(*configOkNode, nullptr, nullptr);
        moduleSystem2->registerModule("HeavyStateModule", std::move(moduleTimeout2));
        moduleSystem2->processModules(1.0f / 60.0f);
        std::cout << "  ✓ Init succeeded with adequate timeout\n";
    } catch (const std::exception& e) {
        success = false;
        std::cout << "  ✗ Failed: " << e.what() << "\n";
    }

    ASSERT_TRUE(success, "Should succeed with adequate timeout");
    reporter.addAssertion("timeout_recovery", success);

    std::cout << "\n✅ TEST 2 PASSED\n\n";

    // ========================================================================
    // TEST 3: Memory Pressure During Reload
    // ========================================================================
    std::cout << "=== TEST 3: Memory Pressure During Reload ===\n\n";

    // Créer un nouveau system pour ce test
    auto moduleSystem3Pressure = std::make_unique<SequentialModuleSystem>();
    auto modulePressureInit = loader.load(modulePath, "HeavyStateModule", false);

    nlohmann::json configPressure;
    configPressure["version"] = "v1.0";
    configPressure["particleCount"] = 10000;
    configPressure["terrainSize"] = 100;
    configPressure["initDuration"] = 0.5f;
    auto configPressureNode = std::make_unique<JsonDataNode>("config", configPressure);
    modulePressureInit->setConfiguration(*configPressureNode, nullptr, nullptr);
    moduleSystem3Pressure->registerModule("HeavyStateModule", std::move(modulePressureInit));

    size_t memBefore = getCurrentMemoryUsage();
    std::cout << "Memory before: " << (memBefore / 1024.0f / 1024.0f) << " MB\n";

    // Exécuter quelques frames
    std::cout << "Running 300 frames...\n";
    for (int i = 0; i < 300; i++) {
        moduleSystem3Pressure->processModules(1.0f / 60.0f);
    }

    // Allouer temporairement beaucoup de mémoire
    std::cout << "Allocating temporary 50MB during reload...\n";
    std::vector<uint8_t> tempAlloc;

    auto reloadPressureStart = std::chrono::high_resolution_clock::now();

    // Allouer 50MB
    tempAlloc.resize(50 * 1024 * 1024);
    std::fill(tempAlloc.begin(), tempAlloc.end(), 0x42);

    size_t memDuringAlloc = getCurrentMemoryUsage();
    std::cout << "  Memory with allocation: " << (memDuringAlloc / 1024.0f / 1024.0f) << " MB\n";

    // Reload pendant la pression mémoire
    auto modulePressure = moduleSystem3Pressure->extractModule();
    auto modulePressureReloaded = loader.reload(std::move(modulePressure));
    moduleSystem3Pressure->registerModule("HeavyStateModule", std::move(modulePressureReloaded));

    auto reloadPressureEnd = std::chrono::high_resolution_clock::now();
    float reloadPressureTime = std::chrono::duration<float, std::milli>(
        reloadPressureEnd - reloadPressureStart).count();

    std::cout << "  Reload under pressure: " << reloadPressureTime << "ms\n";
    reporter.addMetric("reload_under_pressure_ms", reloadPressureTime);

    // Libérer allocation temporaire
    tempAlloc.clear();
    tempAlloc.shrink_to_fit();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    size_t memAfter = getCurrentMemoryUsage();
    std::cout << "  Memory after cleanup: " << (memAfter / 1024.0f / 1024.0f) << " MB\n";

    long memGrowth = static_cast<long>(memAfter) - static_cast<long>(memBefore);
    float memGrowthMB = memGrowth / 1024.0f / 1024.0f;
    std::cout << "  Net memory growth: " << memGrowthMB << " MB\n";

    // Tolérance: max 10MB de croissance
    ASSERT_LT(std::abs(memGrowth), 10 * 1024 * 1024, "Memory growth should be < 10MB");
    reporter.addMetric("memory_growth_mb", memGrowthMB);

    std::cout << "\n✅ TEST 3 PASSED\n\n";

    // ========================================================================
    // TEST 4: Incremental Reloads
    // ========================================================================
    std::cout << "=== TEST 4: Incremental Reloads ===\n\n";

    auto moduleSystem3 = std::make_unique<SequentialModuleSystem>();

    nlohmann::json configIncremental;
    configIncremental["version"] = "v1.0";
    configIncremental["particleCount"] = 10000;  // Petit pour test rapide
    configIncremental["terrainSize"] = 100;
    configIncremental["initDuration"] = 0.5f;
    configIncremental["incrementalState"] = true;

    auto configIncrNode = std::make_unique<JsonDataNode>("config", configIncremental);
    auto moduleIncr = loader.load(modulePath, "HeavyStateModule", false);
    moduleIncr->setConfiguration(*configIncrNode, nullptr, nullptr);
    moduleSystem3->registerModule("HeavyStateModule", std::move(moduleIncr));

    std::vector<float> incrementalTimes;

    std::cout << "Performing 5 incremental reloads...\n";
    for (int reload = 0; reload < 5; reload++) {
        // Exécuter 60 frames
        for (int i = 0; i < 60; i++) {
            moduleSystem3->processModules(1.0f / 60.0f);
        }

        // Reload
        auto incStart = std::chrono::high_resolution_clock::now();

        auto moduleInc = moduleSystem3->extractModule();
        auto moduleIncReloaded = loader.reload(std::move(moduleInc));
        moduleSystem3->registerModule("HeavyStateModule", std::move(moduleIncReloaded));

        auto incEnd = std::chrono::high_resolution_clock::now();
        float incTime = std::chrono::duration<float, std::milli>(incEnd - incStart).count();

        incrementalTimes.push_back(incTime);
        std::cout << "  Reload #" << reload << ": " << incTime << "ms\n";
    }

    float avgIncremental = std::accumulate(incrementalTimes.begin(), incrementalTimes.end(), 0.0f)
                          / incrementalTimes.size();
    std::cout << "\nAverage incremental reload: " << avgIncremental << "ms\n";

    ASSERT_LT(avgIncremental, 2000.0f, "Incremental reloads should be reasonably fast");
    reporter.addMetric("avg_incremental_reload_ms", avgIncremental);

    std::cout << "\n✅ TEST 4 PASSED\n\n";

    // ========================================================================
    // TEST 5: State Corruption Detection
    // ========================================================================
    std::cout << "=== TEST 5: State Corruption Detection ===\n\n";

    auto moduleSystem4 = std::make_unique<SequentialModuleSystem>();

    nlohmann::json configNormal;
    configNormal["version"] = "v1.0";
    configNormal["particleCount"] = 1000;
    configNormal["terrainSize"] = 50;
    configNormal["initDuration"] = 0.2f;

    auto configNormalNode = std::make_unique<JsonDataNode>("config", configNormal);
    auto moduleNormal = loader.load(modulePath, "HeavyStateModule", false);
    moduleNormal->setConfiguration(*configNormalNode, nullptr, nullptr);
    moduleSystem4->registerModule("HeavyStateModule", std::move(moduleNormal));

    // Exécuter un peu
    for (int i = 0; i < 60; i++) {
        moduleSystem4->processModules(1.0f / 60.0f);
    }

    // Créer un état corrompu
    std::cout << "Creating corrupted state...\n";
    nlohmann::json corruptedState;
    corruptedState["version"] = "v1.0";
    corruptedState["frameCount"] = "INVALID_STRING";  // Type incorrect
    corruptedState["config"]["particleCount"] = -500;  // Valeur invalide
    corruptedState["config"]["terrainWidth"] = 100;
    corruptedState["config"]["terrainHeight"] = 100;
    corruptedState["particles"]["count"] = 1000;
    corruptedState["particles"]["data"] = "CORRUPTED";
    corruptedState["terrain"]["width"] = 50;
    corruptedState["terrain"]["height"] = 50;
    corruptedState["terrain"]["compressed"] = true;
    corruptedState["terrain"]["data"] = "CORRUPTED";
    corruptedState["history"] = nlohmann::json::array();

    auto corruptedNode = std::make_unique<JsonDataNode>("corrupted", corruptedState);

    bool detectedCorruption = false;
    std::cout << "Attempting to apply corrupted state...\n";
    try {
        auto moduleCorrupt = loader.load(modulePath, "HeavyStateModule", false);
        moduleCorrupt->setState(*corruptedNode);
    } catch (const std::exception& e) {
        std::string msg = e.what();
        std::cout << "  ✓ Corruption detected: " << msg << "\n";
        detectedCorruption = true;
    }

    ASSERT_TRUE(detectedCorruption, "Should detect corrupted state");
    reporter.addAssertion("corruption_detection", detectedCorruption);

    // Vérifier que le module d'origine reste fonctionnel
    std::cout << "Verifying original module still functional...\n";
    bool stillFunctional = true;
    try {
        for (int i = 0; i < 60; i++) {
            moduleSystem4->processModules(1.0f / 60.0f);
        }
        std::cout << "  ✓ Module remains functional\n";
    } catch (const std::exception& e) {
        stillFunctional = false;
        std::cout << "  ✗ Module broken: " << e.what() << "\n";
    }

    ASSERT_TRUE(stillFunctional, "Module should remain functional after rejected corrupted state");
    reporter.addAssertion("functional_after_corruption", stillFunctional);

    std::cout << "\n✅ TEST 5 PASSED\n\n";

    // ========================================================================
    // RAPPORT FINAL
    // ========================================================================

    std::cout << "================================================================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "================================================================================\n\n";

    metrics.printReport();
    reporter.printFinalReport();

    std::cout << "\n================================================================================\n";
    std::cout << "Result: " << (reporter.getExitCode() == 0 ? "✅ ALL TESTS PASSED" : "❌ SOME TESTS FAILED") << "\n";
    std::cout << "================================================================================\n";

    return reporter.getExitCode();
}
