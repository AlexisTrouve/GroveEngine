# Scénario 3: Stress Test Long-Running

**Priorité**: ⭐⭐⭐ CRITIQUE
**Phase**: 1 (MUST HAVE)
**Durée estimée**: ~10 minutes (extensible à 1h pour nightly)
**Effort implémentation**: ~4-6 heures

---

## 🎯 Objectif

Valider la stabilité du système sur une longue durée avec:
- Memory leaks détectables
- Performance degradation mesurable
- File descriptor leaks
- CPU usage stable
- Hot-reload répétés sans dégradation

**But**: Prouver que le système peut tourner en production 24/7

---

## 📋 Description

### Setup
- Charger 3 modules simultanément:
  - `TankModule` (50 tanks actifs)
  - `ProductionModule` (spawn 1 tank/seconde)
  - `MapModule` (grille 200x200)
- Exécuter à 60 FPS constant pendant 10 minutes
- Hot-reload round-robin toutes les 5 secondes (120 reloads total)

### Métriques Critiques
1. **Memory**: Croissance < 20MB sur 10 minutes
2. **CPU**: Usage stable (variation < 10%)
3. **FPS**: Minimum > 30 (jamais de freeze)
4. **Reload latency**: P99 < 1s (même après 120 reloads)
5. **File descriptors**: Aucun leak

---

## 🏗️ Implémentation

### Modules de Test

#### TankModule (déjà existant)
```cpp
// 50 tanks qui bougent en continu
class TankModule : public IModule {
    std::vector<Tank> tanks; // 50 tanks
    void process(float dt) override {
        for (auto& tank : tanks) {
            tank.position += tank.velocity * dt;
        }
    }
};
```

#### ProductionModule
```cpp
class ProductionModule : public IModule {
public:
    void process(float deltaTime) override {
        timeSinceLastSpawn += deltaTime;

        // Spawner 1 tank par seconde
        if (timeSinceLastSpawn >= 1.0f) {
            spawnTank();
            timeSinceLastSpawn -= 1.0f;
        }
    }

    std::shared_ptr<IDataNode> getState() const override {
        auto state = std::make_shared<JsonDataNode>();
        auto& json = state->getJsonData();

        json["tankCount"] = tankCount;
        json["timeSinceLastSpawn"] = timeSinceLastSpawn;

        nlohmann::json tanksJson = nlohmann::json::array();
        for (const auto& tank : spawnedTanks) {
            tanksJson.push_back({
                {"id", tank.id},
                {"spawnTime", tank.spawnTime}
            });
        }
        json["spawnedTanks"] = tanksJson;

        return state;
    }

private:
    int tankCount = 0;
    float timeSinceLastSpawn = 0.0f;
    std::vector<SpawnedTank> spawnedTanks;

    void spawnTank() {
        tankCount++;
        spawnedTanks.push_back({tankCount, getCurrentTime()});
        logger->debug("Spawned tank #{}", tankCount);
    }
};
```

#### MapModule
```cpp
class MapModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override {
        int size = config->getInt("mapSize", 200);
        grid.resize(size * size, 0); // Grille 200x200 = 40k cells
    }

    void process(float deltaTime) override {
        // Update grille (simuler fog of war ou autre)
        for (size_t i = 0; i < grid.size(); i += 100) {
            grid[i] = (grid[i] + 1) % 256;
        }
    }

    std::shared_ptr<IDataNode> getState() const override {
        auto state = std::make_shared<JsonDataNode>();
        auto& json = state->getJsonData();

        json["mapSize"] = std::sqrt(grid.size());
        // Ne pas sérialiser toute la grille (trop gros)
        json["gridChecksum"] = computeChecksum(grid);

        return state;
    }

private:
    std::vector<uint8_t> grid;

    uint32_t computeChecksum(const std::vector<uint8_t>& data) const {
        uint32_t sum = 0;
        for (auto val : data) sum += val;
        return sum;
    }
};
```

### Test Principal

