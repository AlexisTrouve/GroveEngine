# Scénario 2: Chaos Monkey

**Priorité**: ⭐⭐⭐ CRITIQUE
**Phase**: 1 (MUST HAVE)
**Durée estimée**: ~5 minutes
**Effort implémentation**: ~6-8 heures

---

## 🎯 Objectif

Valider la robustesse du système face à des failures aléatoires et sa capacité à:
- Détecter les crashes
- Récupérer automatiquement
- Maintenir la stabilité mémoire
- Éviter les deadlocks
- Logger les erreurs correctement

**Inspiré de**: Netflix Chaos Monkey (tester en cassant aléatoirement)

---

## 📋 Description

### Principe
Exécuter l'engine pendant 5 minutes avec un module qui génère des events aléatoires de failure:
- 30% chance de hot-reload par seconde
- 10% chance de crash dans `process()`
- 10% chance de state corrompu
- 5% chance de config invalide
- 45% chance de fonctionnement normal

### Comportement Attendu
L'engine doit:
1. Détecter chaque crash
2. Logger l'erreur avec stack trace
3. Récupérer en rechargeant le module
4. Continuer l'exécution
5. Ne jamais deadlock
6. Memory usage stable (< 10MB growth)

---

## 🏗️ Implémentation

### ChaosModule Structure

```cpp
// ChaosModule.h
class ChaosModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return !isProcessing; }

private:
    std::mt19937 rng;
    int frameCount = 0;
    int crashCount = 0;
    int corruptionCount = 0;
    bool isProcessing = false;

    // Configuration du chaos
    float hotReloadProbability = 0.30f;
    float crashProbability = 0.10f;
    float corruptionProbability = 0.10f;
    float invalidConfigProbability = 0.05f;

    // Simulations de failures
    void maybeHotReload();
    void maybeCrash();
    void maybeCorruptState();
    void maybeInvalidConfig();
};
```

### Process Logic

```cpp
void ChaosModule::process(float deltaTime) {
    isProcessing = true;
    frameCount++;

    // Générer event aléatoire (1 fois par seconde = 60 frames)
    if (frameCount % 60 == 0) {
        float roll = (rng() % 100) / 100.0f;

        if (roll < hotReloadProbability) {
            // Signal pour hot-reload (via flag dans state)
            logger->info("🎲 Chaos: Triggering HOT-RELOAD");
            // Note: Le test externe déclenchera le reload
        }
        else if (roll < hotReloadProbability + crashProbability) {
            // CRASH INTENTIONNEL
            logger->warn("🎲 Chaos: Triggering CRASH");
            crashCount++;
            throw std::runtime_error("Intentional crash for chaos testing");
        }
        else if (roll < hotReloadProbability + crashProbability + corruptionProbability) {
            // CORRUPTION DE STATE
            logger->warn("🎲 Chaos: Corrupting STATE");
            corruptionCount++;
            // Modifier state de façon invalide (sera détecté à getState)
        }
        else if (roll < hotReloadProbability + crashProbability + corruptionProbability + invalidConfigProbability) {
            // CONFIG INVALIDE (simulé)
            logger->warn("🎲 Chaos: Invalid CONFIG request");
            // Le test externe tentera setState avec config invalide
        }
        // Sinon: fonctionnement normal (45%)
    }

    isProcessing = false;
}
```

### State Format

```json
{
    "frameCount": 18000,
    "crashCount": 12,
    "corruptionCount": 8,
    "hotReloadCount": 90,
    "seed": 42,
    "isCorrupted": false
}
```

### Test Principal

