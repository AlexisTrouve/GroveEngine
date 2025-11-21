# Scénario 1: Production Hot-Reload

**Priorité**: ⭐⭐⭐ CRITIQUE
**Phase**: 1 (MUST HAVE)
**Durée estimée**: ~30 secondes
**Effort implémentation**: ~4-6 heures

---

## 🎯 Objectif

Valider que le système de hot-reload fonctionne en conditions réelles de production avec:
- State complexe (positions, vitesses, cooldowns, ordres)
- Multiples entités actives (50 tanks)
- Reload pendant l'exécution (mid-frame)
- Préservation exacte de l'état

---

## 📋 Description

### Setup Initial
1. Charger `TankModule` avec configuration initiale
2. Spawner 50 tanks avec:
   - Position aléatoire dans une grille 100x100
   - Vélocité aléatoire (vx, vy) entre -5 et +5
   - Cooldown de tir aléatoire entre 0 et 5 secondes
   - Ordre de mouvement vers une destination aléatoire
3. Exécuter pendant 15 secondes (900 frames à 60 FPS)

### Trigger Hot-Reload
1. À la frame 900 (t=15s):
   - Extraire state du module via `getState()`
   - Décharger le module (dlclose)
   - Recompiler le module avec version modifiée (v2.0)
   - Charger nouveau module (dlopen)
   - Restaurer state via `setState()`
2. Mesurer temps total de reload

### Vérification Post-Reload
1. Continuer exécution pendant 15 secondes supplémentaires
2. Vérifier à chaque frame:
   - Nombre de tanks = 50
   - Positions dans les limites attendues
   - Vélocités préservées
   - Cooldowns continuent de décrémenter
   - Ordres de mouvement toujours actifs

---

## 🏗️ Implémentation

### TankModule Structure

```cpp
// TankModule.h
class TankModule : public IModule {
public:
    struct Tank {
        float x, y;           // Position
        float vx, vy;         // Vélocité
        float cooldown;       // Temps avant prochain tir
        float targetX, targetY; // Destination
        int id;               // Identifiant unique
    };

    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

private:
    std::vector<Tank> tanks;
    int frameCount = 0;
    std::string version = "v1.0";

    void updateTank(Tank& tank, float dt);
    void spawnTanks(int count);
};
```

### State Format (JSON)

```json
{
    "version": "v1.0",
    "frameCount": 900,
    "tanks": [
        {
            "id": 0,
            "x": 23.45,
            "y": 67.89,
            "vx": 2.3,
            "vy": -1.7,
            "cooldown": 2.4,
            "targetX": 80.0,
            "targetY": 50.0
        },
        // ... 49 autres tanks
    ]
}
```

### Test Principal