```cpp
// test_03_stress_test.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestReporter.h"
#include "helpers/ResourceMonitor.h"

int main(int argc, char* argv[]) {
    // Durée configurable (10 min par défaut, 1h pour nightly)
    int durationMinutes = 10;
    if (argc > 1 && std::string(argv[1]) == "--nightly") {
        durationMinutes = 60;
    }

    int totalFrames = durationMinutes * 60 * 60; // min * sec * fps
    int reloadIntervalFrames = 5 * 60; // 5 secondes

    TestReporter reporter("Stress Test Long-Running");
    TestMetrics metrics;
    ResourceMonitor resMonitor;

    std::cout << "================================================================================\n";
    std::cout << "STRESS TEST: " << durationMinutes << " minutes\n";
    std::cout << "================================================================================\n\n";

    // === SETUP ===
    DebugEngine engine;

    // Charger 3 modules
    engine.loadModule("TankModule", "build/modules/libTankModule.so");
    engine.loadModule("ProductionModule", "build/modules/libProductionModule.so");
    engine.loadModule("MapModule", "build/modules/libMapModule.so");

    // Configurations
    auto tankConfig = createJsonConfig({{"tankCount", 50}});
    auto prodConfig = createJsonConfig({{"spawnRate", 1.0}});
    auto mapConfig = createJsonConfig({{"mapSize", 200}});

    engine.initializeModule("TankModule", tankConfig);
    engine.initializeModule("ProductionModule", prodConfig);
    engine.initializeModule("MapModule", mapConfig);

    // Baseline metrics
    size_t baselineMemory = getCurrentMemoryUsage();
    int baselineFDs = getOpenFileDescriptors();
    float baselineCPU = getCurrentCPUUsage();

    std::cout << "Baseline:\n";
    std::cout << "  Memory: " << (baselineMemory / (1024.0f * 1024.0f)) << " MB\n";
    std::cout << "  FDs:    " << baselineFDs << "\n";
    std::cout << "  CPU:    " << baselineCPU << "%\n\n";

    // === STRESS LOOP ===
    std::vector<std::string> moduleNames = {"TankModule", "ProductionModule", "MapModule"};
    int currentModuleIndex = 0;
    int reloadCount = 0;

    auto testStart = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < totalFrames; frame++) {
        auto frameStart = std::chrono::high_resolution_clock::now();

        // Update engine
        engine.update(1.0f / 60.0f);

        // Hot-reload round-robin toutes les 5 secondes
        if (frame > 0 && frame % reloadIntervalFrames == 0) {
            std::string moduleName = moduleNames[currentModuleIndex];

            std::cout << "[" << (frame / 3600.0f) << "min] Hot-reloading " << moduleName << "...\n";

            auto reloadStart = std::chrono::high_resolution_clock::now();

            engine.reloadModule(moduleName);
            reloadCount++;

            auto reloadEnd = std::chrono::high_resolution_clock::now();
            float reloadTime = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();
            metrics.recordReloadTime(reloadTime);

            std::cout << "  → Completed in " << reloadTime << "ms\n";

            // Rotate module
            currentModuleIndex = (currentModuleIndex + 1) % moduleNames.size();
        }

        // Métriques (échantillonner toutes les 60 frames = 1 seconde)
        if (frame % 60 == 0) {
            size_t currentMemory = getCurrentMemoryUsage();
            int currentFDs = getOpenFileDescriptors();
            float currentCPU = getCurrentCPUUsage();

            metrics.recordMemoryUsage(currentMemory);
            resMonitor.recordFDCount(currentFDs);
            resMonitor.recordCPUUsage(currentCPU);
        }

        // FPS (chaque frame)
        auto frameEnd = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
        metrics.recordFPS(1000.0f / frameTime);

        // Progress (toutes les minutes)
        if (frame % 3600 == 0 && frame > 0) {
            int elapsedMin = frame / 3600;
            std::cout << "Progress: " << elapsedMin << "/" << durationMinutes << " minutes\n";

            // Stats intermédiaires
            size_t currentMemory = getCurrentMemoryUsage();
            float memGrowth = (currentMemory - baselineMemory) / (1024.0f * 1024.0f);
            std::cout << "  Memory growth: " << memGrowth << " MB\n";
            std::cout << "  FPS (last min): min=" << metrics.getFPSMinLast60s()
                      << " avg=" << metrics.getFPSAvgLast60s() << "\n";
            std::cout << "  Reload avg:     " << metrics.getReloadTimeAvg() << "ms\n\n";
        }
    }

    auto testEnd = std::chrono::high_resolution_clock::now();
    float totalDuration = std::chrono::duration<float>(testEnd - testStart).count();

    // === VÉRIFICATIONS FINALES ===

    size_t finalMemory = getCurrentMemoryUsage();
    size_t memGrowth = finalMemory - baselineMemory;

    int finalFDs = getOpenFileDescriptors();
    int fdLeak = finalFDs - baselineFDs;

    float avgCPU = resMonitor.getCPUAvg();
    float cpuStdDev = resMonitor.getCPUStdDev();

    // Assertions
    ASSERT_LT(memGrowth, 20 * 1024 * 1024, "Memory growth should be < 20MB");
    reporter.addMetric("memory_growth_mb", memGrowth / (1024.0f * 1024.0f));

    ASSERT_EQ(fdLeak, 0, "Should have no file descriptor leaks");
    reporter.addMetric("fd_leak", fdLeak);

    float fpsMin = metrics.getFPSMin();
    ASSERT_GT(fpsMin, 30.0f, "FPS min should be > 30");
    reporter.addMetric("fps_min", fpsMin);
    reporter.addMetric("fps_avg", metrics.getFPSAvg());

    float reloadP99 = metrics.getReloadTimeP99();
    ASSERT_LT(reloadP99, 1000.0f, "Reload P99 should be < 1000ms");
    reporter.addMetric("reload_time_p99_ms", reloadP99);

    ASSERT_LT(cpuStdDev, 10.0f, "CPU usage should be stable (stddev < 10%)");
    reporter.addMetric("cpu_avg_percent", avgCPU);
    reporter.addMetric("cpu_stddev_percent", cpuStdDev);

    reporter.addMetric("total_reloads", reloadCount);
    reporter.addMetric("total_duration_sec", totalDuration);

    // === RAPPORT FINAL ===
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "STRESS TEST SUMMARY\n";
    std::cout << "================================================================================\n";
    std::cout << "  Duration:         " << totalDuration << "s (" << (totalDuration / 60.0f) << " min)\n";
    std::cout << "  Total reloads:    " << reloadCount << "\n";
    std::cout << "  Memory growth:    " << (memGrowth / (1024.0f * 1024.0f)) << " MB\n";
    std::cout << "  FD leak:          " << fdLeak << "\n";
    std::cout << "  FPS min/avg/max:  " << fpsMin << " / " << metrics.getFPSAvg() << " / " << metrics.getFPSMax() << "\n";
    std::cout << "  Reload avg/p99:   " << metrics.getReloadTimeAvg() << "ms / " << reloadP99 << "ms\n";
    std::cout << "  CPU avg±stddev:   " << avgCPU << "% ± " << cpuStdDev << "%\n";
    std::cout << "================================================================================\n\n";

    metrics.printReport();
    reporter.printFinalReport();

    return reporter.getExitCode();
}
```