```cpp
// test_02_chaos_monkey.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestReporter.h"
#include <csignal>

// Global pour catch segfault
static bool engineCrashed = false;
void signalHandler(int signal) {
    if (signal == SIGSEGV || signal == SIGABRT) {
        engineCrashed = true;
    }
}

int main() {
    TestReporter reporter("Chaos Monkey");
    TestMetrics metrics;

    // Setup signal handlers
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);

    // === SETUP ===
    DebugEngine engine;
    engine.loadModule("ChaosModule", "build/modules/libChaosModule.so");

    auto config = createJsonConfig({
        {"seed", 42},  // Reproductible
        {"hotReloadProbability", 0.30},
        {"crashProbability", 0.10},
        {"corruptionProbability", 0.10},
        {"invalidConfigProbability", 0.05}
    });

    engine.initializeModule("ChaosModule", config);

    // === CHAOS LOOP (5 minutes = 18000 frames) ===
    std::cout << "Starting Chaos Monkey (5 minutes)...\n";

    int totalFrames = 18000; // 5 * 60 * 60
    int crashesDetected = 0;
    int reloadsTriggered = 0;
    int recoverySuccesses = 0;
    bool hadDeadlock = false;

    auto testStart = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < totalFrames; frame++) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        try {
            // Update engine
            engine.update(1.0f / 60.0f);

            // Check si le module demande un hot-reload
            auto state = engine.getModuleState("ChaosModule");
            auto* jsonNode = dynamic_cast<JsonDataNode*>(state.get());
            const auto& stateJson = jsonNode->getJsonData();

            // Simuler hot-reload si demandé (aléatoire 30%)
            if (frame % 60 == 0) { // Check toutes les secondes
                float roll = (rand() % 100) / 100.0f;
                if (roll < 0.30f) {
                    std::cout << "  [Frame " << frame << "] Hot-reload triggered\n";

                    auto reloadStart = std::chrono::high_resolution_clock::now();

                    engine.reloadModule("ChaosModule");
                    reloadsTriggered++;

                    auto reloadEnd = std::chrono::high_resolution_clock::now();
                    float reloadTime = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();
                    metrics.recordReloadTime(reloadTime);
                }
            }

        } catch (const std::exception& e) {
            // CRASH DÉTECTÉ
            crashesDetected++;
            std::cout << "  [Frame " << frame << "] ⚠️  Crash detected: " << e.what() << "\n";

            // Tentative de recovery
            try {
                std::cout << "  [Frame " << frame << "] 🔄 Attempting recovery...\n";

                // Recharger le module
                engine.reloadModule("ChaosModule");

                // Réinitialiser avec state par défaut
                engine.initializeModule("ChaosModule", config);

                recoverySuccesses++;
                std::cout << "  [Frame " << frame << "] ✅ Recovery successful\n";

            } catch (const std::exception& recoveryError) {
                std::cout << "  [Frame " << frame << "] ❌ Recovery FAILED: " << recoveryError.what() << "\n";
                reporter.addAssertion("recovery_failed", false);
                break; // Arrêter le test
            }
        }

        // Métriques
        auto frameEnd = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
        metrics.recordFPS(1000.0f / frameTime);
        metrics.recordMemoryUsage(getCurrentMemoryUsage());

        // Deadlock detection (frame > 100ms)
        if (frameTime > 100.0f) {
            std::cout << "  [Frame " << frame << "] ⚠️  Potential deadlock (frame time: " << frameTime << "ms)\n";
            hadDeadlock = true;
        }

        // Progress (toutes les 3000 frames = 50s)
        if (frame % 3000 == 0 && frame > 0) {
            float elapsedMin = frame / 3600.0f;
            std::cout << "Progress: " << elapsedMin << "/5.0 minutes (" << (frame * 100 / totalFrames) << "%)\n";
        }
    }

    auto testEnd = std::chrono::high_resolution_clock::now();
    float totalDuration = std::chrono::duration<float>(testEnd - testStart).count();

    // === VÉRIFICATIONS FINALES ===

    // Engine toujours vivant
    bool engineAlive = !engineCrashed;
    ASSERT_TRUE(engineAlive, "Engine should still be alive");
    reporter.addAssertion("engine_alive", engineAlive);

    // Pas de deadlocks
    ASSERT_FALSE(hadDeadlock, "Should not have deadlocks");
    reporter.addAssertion("no_deadlocks", !hadDeadlock);

    // Recovery rate > 95%
    float recoveryRate = (crashesDetected > 0) ? (recoverySuccesses * 100.0f / crashesDetected) : 100.0f;
    ASSERT_GT(recoveryRate, 95.0f, "Recovery rate should be > 95%");
    reporter.addMetric("recovery_rate_percent", recoveryRate);

    // Memory growth < 10MB
    size_t memGrowth = metrics.getMemoryGrowth();
    ASSERT_LT(memGrowth, 10 * 1024 * 1024, "Memory growth should be < 10MB");
    reporter.addMetric("memory_growth_mb", memGrowth / (1024.0f * 1024.0f));

    // Durée totale proche de 5 minutes (tolérance ±10s)
    ASSERT_WITHIN(totalDuration, 300.0f, 10.0f, "Total duration should be ~5 minutes");
    reporter.addMetric("total_duration_sec", totalDuration);

    // Statistiques
    reporter.addMetric("crashes_detected", crashesDetected);
    reporter.addMetric("reloads_triggered", reloadsTriggered);
    reporter.addMetric("recovery_successes", recoverySuccesses);

    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "CHAOS MONKEY STATISTICS\n";
    std::cout << "================================================================================\n";
    std::cout << "  Total frames:        " << totalFrames << "\n";
    std::cout << "  Duration:            " << totalDuration << "s\n";
    std::cout << "  Crashes detected:    " << crashesDetected << "\n";
    std::cout << "  Reloads triggered:   " << reloadsTriggered << "\n";
    std::cout << "  Recovery successes:  " << recoverySuccesses << "\n";
    std::cout << "  Recovery rate:       " << recoveryRate << "%\n";
    std::cout << "  Memory growth:       " << (memGrowth / (1024.0f * 1024.0f)) << " MB\n";
    std::cout << "  Had deadlocks:       " << (hadDeadlock ? "YES ❌" : "NO ✅") << "\n";
    std::cout << "================================================================================\n\n";

    // === RAPPORT FINAL ===
    metrics.printReport();
    reporter.printFinalReport();

    return reporter.getExitCode();
}
```

