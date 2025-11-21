# Scénario 4: Race Condition Hunter

**Priorité**: ⭐⭐ IMPORTANT
**Phase**: 2 (SHOULD HAVE)
**Durée estimée**: ~10 minutes (1000 compilations)
**Effort implémentation**: ~6-8 heures

---

## 🎯 Objectif

Détecter et valider la robustesse face aux race conditions lors de la compilation concurrente:
- FileWatcher détecte changements pendant compilation
- File stability check fonctionne
- Aucun .so corrompu chargé
- Aucun deadlock entre threads
- 100% success rate des reloads

**C'est le test qui a motivé le fix de la race condition initiale !**

---

## 📋 Description

### Setup
1. Thread 1 (Compiler): Recompile `TestModule.so` toutes les 300ms
2. Thread 2 (FileWatcher): Détecte changements et déclenche reload
3. Thread 3 (Engine): Exécute `process()` en tight loop à 60 FPS
4. Durée: 1000 cycles de compilation (~5 minutes)

### Comportements à Tester
- **File stability check**: Attend que le fichier soit stable avant reload
- **Size verification**: Vérifie que le .so copié est complet
- **Concurrent access**: Pas de corruption pendant dlopen/dlclose
- **Error handling**: Détecte et récupère des .so incomplets

---

## 🏗️ Implémentation

### AutoCompiler Helper

```cpp
// helpers/AutoCompiler.h
class AutoCompiler {
public:
    AutoCompiler(const std::string& moduleName, const std::string& buildDir)
        : moduleName(moduleName), buildDir(buildDir), isRunning(false) {}

    void start(int iterations, int intervalMs) {
        isRunning = true;
        compilerThread = std::thread([this, iterations, intervalMs]() {
            for (int i = 0; i < iterations && isRunning; i++) {
                compile(i);
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            }
        });
    }

    void stop() {
        isRunning = false;
        if (compilerThread.joinable()) {
            compilerThread.join();
        }
    }

    int getSuccessCount() const { return successCount; }
    int getFailureCount() const { return failureCount; }
    int getCurrentIteration() const { return currentIteration; }

private:
    std::string moduleName;
    std::string buildDir;
    std::atomic<bool> isRunning;
    std::atomic<int> successCount{0};
    std::atomic<int> failureCount{0};
    std::atomic<int> currentIteration{0};
    std::thread compilerThread;

    void compile(int iteration) {
        currentIteration = iteration;

        // Modifier source pour forcer recompilation
        modifySourceVersion(iteration);

        // Compiler
        std::string cmd = "cmake --build " + buildDir + " --target " + moduleName + " 2>&1";
        int result = system(cmd.c_str());

        if (result == 0) {
            successCount++;
        } else {
            failureCount++;
            std::cerr << "Compilation failed at iteration " << iteration << "\n";
        }
    }

    void modifySourceVersion(int iteration) {
        // Modifier TestModule.cpp pour changer version
        std::string sourcePath = buildDir + "/../tests/modules/TestModule.cpp";
        std::ifstream input(sourcePath);
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();

        // Remplacer version
        std::regex versionRegex(R"(moduleVersion = "v[0-9]+")");
        std::string newVersion = "moduleVersion = \"v" + std::to_string(iteration) + "\"";
        content = std::regex_replace(content, versionRegex, newVersion);

        std::ofstream output(sourcePath);
        output << content;
    }
};
```

### Test Principal