---

## 📊 Métriques Collectées

| Métrique | Description | Seuil (10 min) | Seuil (1h) |
|----------|-------------|----------------|------------|
| **memory_growth_mb** | Croissance mémoire totale | < 20MB | < 100MB |
| **fd_leak** | File descriptors ouverts en trop | 0 | 0 |
| **fps_min** | FPS minimum observé | > 30 | > 30 |
| **fps_avg** | FPS moyen | ~60 | ~60 |
| **reload_time_p99_ms** | Latence P99 des reloads | < 1000ms | < 1000ms |
| **cpu_avg_percent** | CPU moyen | N/A (info) | N/A (info) |
| **cpu_stddev_percent** | Stabilité CPU | < 10% | < 10% |
| **total_reloads** | Nombre total de reloads | ~120 | ~720 |

---

## ✅ Critères de Succès

### MUST PASS (10 minutes)
1. ✅ Memory growth < 20MB
2. ✅ FD leak = 0
3. ✅ FPS min > 30
4. ✅ Reload P99 < 1000ms
5. ✅ CPU stable (stddev < 10%)
6. ✅ Aucun crash

### MUST PASS (1 heure nightly)
1. ✅ Memory growth < 100MB
2. ✅ FD leak = 0
3. ✅ FPS min > 30
4. ✅ Reload P99 < 1000ms (pas de dégradation)
5. ✅ CPU stable (stddev < 10%)
6. ✅ Aucun crash