---

## 📊 Métriques Collectées

| Métrique | Description | Seuil |
|----------|-------------|-------|
| **engine_alive** | Engine toujours vivant à la fin | true |
| **no_deadlocks** | Aucun deadlock détecté | true |
| **recovery_rate_percent** | % de crashes récupérés | > 95% |
| **memory_growth_mb** | Croissance mémoire totale | < 10MB |
| **total_duration_sec** | Durée totale du test | ~300s (±10s) |
| **crashes_detected** | Nombre de crashes détectés | N/A (info) |
| **reloads_triggered** | Nombre de hot-reloads | ~90 (30% * 300s) |
| **recovery_successes** | Nombre de recoveries réussies | ~crashes_detected |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ Engine toujours vivant après 5 minutes
2. ✅ Aucun deadlock
3. ✅ Recovery rate > 95%
4. ✅ Memory growth < 10MB
5. ✅ Logs contiennent tous les crashes (avec stack trace si possible)

### NICE TO HAVE
1. ✅ Recovery rate = 100% (aucun échec)
2. ✅ Memory growth < 5MB
3. ✅ Reload time moyen < 500ms même pendant chaos

---

## 🔧 Recovery Strategy

### Dans DebugEngine::update()

```cpp
void DebugEngine::update(float deltaTime) {
    try {
        moduleSystem->update(deltaTime);

    } catch (const std::exception& e) {
        // CRASH DÉTECTÉ
        logger->error("❌ Module crashed: {}", e.what());

        // Tentative de recovery automatique
        try {
            logger->info("🔄 Attempting automatic recovery...");

            // 1. Extraire le module défaillant
            auto failedModule = moduleSystem->extractModule();

            // 2. Recharger depuis .so
            std::string moduleName = "ChaosModule"; // À généraliser
            reloadModule(moduleName);

            // 3. Réinitialiser avec config par défaut
            auto defaultConfig = createDefaultConfig();
            auto newModule = moduleLoader->getModule(moduleName);
            newModule->initialize(defaultConfig);

            // 4. Ré-enregistrer
            moduleSystem->registerModule(moduleName, std::move(newModule));

            logger->info("✅ Recovery successful");

        } catch (const std::exception& recoveryError) {
            logger->critical("❌ Recovery failed: {}", recoveryError.what());
            throw; // Re-throw si recovery impossible
        }
    }
}
```

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| Crash non récupéré | Recovery logic bugué | FAIL - fixer recovery |
| Deadlock | Mutex lock dans crash handler | FAIL - review locks |
| Memory leak > 10MB | Module pas correctement nettoyé | FAIL - fix destructors |
| Duration >> 300s | Reloads trop longs | WARNING - optimiser |

---

## 📝 Output Attendu

```
================================================================================
TEST: Chaos Monkey
================================================================================

Starting Chaos Monkey (5 minutes)...

  [Frame 60] Hot-reload triggered
  [Frame 180] ⚠️  Crash detected: Intentional crash for chaos testing
  [Frame 180] 🔄 Attempting recovery...
  [Frame 180] ✅ Recovery successful
  [Frame 240] Hot-reload triggered
  ...
Progress: 1.0/5.0 minutes (20%)
  [Frame 3420] ⚠️  Crash detected: Intentional crash for chaos testing
  [Frame 3420] 🔄 Attempting recovery...
  [Frame 3420] ✅ Recovery successful
  ...
Progress: 5.0/5.0 minutes (100%)

================================================================================
CHAOS MONKEY STATISTICS
================================================================================
  Total frames:        18000
  Duration:            302.4s
  Crashes detected:    28
  Reloads triggered:   89
  Recovery successes:  28
  Recovery rate:       100%
  Memory growth:       3.2 MB
  Had deadlocks:       NO ✅
================================================================================

METRICS
================================================================================
  Engine alive:        true           ✓
  No deadlocks:        true           ✓
  Recovery rate:       100%           (threshold: > 95%) ✓
  Memory growth:       3.2 MB         (threshold: < 10MB) ✓
  Total duration:      302.4s         (expected: ~300s) ✓

ASSERTIONS
================================================================================
  ✓ engine_alive
  ✓ no_deadlocks
  ✓ recovery_rate > 95%
  ✓ memory_stable

Result: ✅ PASSED

================================================================================
```

---

## 📅 Planning

**Jour 1 (4h):**
- Implémenter ChaosModule avec probabilités configurables
- Implémenter recovery logic dans DebugEngine

**Jour 2 (4h):**
- Implémenter test_02_chaos_monkey.cpp
- Signal handling (SIGSEGV, SIGABRT)
- Deadlock detection
- Debug + validation

---

**Prochaine étape**: `scenario_03_stress_test.md`
