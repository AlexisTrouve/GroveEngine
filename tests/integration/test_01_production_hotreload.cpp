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
#include <fstream>
#include <regex>

using namespace grove;

int main() {
    TestReporter reporter("Production Hot-Reload");
    TestMetrics metrics;

    std::cout << "================================================================================\n";
    std::cout << "TEST: Production Hot-Reload\n";
    std::cout << "================================================================================\n\n";

    // === SETUP ===
    std::cout << "Setup: Loading TankModule...\n";

    ModuleLoader loader;
    auto moduleSystem = std::make_unique<SequentialModuleSystem>();

    // Charger module
    std::string modulePath = "./libTankModule.so";
#ifdef _WIN32
    modulePath = "./libTankModule.dll";
#endif
    auto module = loader.load(modulePath, "TankModule", false);

    // Config
    nlohmann::json configJson;
    configJson["tankCount"] = 50;
    configJson["version"] = "v1.0";
    auto config = std::make_unique<JsonDataNode>("config", configJson);

    // Initialiser (setConfiguration)
    module->setConfiguration(*config, nullptr, nullptr);

    // Enregistrer dans system
    moduleSystem->registerModule("TankModule", std::move(module));

    std::cout << "  ✓ Module loaded and initialized\n\n";

    // === PHASE 1: Pre-Reload (15s = 900 frames) ===
    std::cout << "Phase 1: Running 15s before reload...\n";

    // Créer input avec deltaTime
    nlohmann::json inputJson;
    inputJson["deltaTime"] = 1.0 / 60.0;
    auto inputNode = std::make_unique<JsonDataNode>("input", inputJson);

    for (int frame = 0; frame < 900; frame++) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        moduleSystem->processModules(1.0f / 60.0f);

        auto frameEnd = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();

        metrics.recordFPS(1000.0f / frameTime);

        if (frame % 60 == 0) {
            metrics.recordMemoryUsage(getCurrentMemoryUsage());
        }

        if (frame % 300 == 0) {
            std::cout << "  Frame " << frame << "/900\n";
        }
    }

    // Snapshot state AVANT reload
    auto tankModule = moduleSystem->extractModule();
    auto preReloadState = tankModule->getState();

    // Cast to JsonDataNode to access JSON
    auto* jsonNodeBefore = dynamic_cast<JsonDataNode*>(preReloadState.get());
    if (!jsonNodeBefore) {
        std::cerr << "❌ Failed to cast state to JsonDataNode\n";
        return 1;
    }

    const auto& stateJsonBefore = jsonNodeBefore->getJsonData();

    int tankCountBefore = stateJsonBefore["tanks"].size();
    std::string versionBefore = stateJsonBefore.value("version", "unknown");
    int frameCountBefore = stateJsonBefore.value("frameCount", 0);

    std::cout << "\nState snapshot BEFORE reload:\n";
    std::cout << "  Version:    " << versionBefore << "\n";
    std::cout << "  Tank count: " << tankCountBefore << "\n";
    std::cout << "  Frame:      " << frameCountBefore << "\n\n";

    ASSERT_EQ(tankCountBefore, 50, "Should have 50 tanks before reload");

    // Ré-enregistrer le module temporairement
    moduleSystem->registerModule("TankModule", std::move(tankModule));

    // DIAG/FIX: libérer le state pré-reload AVANT de décharger sa DLL d'origine.
    // preReloadState est un JsonDataNode créé par le module INITIAL ; le reload ci-
    // dessous décharge cette DLL. Le garder vivant jusqu'à la fin de main ferait
    // tourner ~JsonDataNode sur une vtable potentiellement démappée.
    preReloadState.reset();

    // === HOT-RELOAD ===
    std::cout << "Triggering hot-reload...\n";

    // Modifier version dans source (HEADER)
    std::cout << "  1. Modifying source code (v1.0 -> v2.0 HOT-RELOADED)...\n";

    // Test runs from build/tests/, so source files are at ../../tests/modules/
    std::ifstream input("../../tests/modules/TankModule.h");
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();

    // POURQUOI une variable + .length() au lieu du nombre magique 39 :
    // l'ancien code faisait replace(pos, 39, …) alors que la chaîne cherchée fait
    // 35 caractères. Les 4 caractères en trop étaient mangés APRÈS le ';' (le '\n'
    // + le début de la ligne suivante " // Module logging"), fusionnant les lignes
    // 35-36 → 'logger not declared' → "Compilation failed". En réutilisant
    // oldVersion.length() la longueur remplacée est toujours exacte.
    const std::string oldVersion = "std::string moduleVersion = \"v1.0\";";
    size_t pos = content.find(oldVersion);
    if (pos != std::string::npos) {
        content.replace(pos, oldVersion.length(), "std::string moduleVersion = \"v2.0 HOT-RELOADED\";");
    }

    std::ofstream output("../../tests/modules/TankModule.h");
    output << content;
    output.close();

    // Recompiler
    std::cout << "  2. Recompiling module...\n";
    // Use cmake --build to rebuild TankModule. This is generator-agnostic (works with
    // ninja, make, MSVC, etc.) and doesn't require ninja/make to be in PATH at runtime.
    // GROVE_CMAKE_COMMAND env var is set by CMakeLists.txt to the full cmake executable
    // path detected at configure time. Falls back to "cmake" if not set.
    {
        const char* cmakeExe = std::getenv("GROVE_CMAKE_COMMAND");
        std::string cmakeCmd = cmakeExe ? cmakeExe : "cmake";
        // Tests run from build/tests/, so the build root is one level up ("..").
        // We quote the cmake path to handle spaces (e.g. "C:\Program Files\CMake\bin\cmake.exe").
        std::string buildCmd = "\"" + cmakeCmd + "\"" +
                               " --build .." +
                               " --target TankModule";
#ifdef _WIN32
        buildCmd += " > NUL 2>&1";
#else
        buildCmd += " > /dev/null 2>&1";
#endif
        int buildResult = system(buildCmd.c_str());
        if (buildResult != 0) {
            std::cerr << "❌ Compilation failed!\n";
            return 1;
        }
    }
    std::cout << "  ✓ Compilation succeeded\n";

    // Wait for file to be ready (simulate file stability check)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Reload
    std::cout << "  3. Reloading module...\n";
    auto reloadStart = std::chrono::high_resolution_clock::now();

    // Extract module from system
    tankModule = moduleSystem->extractModule();

    // Use ModuleLoader::reload()
    auto newModule = loader.reload(std::move(tankModule));

    // Re-register
    moduleSystem->registerModule("TankModule", std::move(newModule));

    auto reloadEnd = std::chrono::high_resolution_clock::now();
    float reloadTime = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();

    metrics.recordReloadTime(reloadTime);
    reporter.addMetric("reload_time_ms", reloadTime);

    std::cout << "  ✓ Reload completed in " << reloadTime << "ms\n\n";

    // === VÉRIFICATIONS POST-RELOAD ===
    std::cout << "Verifying state preservation...\n";

    tankModule = moduleSystem->extractModule();
    auto postReloadState = tankModule->getState();
    auto* jsonNodeAfter = dynamic_cast<JsonDataNode*>(postReloadState.get());

    if (!jsonNodeAfter) {
        std::cerr << "❌ Failed to cast post-reload state to JsonDataNode\n";
        return 1;
    }

    const auto& stateJsonAfter = jsonNodeAfter->getJsonData();

    int tankCountAfter = stateJsonAfter["tanks"].size();
    std::string versionAfter = stateJsonAfter.value("version", "unknown");
    int frameCountAfter = stateJsonAfter.value("frameCount", 0);

    std::cout << "\nState snapshot AFTER reload:\n";
    std::cout << "  Version:    " << versionAfter << "\n";
    std::cout << "  Tank count: " << tankCountAfter << "\n";
    std::cout << "  Frame:      " << frameCountAfter << "\n\n";

    // Vérification 1: Nombre de tanks
    ASSERT_EQ(tankCountAfter, 50, "Should still have 50 tanks after reload");
    reporter.addAssertion("tank_count_preserved", tankCountAfter == 50);

    // Vérification 2: Version mise à jour
    bool versionUpdated = versionAfter.find("v2.0") != std::string::npos;
    ASSERT_TRUE(versionUpdated, "Version should be updated to v2.0");
    reporter.addAssertion("version_updated", versionUpdated);

    // Vérification 3: Frame count préservé
    ASSERT_EQ(frameCountAfter, frameCountBefore, "Frame count should be preserved");
    reporter.addAssertion("framecount_preserved", frameCountAfter == frameCountBefore);

    std::cout << "  ✓ State preserved correctly\n";

    // Ré-enregistrer module
    moduleSystem->registerModule("TankModule", std::move(tankModule));

    // === PHASE 2: Post-Reload (15s = 900 frames) ===
    std::cout << "\nPhase 2: Running 15s after reload...\n";

    for (int frame = 0; frame < 900; frame++) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        moduleSystem->processModules(1.0f / 60.0f);

        auto frameEnd = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();

        metrics.recordFPS(1000.0f / frameTime);

        if (frame % 60 == 0) {
            metrics.recordMemoryUsage(getCurrentMemoryUsage());
        }

        if (frame % 300 == 0) {
            std::cout << "  Frame " << frame << "/900\n";
        }
    }

    // === VÉRIFICATIONS FINALES ===
    std::cout << "\nFinal verifications...\n";

    // Memory growth
    size_t memGrowth = metrics.getMemoryGrowth();
    float memGrowthMB = memGrowth / (1024.0f * 1024.0f);
    ASSERT_LT(memGrowthMB, 5.0f, "Memory growth should be < 5MB");
    reporter.addMetric("memory_growth_mb", memGrowthMB);

    // FPS
    float minFPS = metrics.getFPSMin();
    ASSERT_GT(minFPS, 30.0f, "Min FPS should be > 30");
    reporter.addMetric("fps_min", minFPS);
    reporter.addMetric("fps_avg", metrics.getFPSAvg());
    reporter.addMetric("fps_max", metrics.getFPSMax());

    // Reload time
    ASSERT_LT(reloadTime, 1000.0f, "Reload time should be < 1000ms");

    // No crashes
    reporter.addAssertion("no_crashes", true);

    // === CLEANUP ===
    std::cout << "\nCleaning up...\n";

    // Restaurer version originale (HEADER)
    std::ifstream inputRestore("../../tests/modules/TankModule.h");
    std::string contentRestore((std::istreambuf_iterator<char>(inputRestore)), std::istreambuf_iterator<char>());
    inputRestore.close();

    // Même correctif qu'à l'aller : la chaîne v2.0 fait 48 caractères, l'ancien
    // replace(pos, 50, …) en mangeait 2 de trop. On réutilise hotVersion.length().
    const std::string hotVersion = "std::string moduleVersion = \"v2.0 HOT-RELOADED\";";
    pos = contentRestore.find(hotVersion);
    if (pos != std::string::npos) {
        contentRestore.replace(pos, hotVersion.length(), "std::string moduleVersion = \"v1.0\";");
    }

    std::ofstream outputRestore("../../tests/modules/TankModule.h");
    outputRestore << contentRestore;
    outputRestore.close();

    // Rebuild to restore original version (test runs from build/tests/).
    // Same cmake --build approach used during the hot-reload step above.
    {
        const char* cmakeExe = std::getenv("GROVE_CMAKE_COMMAND");
        std::string cmakeCmd = cmakeExe ? cmakeExe : "cmake";
        std::string buildCmd = "\"" + cmakeCmd + "\"" +
                               " --build .." +
                               " --target TankModule";
#ifdef _WIN32
        buildCmd += " > NUL 2>&1";
#else
        buildCmd += " > /dev/null 2>&1";
#endif
        system(buildCmd.c_str());
    }

    // === RAPPORTS ===
    std::cout << "\n";
    metrics.printReport();
    reporter.printFinalReport();

    return reporter.getExitCode();
}