---

## 🔧 Helpers Nécessaires

### ResourceMonitor

```cpp
// helpers/ResourceMonitor.h
class ResourceMonitor {
public:
    void recordFDCount(int count) {
        fdCounts.push_back(count);
    }

    void recordCPUUsage(float percent) {
        cpuUsages.push_back(percent);
    }

    float getCPUAvg() const {
        return std::accumulate(cpuUsages.begin(), cpuUsages.end(), 0.0f) / cpuUsages.size();
    }

    float getCPUStdDev() const {
        float avg = getCPUAvg();
        float variance = 0.0f;
        for (float cpu : cpuUsages) {
            variance += std::pow(cpu - avg, 2);
        }
        return std::sqrt(variance / cpuUsages.size());
    }

private:
    std::vector<int> fdCounts;
    std::vector<float> cpuUsages;
};
```

### System Utilities

```cpp
// helpers/SystemUtils.h

int getOpenFileDescriptors() {
    // Linux: /proc/self/fd
    int count = 0;
    DIR* dir = opendir("/proc/self/fd");
    if (dir) {
        while (readdir(dir)) count++;
        closedir(dir);
    }
    return count - 2; // Exclude . and ..
}

float getCurrentCPUUsage() {
    // Linux: /proc/self/stat
    std::ifstream stat("/proc/self/stat");
    std::string line;
    std::getline(stat, line);

    // Parse utime + stime (fields 14 & 15)
    // Comparer avec previous reading pour obtenir %
    // Simplifié ici, voir impl complète
    return 0.0f; // Placeholder
}
```

---

## 📝 Output Attendu (10 minutes)

```
================================================================================
STRESS TEST: 10 minutes
================================================================================

Baseline:
  Memory: 45.2 MB
  FDs:    12
  CPU:    2.3%

[0.08min] Hot-reloading TankModule...
  → Completed in 423ms
[0.17min] Hot-reloading ProductionModule...
  → Completed in 389ms
Progress: 1/10 minutes
  Memory growth: 1.2 MB
  FPS (last min): min=59 avg=60
  Reload avg:     405ms

Progress: 2/10 minutes
  Memory growth: 2.1 MB
  FPS (last min): min=58 avg=60
  Reload avg:     412ms

...

Progress: 10/10 minutes
  Memory growth: 8.7 MB
  FPS (last min): min=59 avg=60
  Reload avg:     418ms

================================================================================
STRESS TEST SUMMARY
================================================================================
  Duration:         601.2s (10.0 min)
  Total reloads:    120
  Memory growth:    8.7 MB
  FD leak:          0
  FPS min/avg/max:  58 / 60 / 62
  Reload avg/p99:   415ms / 687ms
  CPU avg±stddev:   12.3% ± 3.2%
================================================================================

METRICS
================================================================================
  Memory growth:    8.7 MB         (threshold: < 20MB)   ✓
  FD leak:          0              (threshold: 0)        ✓
  FPS min:          58             (threshold: > 30)     ✓
  Reload P99:       687ms          (threshold: < 1000ms) ✓
  CPU stable:       3.2%           (threshold: < 10%)    ✓

Result: ✅ PASSED

================================================================================
```

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| Memory growth > 20MB | Memory leak dans module | FAIL - fix destructors |
| FD leak > 0 | dlopen/dlclose déséquilibré | FAIL - fix ModuleLoader |
| FPS degradation | Performance regression | FAIL - profile + optimize |
| Reload P99 croissant | Fragmentation mémoire | WARNING - investigate |
| CPU instable | Busy loop ou GC | FAIL - fix algorithm |

---

## 📅 Planning

**Jour 1 (3h):**
- Implémenter ProductionModule et MapModule
- Implémenter ResourceMonitor helper

**Jour 2 (3h):**
- Implémenter test_03_stress_test.cpp
- System utilities (FD count, CPU usage)
- Debug + validation

---

**Prochaine étape**: `scenario_04_race_condition.md`