```cpp
// test_04_race_condition.cpp
#include "helpers/AutoCompiler.h"
#include "helpers/TestMetrics.h"
#include "helpers/TestReporter.h"
#include <atomic>
#include <thread>

int main() {
    TestReporter reporter("Race Condition Hunter");
    TestMetrics metrics;

    const int TOTAL_COMPILATIONS = 1000;
    const int COMPILE_INTERVAL_MS = 300;

    std::cout << "================================================================================\n";
    std::cout << "RACE CONDITION HUNTER: " << TOTAL_COMPILATIONS << " compilations\n";
    std::cout << "================================================================================\n\n";

    // === SETUP ===
    DebugEngine engine;
    engine.loadModule("TestModule", "build/modules/libTestModule.so");

    auto config = createJsonConfig({{"version", "v0"}});
    engine.initializeModule("TestModule", config);

    // === STATISTIQUES ===
    std::atomic<int> reloadAttempts{0};
    std::atomic<int> reloadSuccesses{0};
    std::atomic<int> reloadFailures{0};
    std::atomic<int> corruptedLoads{0};
    std::atomic<int> crashes{0};
    std::atomic<bool> engineRunning{true};

    // === THREAD 1: Auto-Compiler ===
    std::cout << "Starting auto-compiler (300ms interval)...\n";
    AutoCompiler compiler("TestModule", "build");
    compiler.start(TOTAL_COMPILATIONS, COMPILE_INTERVAL_MS);

    // === THREAD 2: FileWatcher + Reload ===
    std::cout << "Starting FileWatcher...\n";
    std::thread watcherThread([&]() {
        std::string soPath = "build/modules/libTestModule.so";
        std::filesystem::file_time_type lastWriteTime;

        try {
            lastWriteTime = std::filesystem::last_write_time(soPath);
        } catch (...) {
            std::cerr << "Failed to get initial file time\n";
            return;
        }

        while (engineRunning && compiler.getCurrentIteration() < TOTAL_COMPILATIONS) {
            try {
                auto currentWriteTime = std::filesystem::last_write_time(soPath);

                if (currentWriteTime != lastWriteTime) {
                    // FICHIER MODIFIÉ - RELOAD
                    reloadAttempts++;

                    std::cout << "[Compilation #" << compiler.getCurrentIteration()
                              << "] File changed, triggering reload...\n";

                    auto reloadStart = std::chrono::high_resolution_clock::now();

                    try {
                        // Le ModuleLoader va attendre file stability
                        engine.reloadModule("TestModule");

                        auto reloadEnd = std::chrono::high_resolution_clock::now();
                        float reloadTime = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();
                        metrics.recordReloadTime(reloadTime);

                        reloadSuccesses++;

                        // Vérifier que le module est valide
                        auto state = engine.getModuleState("TestModule");
                        auto* jsonNode = dynamic_cast<JsonDataNode*>(state.get());
                        const auto& stateJson = jsonNode->getJsonData();

                        std::string version = stateJson["version"];
                        std::cout << "  → Reload OK (" << reloadTime << "ms), version: " << version << "\n";

                    } catch (const std::exception& e) {
                        reloadFailures++;
                        std::cerr << "  → Reload FAILED: " << e.what() << "\n";

                        // Vérifier si c'est un .so corrompu
                        if (std::string(e.what()).find("Incomplete") != std::string::npos ||
                            std::string(e.what()).find("dlopen") != std::string::npos) {
                            corruptedLoads++;
                        }
                    }

                    lastWriteTime = currentWriteTime;
                }

            } catch (const std::filesystem::filesystem_error& e) {
                // Fichier en cours d'écriture, ignore
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // === THREAD 3: Engine Loop ===
    std::cout << "Starting engine loop (60 FPS)...\n";
    std::thread engineThread([&]() {
        int frame = 0;

        while (engineRunning && compiler.getCurrentIteration() < TOTAL_COMPILATIONS) {
            auto frameStart = std::chrono::high_resolution_clock::now();

            try {
                engine.update(1.0f / 60.0f);

                // Métriques
                auto frameEnd = std::chrono::high_resolution_clock::now();
                float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
                metrics.recordFPS(1000.0f / frameTime);

                if (frame % 60 == 0) {
                    metrics.recordMemoryUsage(getCurrentMemoryUsage());
                }

            } catch (const std::exception& e) {
                crashes++;
                std::cerr << "[Frame " << frame << "] ENGINE CRASH: " << e.what() << "\n";
                // Continue malgré le crash (test robustesse)
            }

            frame++;

            // Sleep pour maintenir 60 FPS
            auto frameEnd = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
            int sleepMs = std::max(0, static_cast<int>(16.67f - elapsed));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    });

    // === ATTENDRE FIN ===
    std::cout << "\nRunning test...\n";

    // Progress monitoring
    while (compiler.getCurrentIteration() < TOTAL_COMPILATIONS) {
        std::this_thread::sleep_for(std::chrono::seconds(10));

        int progress = (compiler.getCurrentIteration() * 100) / TOTAL_COMPILATIONS;
        std::cout << "Progress: " << progress << "% ("
                  << compiler.getCurrentIteration() << "/" << TOTAL_COMPILATIONS << " compilations)\n";
        std::cout << "  Reloads: " << reloadSuccesses << " OK, " << reloadFailures << " FAIL\n";
        std::cout << "  Corrupted loads: " << corruptedLoads << "\n";
        std::cout << "  Crashes: " << crashes << "\n\n";
    }

    // Stop tous les threads
    engineRunning = false;
    compiler.stop();
    watcherThread.join();
    engineThread.join();

    std::cout << "\nAll threads stopped.\n\n";

    // === VÉRIFICATIONS FINALES ===

    int compileSuccesses = compiler.getSuccessCount();
    int compileFailures = compiler.getFailureCount();

    float compileSuccessRate = (compileSuccesses * 100.0f) / TOTAL_COMPILATIONS;
    float reloadSuccessRate = (reloadAttempts > 0) ? (reloadSuccesses * 100.0f / reloadAttempts) : 100.0f;

    // Assertions
    ASSERT_GT(compileSuccessRate, 95.0f, "Compile success rate should be > 95%");
    reporter.addMetric("compile_success_rate_percent", compileSuccessRate);

    ASSERT_EQ(corruptedLoads, 0, "Should have 0 corrupted loads (file stability check should prevent this)");
    reporter.addMetric("corrupted_loads", corruptedLoads);

    ASSERT_EQ(crashes, 0, "Should have 0 crashes");
    reporter.addMetric("crashes", crashes);

    // Si on a des reloads, vérifier le success rate
    if (reloadAttempts > 0) {
        ASSERT_GT(reloadSuccessRate, 99.0f, "Reload success rate should be > 99%");
    }
    reporter.addMetric("reload_success_rate_percent", reloadSuccessRate);

    // Vérifier que file stability check a fonctionné (temps moyen > 0)
    float avgReloadTime = metrics.getReloadTimeAvg();
    ASSERT_GT(avgReloadTime, 100.0f, "Avg reload time should be > 100ms (file stability wait)");
    reporter.addMetric("reload_time_avg_ms", avgReloadTime);

    reporter.addMetric("total_compilations", TOTAL_COMPILATIONS);
    reporter.addMetric("compile_successes", compileSuccesses);
    reporter.addMetric("compile_failures", compileFailures);
    reporter.addMetric("reload_attempts", static_cast<int>(reloadAttempts));
    reporter.addMetric("reload_successes", static_cast<int>(reloadSuccesses));
    reporter.addMetric("reload_failures", static_cast<int>(reloadFailures));

    // === RAPPORT FINAL ===
    std::cout << "================================================================================\n";
    std::cout << "RACE CONDITION HUNTER SUMMARY\n";
    std::cout << "================================================================================\n";
    std::cout << "Compilations:\n";
    std::cout << "  Total:        " << TOTAL_COMPILATIONS << "\n";
    std::cout << "  Successes:    " << compileSuccesses << " (" << compileSuccessRate << "%)\n";
    std::cout << "  Failures:     " << compileFailures << "\n\n";

    std::cout << "Reloads:\n";
    std::cout << "  Attempts:     " << reloadAttempts << "\n";
    std::cout << "  Successes:    " << reloadSuccesses << " (" << reloadSuccessRate << "%)\n";
    std::cout << "  Failures:     " << reloadFailures << "\n";
    std::cout << "  Corrupted:    " << corruptedLoads << "\n\n";

    std::cout << "Stability:\n";
    std::cout << "  Crashes:      " << crashes << "\n";
    std::cout << "  Reload avg:   " << avgReloadTime << "ms\n";
    std::cout << "================================================================================\n\n";

    reporter.printFinalReport();

    return reporter.getExitCode();
}
```