```cpp
// test_01_production_hotreload.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestAssertions.h"
#include "helpers/TestReporter.h"

int main() {
    TestReporter reporter("Production Hot-Reload");
    TestMetrics metrics;

    // === SETUP ===
    DebugEngine engine;
    engine.loadModule("TankModule", "build/modules/libTankModule.so");

    // Config initiale
    auto config = createJsonConfig({
        {"version", "v1.0"},
        {"tankCount", 50},
        {"mapSize", 100}
    });
    engine.initializeModule("TankModule", config);

    // === PHASE 1: Pre-Reload (15s) ===
    std::cout << "Phase 1: Running 15s before reload...\n";

    for (int i = 0; i < 900; i++) { // 15s * 60 FPS
        auto start = std::chrono::high_resolution_clock::now();

        engine.update(1.0f/60.0f);

        auto end = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(end - start).count();

        metrics.recordFPS(1000.0f / frameTime);
        metrics.recordMemoryUsage(getCurrentMemoryUsage());
    }

    // Snapshot state AVANT reload
    auto preReloadState = engine.getModuleState("TankModule");
    auto* jsonNode = dynamic_cast<JsonDataNode*>(preReloadState.get());
    const auto& stateJson = jsonNode->getJsonData();

    int tankCountBefore = stateJson["tanks"].size();
    ASSERT_EQ(tankCountBefore, 50, "Should have 50 tanks before reload");

    // Sauvegarder positions pour comparaison
    std::vector<std::pair<float, float>> positionsBefore;
    for (const auto& tank : stateJson["tanks"]) {
        positionsBefore.push_back({tank["x"], tank["y"]});
    }

    // === HOT-RELOAD ===
    std::cout << "Triggering hot-reload...\n";

    auto reloadStart = std::chrono::high_resolution_clock::now();

    // Modifier la version dans le code source (simulé)
    modifySourceFile("tests/modules/TankModule.cpp", "v1.0", "v2.0 HOT-RELOADED");

    // Recompiler
    int result = system("cmake --build build --target TankModule 2>&1 | grep -v '^\\['");
    ASSERT_EQ(result, 0, "Compilation should succeed");

    // Le FileWatcher va détecter et recharger automatiquement
    // On attend que le reload soit fait
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Ou déclencher manuellement
    engine.reloadModule("TankModule");

    auto reloadEnd = std::chrono::high_resolution_clock::now();
    float reloadTime = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();

    metrics.recordReloadTime(reloadTime);
    reporter.addMetric("reload_time_ms", reloadTime);

    // === PHASE 2: Post-Reload (15s) ===
    std::cout << "Phase 2: Running 15s after reload...\n";

    // Vérifier state immédiatement après reload
    auto postReloadState = engine.getModuleState("TankModule");
    auto* jsonNodePost = dynamic_cast<JsonDataNode*>(postReloadState.get());
    const auto& stateJsonPost = jsonNodePost->getJsonData();

    // Vérification 1: Nombre de tanks
    int tankCountAfter = stateJsonPost["tanks"].size();
    ASSERT_EQ(tankCountAfter, 50, "Should still have 50 tanks after reload");
    reporter.addAssertion("tank_count_preserved", tankCountAfter == 50);

    // Vérification 2: Version mise à jour
    std::string versionAfter = stateJsonPost["version"];
    ASSERT_TRUE(versionAfter.find("v2.0") != std::string::npos, "Version should be updated");
    reporter.addAssertion("version_updated", versionAfter.find("v2.0") != std::string::npos);

    // Vérification 3: Positions préservées (tolérance 0.01)
    for (size_t i = 0; i < 50; i++) {
        float xBefore = positionsBefore[i].first;
        float yBefore = positionsBefore[i].second;
        float xAfter = stateJsonPost["tanks"][i]["x"];
        float yAfter = stateJsonPost["tanks"][i]["y"];

        // Tolérance: pendant le reload, ~500ms se sont écoulées
        // Les tanks ont bougé de velocity * 0.5s
        float maxMovement = 5.0f * 0.5f; // velocity max * temps max
        float distance = std::sqrt(std::pow(xAfter - xBefore, 2) + std::pow(yAfter - yBefore, 2));

        ASSERT_LT(distance, maxMovement + 0.1f, "Tank position should be preserved within movement tolerance");
    }
    reporter.addAssertion("positions_preserved", true);

    // Continuer exécution
    for (int i = 0; i < 900; i++) { // 15s * 60 FPS
        auto start = std::chrono::high_resolution_clock::now();

        engine.update(1.0f/60.0f);

        auto end = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(end - start).count();

        metrics.recordFPS(1000.0f / frameTime);
        metrics.recordMemoryUsage(getCurrentMemoryUsage());
    }

    // === VÉRIFICATIONS FINALES ===

    // Memory growth
    size_t memGrowth = metrics.getMemoryGrowth();
    ASSERT_LT(memGrowth, 5 * 1024 * 1024, "Memory growth should be < 5MB");
    reporter.addMetric("memory_growth_mb", memGrowth / (1024.0f * 1024.0f));

    // FPS
    float minFPS = metrics.getFPSMin();
    ASSERT_GT(minFPS, 30.0f, "Min FPS should be > 30");
    reporter.addMetric("fps_min", minFPS);
    reporter.addMetric("fps_avg", metrics.getFPSAvg());

    // No crashes
    reporter.addAssertion("no_crashes", true);

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
| **reload_time_ms** | Temps total du hot-reload | < 1000ms |
| **memory_growth_mb** | Croissance mémoire (final - initial) | < 5MB |
| **fps_min** | FPS minimum observé | > 30 |
| **fps_avg** | FPS moyen sur 30s | ~60 |
| **tank_count_preserved** | Nombre de tanks identique avant/après | 50/50 |
| **positions_preserved** | Positions dans tolérance | 100% |
| **version_updated** | Version du module mise à jour | true |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ Reload time < 1000ms
2. ✅ Memory growth < 5MB
3. ✅ FPS min > 30
4. ✅ 50 tanks présents avant ET après reload
5. ✅ Positions préservées (distance < velocity * reloadTime)
6. ✅ Aucun crash

### NICE TO HAVE
1. ✅ Reload time < 500ms (optimal)
2. ✅ FPS min > 50 (très fluide)
3. ✅ Memory growth < 1MB (quasi stable)

---

## 🔧 Helpers Nécessaires

### TestMetrics.h
```cpp
class TestMetrics {
    std::vector<float> fpsValues;
    std::vector<size_t> memoryValues;
    std::vector<float> reloadTimes;
    size_t initialMemory;

public:
    void recordFPS(float fps) { fpsValues.push_back(fps); }
    void recordMemoryUsage(size_t bytes) { memoryValues.push_back(bytes); }
    void recordReloadTime(float ms) { reloadTimes.push_back(ms); }