---

## 📊 Métriques Collectées

| Métrique | Description | Seuil |
|----------|-------------|-------|
| **compile_success_rate_percent** | % de compilations réussies | > 95% |
| **reload_success_rate_percent** | % de reloads réussis | > 99% |
| **corrupted_loads** | Nombre de .so corrompus chargés | 0 |
| **crashes** | Nombre de crashes engine | 0 |
| **reload_time_avg_ms** | Temps moyen de reload | > 100ms (prouve que file stability fonctionne) |
| **reload_attempts** | Nombre de tentatives de reload | N/A (info) |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ Compile success rate > 95%
2. ✅ Corrupted loads = 0 (file stability check marche)
3. ✅ Crashes = 0
4. ✅ Reload success rate > 99%
5. ✅ Reload time avg > 100ms (prouve attente file stability)

### NICE TO HAVE
1. ✅ Compile success rate = 100%
2. ✅ Reload success rate = 100%
3. ✅ Reload time avg < 600ms (efficace malgré stability check)

---

## 🔧 Détection de Corruptions

### Dans ModuleLoader::loadModule()

```cpp
// DÉJÀ IMPLÉMENTÉ - Vérification
auto origSize = std::filesystem::file_size(path);
auto copiedSize = std::filesystem::file_size(tempPath);

if (copiedSize != origSize) {
    logger->error("❌ Incomplete copy: orig={} bytes, copied={} bytes", origSize, copiedSize);
    throw std::runtime_error("Incomplete file copy detected - CORRUPTED");
}

// Tentative dlopen
void* handle = dlopen(tempPath.c_str(), RTLD_NOW | RTLD_LOCAL);
if (!handle) {
    logger->error("❌ dlopen failed: {}", dlerror());
    throw std::runtime_error(std::string("Failed to load module: ") + dlerror());
}
```

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Comportement attendu |
|--------|-------|---------------------|
| Corrupted .so loaded | File stability check raté | FAIL - augmenter stableRequired |
| Reload failure | dlopen pendant write | RETRY - file stability devrait éviter |
| Engine crash | Race dans dlopen/dlclose | FAIL - ajouter mutex |
| High reload time variance | Compilation variable | OK - tant que P99 < seuil |

---

## 📝 Output Attendu

```
================================================================================
RACE CONDITION HUNTER: 1000 compilations
================================================================================

Starting auto-compiler (300ms interval)...
Starting FileWatcher...
Starting engine loop (60 FPS)...

Running test...
[Compilation #3] File changed, triggering reload...
  → Reload OK (487ms), version: v3
[Compilation #7] File changed, triggering reload...
  → Reload OK (523ms), version: v7

Progress: 10% (100/1000 compilations)
  Reloads: 98 OK, 0 FAIL
  Corrupted loads: 0
  Crashes: 0

Progress: 20% (200/1000 compilations)
  Reloads: 195 OK, 2 FAIL
  Corrupted loads: 0
  Crashes: 0

...

Progress: 100% (1000/1000 compilations)
  Reloads: 987 OK, 5 FAIL
  Corrupted loads: 0
  Crashes: 0

All threads stopped.

================================================================================
RACE CONDITION HUNTER SUMMARY
================================================================================
Compilations:
  Total:        1000
  Successes:    998 (99.8%)
  Failures:     2

Reloads:
  Attempts:     992
  Successes:    987 (99.5%)
  Failures:     5
  Corrupted:    0

Stability:
  Crashes:      0
  Reload avg:   505ms
================================================================================

METRICS
================================================================================
  Compile success:   99.8%          (threshold: > 95%)    ✓
  Reload success:    99.5%          (threshold: > 99%)    ✓
  Corrupted loads:   0              (threshold: 0)        ✓
  Crashes:           0              (threshold: 0)        ✓
  Reload time avg:   505ms          (threshold: > 100ms)  ✓

Result: ✅ PASSED

================================================================================
```

---

## 📅 Planning

**Jour 1 (4h):**
- Implémenter AutoCompiler helper
- Source modification automatique (version bump)

**Jour 2 (4h):**
- Implémenter test_04_race_condition.cpp
- Threading (compiler, watcher, engine)
- Synchronisation + safety
- Debug + validation

---

**Prochaine étape**: `scenario_05_multimodule.md`