    float getFPSMin() const { return *std::min_element(fpsValues.begin(), fpsValues.end()); }
    float getFPSAvg() const { return std::accumulate(fpsValues.begin(), fpsValues.end(), 0.0f) / fpsValues.size(); }
    size_t getMemoryGrowth() const { return memoryValues.back() - initialMemory; }

    void printReport() const;
};
```

### Utility Functions
```cpp
size_t getCurrentMemoryUsage() {
    // Linux: /proc/self/status
    std::ifstream file("/proc/self/status");
    std::string line;
    while (std::getline(file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            size_t kb = std::stoi(line.substr(7));
            return kb * 1024;
        }
    }
    return 0;
}

void modifySourceFile(const std::string& path, const std::string& oldStr, const std::string& newStr) {
    std::ifstream input(path);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();

    size_t pos = content.find(oldStr);
    if (pos != std::string::npos) {
        content.replace(pos, oldStr.length(), newStr);
    }

    std::ofstream output(path);
    output << content;
}
```

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| Tank count mismatch | État corrompu | FAIL - état mal sauvegardé/restauré |
| Position out of bounds | Calcul incorrect | FAIL - bug dans updateTank() |
| Reload time > 1s | Compilation lente | FAIL - optimiser build |
| Memory growth > 5MB | Memory leak | FAIL - vérifier destructeurs |
| FPS < 30 | Reload bloque trop | FAIL - optimiser waitForCleanState |

---

## 📝 Output Attendu

```
================================================================================
TEST: Production Hot-Reload
================================================================================

Phase 1: Running 15s before reload...
[900 frames processed]

State snapshot:
  - Tanks: 50
  - Version: v1.0
  - Frame: 900

Triggering hot-reload...
[Compilation OK]
[Reload completed in 487ms]

Verification:
  ✓ Tank count: 50/50
  ✓ Version updated: v2.0 HOT-RELOADED
  ✓ Positions preserved: 50/50 (max error: 0.003)

Phase 2: Running 15s after reload...
[900 frames processed]

================================================================================
METRICS
================================================================================
  Reload time:     487ms          (threshold: < 1000ms) ✓
  Memory growth:   2.3MB          (threshold: < 5MB)    ✓
  FPS min:         58             (threshold: > 30)     ✓
  FPS avg:         60
  FPS max:         62

================================================================================
ASSERTIONS
================================================================================
  ✓ tank_count_preserved
  ✓ version_updated
  ✓ positions_preserved
  ✓ no_crashes

Result: ✅ PASSED

================================================================================
```

---

## 📅 Planning

**Jour 1 (4h):**
- Implémenter TankModule avec state complexe
- Implémenter helpers (TestMetrics, assertions)

**Jour 2 (2h):**
- Implémenter test_01_production_hotreload.cpp
- Debug + validation

---

**Prochaine étape**: `scenario_02_chaos_monkey.md`
