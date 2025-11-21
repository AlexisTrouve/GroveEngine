# Architecture des Tests - Helpers & Infrastructure

Ce document détaille l'architecture commune à tous les tests d'intégration.

---

## 📁 Structure des Fichiers

```
tests/
  ├─ integration/
  │   ├─ test_01_production_hotreload.cpp
  │   ├─ test_02_chaos_monkey.cpp
  │   ├─ test_03_stress_test.cpp
  │   ├─ test_04_race_condition.cpp
  │   └─ test_05_multimodule.cpp
  │
  ├─ modules/
  │   ├─ TankModule.h/.cpp           # Module réaliste avec state complexe
  │   ├─ ProductionModule.h/.cpp     # Auto-spawn entities
  │   ├─ MapModule.h/.cpp            # Grille 2D
  │   ├─ ChaosModule.h/.cpp          # Génère failures aléatoires
  │   └─ HeavyStateModule.h/.cpp     # State 100MB (Phase 3)
  │
  └─ helpers/
      ├─ TestMetrics.h/.cpp          # Collecte métriques (memory, FPS, etc.)
      ├─ TestAssertions.h            # Macros d'assertions
      ├─ TestReporter.h/.cpp         # Génération rapports pass/fail
      ├─ ResourceMonitor.h/.cpp      # Monitoring CPU, FD, etc.
      ├─ AutoCompiler.h/.cpp         # Compilation automatique
      └─ SystemUtils.h/.cpp          # Utilitaires système (memory, FD, CPU)
```

---

## 🔧 Helpers Détaillés

### 1. TestMetrics

**Fichier**: `tests/helpers/TestMetrics.h` et `TestMetrics.cpp`

**Responsabilité**: Collecter toutes les métriques durant l'exécution des tests.

```cpp
// TestMetrics.h
#pragma once
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

class TestMetrics {
public:
    // Enregistrement
    void recordFPS(float fps);
    void recordMemoryUsage(size_t bytes);
    void recordReloadTime(float ms);
    void recordCrash(const std::string& reason);

    // Getters - FPS
    float getFPSMin() const;
    float getFPSMax() const;
    float getFPSAvg() const;
    float getFPSStdDev() const;
    float getFPSMinLast60s() const;  // Pour stress test
    float getFPSAvgLast60s() const;

    // Getters - Memory
    size_t getMemoryInitial() const;
    size_t getMemoryFinal() const;
    size_t getMemoryPeak() const;
    size_t getMemoryGrowth() const;

    // Getters - Reload
    float getReloadTimeAvg() const;
    float getReloadTimeMin() const;
    float getReloadTimeMax() const;
    float getReloadTimeP99() const;  // Percentile 99
    int getReloadCount() const;

    // Getters - Crashes
    int getCrashCount() const;
    const std::vector<std::string>& getCrashReasons() const;

    // Rapport
    void printReport() const;

private:
    std::vector<float> fpsValues;
    std::vector<size_t> memoryValues;
    std::vector<float> reloadTimes;
    std::vector<std::string> crashReasons;

    size_t initialMemory = 0;
    bool hasInitialMemory = false;
};
```

```cpp
// TestMetrics.cpp
#include "TestMetrics.h"
#include <iostream>
#include <iomanip>

void TestMetrics::recordFPS(float fps) {
    fpsValues.push_back(fps);
}

void TestMetrics::recordMemoryUsage(size_t bytes) {
    if (!hasInitialMemory) {
        initialMemory = bytes;
        hasInitialMemory = true;
    }
    memoryValues.push_back(bytes);
}

void TestMetrics::recordReloadTime(float ms) {
    reloadTimes.push_back(ms);
}

void TestMetrics::recordCrash(const std::string& reason) {
    crashReasons.push_back(reason);
}

float TestMetrics::getFPSMin() const {
    if (fpsValues.empty()) return 0.0f;
    return *std::min_element(fpsValues.begin(), fpsValues.end());
}

float TestMetrics::getFPSMax() const {
    if (fpsValues.empty()) return 0.0f;
    return *std::max_element(fpsValues.begin(), fpsValues.end());
}

float TestMetrics::getFPSAvg() const {
    if (fpsValues.empty()) return 0.0f;
    return std::accumulate(fpsValues.begin(), fpsValues.end(), 0.0f) / fpsValues.size();
}

float TestMetrics::getFPSStdDev() const {
    if (fpsValues.empty()) return 0.0f;
    float avg = getFPSAvg();
    float variance = 0.0f;
    for (float fps : fpsValues) {
        variance += std::pow(fps - avg, 2);
    }
    return std::sqrt(variance / fpsValues.size());
}

size_t TestMetrics::getMemoryGrowth() const {
    if (memoryValues.empty()) return 0;
    return memoryValues.back() - initialMemory;
}

size_t TestMetrics::getMemoryPeak() const {
    if (memoryValues.empty()) return 0;
    return *std::max_element(memoryValues.begin(), memoryValues.end());
}

float TestMetrics::getReloadTimeAvg() const {
    if (reloadTimes.empty()) return 0.0f;
    return std::accumulate(reloadTimes.begin(), reloadTimes.end(), 0.0f) / reloadTimes.size();
}

float TestMetrics::getReloadTimeP99() const {
    if (reloadTimes.empty()) return 0.0f;
    auto sorted = reloadTimes;
    std::sort(sorted.begin(), sorted.end());
    size_t p99Index = static_cast<size_t>(sorted.size() * 0.99);
    return sorted[p99Index];
}

void TestMetrics::printReport() const {
    std::cout << "╔══════════════════════════════════════════════════════════════\n";
    std::cout << "║ METRICS REPORT\n";
    std::cout << "╠══════════════════════════════════════════════════════════════\n";

    if (!fpsValues.empty()) {
        std::cout << "║ FPS:\n";
        std::cout << "║   Min:       " << std::setw(8) << getFPSMin() << "\n";
        std::cout << "║   Avg:       " << std::setw(8) << getFPSAvg() << "\n";
        std::cout << "║   Max:       " << std::setw(8) << getFPSMax() << "\n";
        std::cout << "║   Std Dev:   " << std::setw(8) << getFPSStdDev() << "\n";
    }

    if (!memoryValues.empty()) {
        std::cout << "║ Memory:\n";
        std::cout << "║   Initial:   " << std::setw(8) << (initialMemory / 1024.0f / 1024.0f) << " MB\n";
        std::cout << "║   Final:     " << std::setw(8) << (memoryValues.back() / 1024.0f / 1024.0f) << " MB\n";
        std::cout << "║   Peak:      " << std::setw(8) << (getMemoryPeak() / 1024.0f / 1024.0f) << " MB\n";
        std::cout << "║   Growth:    " << std::setw(8) << (getMemoryGrowth() / 1024.0f / 1024.0f) << " MB\n";
    }

    if (!reloadTimes.empty()) {
        std::cout << "║ Reload Times:\n";
        std::cout << "║   Count:     " << std::setw(8) << reloadTimes.size() << "\n";
        std::cout << "║   Avg:       " << std::setw(8) << getReloadTimeAvg() << " ms\n";
        std::cout << "║   Min:       " << std::setw(8) << getReloadTimeMin() << " ms\n";
        std::cout << "║   Max:       " << std::setw(8) << getReloadTimeMax() << " ms\n";
        std::cout << "║   P99:       " << std::setw(8) << getReloadTimeP99() << " ms\n";
    }

    if (!crashReasons.empty()) {
        std::cout << "║ Crashes:     " << crashReasons.size() << "\n";
        for (const auto& reason : crashReasons) {
            std::cout << "║   - " << reason << "\n";
        }
    }

    std::cout << "╚══════════════════════════════════════════════════════════════\n";
}
```

---

### 2. TestAssertions

**Fichier**: `tests/helpers/TestAssertions.h` (header-only)

**Responsabilité**: Macros d'assertions pour tests.

```cpp
// TestAssertions.h
#pragma once
#include <iostream>
#include <cstdlib>
#include <cmath>

// Couleurs pour output
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_RESET   "\033[0m"

#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_FALSE(condition, message) \
    ASSERT_TRUE(!(condition), message)

#define ASSERT_EQ(actual, expected, message) \
    do { \
        if ((actual) != (expected)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Expected: " << (expected) << "\n"; \
            std::cerr << "   Actual:   " << (actual) << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_NE(actual, expected, message) \
    do { \
        if ((actual) == (expected)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Should not equal: " << (expected) << "\n"; \
            std::cerr << "   But got:          " << (actual) << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_LT(value, max, message) \
    do { \
        if ((value) >= (max)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Expected: < " << (max) << "\n"; \
            std::cerr << "   Actual:   " << (value) << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_GT(value, min, message) \
    do { \
        if ((value) <= (min)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Expected: > " << (min) << "\n"; \
            std::cerr << "   Actual:   " << (value) << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_WITHIN(actual, expected, tolerance, message) \
    do { \
        auto diff = std::abs((actual) - (expected)); \
        if (diff > (tolerance)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Expected: " << (expected) << " ± " << (tolerance) << "\n"; \
            std::cerr << "   Actual:   " << (actual) << " (diff: " << diff << ")\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)
```

---

### 3. TestReporter

**Fichier**: `tests/helpers/TestReporter.h` et `TestReporter.cpp`

**Responsabilité**: Générer rapport final pass/fail.

```cpp
// TestReporter.h
#pragma once
#include <string>
#include <map>
#include <vector>

class TestReporter {
public:
    explicit TestReporter(const std::string& scenarioName);

    void addMetric(const std::string& name, float value);
    void addAssertion(const std::string& name, bool passed);

    void printFinalReport() const;
    int getExitCode() const; // 0 = pass, 1 = fail

private:
    std::string scenarioName;
    std::map<std::string, float> metrics;
    std::vector<std::pair<std::string, bool>> assertions;
};
```

```cpp
// TestReporter.cpp
#include "TestReporter.h"
#include <iostream>

TestReporter::TestReporter(const std::string& name) : scenarioName(name) {}

void TestReporter::addMetric(const std::string& name, float value) {
    metrics[name] = value;
}

void TestReporter::addAssertion(const std::string& name, bool passed) {
    assertions.push_back({name, passed});
}

void TestReporter::printFinalReport() const {
    std::cout << "\n";
    std::cout << "════════════════════════════════════════════════════════════════\n";
    std::cout << "FINAL REPORT: " << scenarioName << "\n";
    std::cout << "════════════════════════════════════════════════════════════════\n\n";

    // Metrics
    if (!metrics.empty()) {
        std::cout << "Metrics:\n";
        for (const auto& [name, value] : metrics) {
            std::cout << "  " << name << ": " << value << "\n";
        }
        std::cout << "\n";
    }

    // Assertions
    if (!assertions.empty()) {
        std::cout << "Assertions:\n";
        bool allPassed = true;
        for (const auto& [name, passed] : assertions) {
            std::cout << "  " << (passed ? "✓" : "✗") << " " << name << "\n";
            if (!passed) allPassed = false;
        }
        std::cout << "\n";

        if (allPassed) {
            std::cout << "Result: ✅ PASSED\n";
        } else {
            std::cout << "Result: ❌ FAILED\n";
        }
    }

    std::cout << "════════════════════════════════════════════════════════════════\n";
}

int TestReporter::getExitCode() const {
    for (const auto& [name, passed] : assertions) {
        if (!passed) return 1; // FAIL
    }
    return 0; // PASS
}
```

---

### 4. SystemUtils

**Fichier**: `tests/helpers/SystemUtils.h` et `SystemUtils.cpp`

**Responsabilité**: Fonctions utilitaires système (Linux).

```cpp
// SystemUtils.h
#pragma once
#include <cstddef>

size_t getCurrentMemoryUsage();
int getOpenFileDescriptors();
float getCurrentCPUUsage();
```

```cpp
// SystemUtils.cpp
#include "SystemUtils.h"
#include <fstream>
#include <string>
#include <dirent.h>
#include <sstream>

size_t getCurrentMemoryUsage() {
    // Linux: /proc/self/status -> VmRSS
    std::ifstream file("/proc/self/status");
    std::string line;

    while (std::getline(file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line.substr(7));
            size_t kb;
            iss >> kb;
            return kb * 1024; // Convert to bytes
        }
    }

    return 0;
}

int getOpenFileDescriptors() {
    // Linux: /proc/self/fd
    int count = 0;
    DIR* dir = opendir("/proc/self/fd");

    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            count++;
        }
        closedir(dir);
    }

    return count - 2; // Exclude . and ..
}

float getCurrentCPUUsage() {
    // Simplifié - retourne 0 pour l'instant
    // Implémentation complète nécessite tracking du /proc/self/stat
    // entre deux lectures (utime + stime delta)
    return 0.0f;
}
```

---

### 5. ResourceMonitor

**Fichier**: `tests/helpers/ResourceMonitor.h` et `ResourceMonitor.cpp`

**Responsabilité**: Monitoring CPU, FD pour stress tests.

```cpp
// ResourceMonitor.h
#pragma once
#include <vector>

class ResourceMonitor {
public:
    void recordFDCount(int count);
    void recordCPUUsage(float percent);

    int getFDAvg() const;
    int getFDMax() const;

    float getCPUAvg() const;
    float getCPUStdDev() const;

private:
    std::vector<int> fdCounts;
    std::vector<float> cpuUsages;
};
```

```cpp
// ResourceMonitor.cpp
#include "ResourceMonitor.h"
#include <algorithm>
#include <numeric>
#include <cmath>

void ResourceMonitor::recordFDCount(int count) {
    fdCounts.push_back(count);
}

void ResourceMonitor::recordCPUUsage(float percent) {
    cpuUsages.push_back(percent);
}

int ResourceMonitor::getFDAvg() const {
    if (fdCounts.empty()) return 0;
    return std::accumulate(fdCounts.begin(), fdCounts.end(), 0) / fdCounts.size();
}

int ResourceMonitor::getFDMax() const {
    if (fdCounts.empty()) return 0;
    return *std::max_element(fdCounts.begin(), fdCounts.end());
}

float ResourceMonitor::getCPUAvg() const {
    if (cpuUsages.empty()) return 0.0f;
    return std::accumulate(cpuUsages.begin(), cpuUsages.end(), 0.0f) / cpuUsages.size();
}

float ResourceMonitor::getCPUStdDev() const {
    if (cpuUsages.empty()) return 0.0f;
    float avg = getCPUAvg();
    float variance = 0.0f;
    for (float cpu : cpuUsages) {
        variance += std::pow(cpu - avg, 2);
    }
    return std::sqrt(variance / cpuUsages.size());
}
```

---

### 6. AutoCompiler

**Fichier**: `tests/helpers/AutoCompiler.h` et `AutoCompiler.cpp`

Voir détails dans `scenario_04_race_condition.md`.

---

## 🔨 CMakeLists.txt pour Tests

```cmake
# tests/CMakeLists.txt

# Helpers library (partagée par tous les tests)
add_library(test_helpers STATIC
    helpers/TestMetrics.cpp
    helpers/TestReporter.cpp
    helpers/SystemUtils.cpp
    helpers/ResourceMonitor.cpp
    helpers/AutoCompiler.cpp
)

target_include_directories(test_helpers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_helpers PUBLIC
    grove_core
    spdlog::spdlog
)

# Tests d'intégration
add_executable(test_01_production_hotreload integration/test_01_production_hotreload.cpp)
target_link_libraries(test_01_production_hotreload PRIVATE test_helpers grove_core)

add_executable(test_02_chaos_monkey integration/test_02_chaos_monkey.cpp)
target_link_libraries(test_02_chaos_monkey PRIVATE test_helpers grove_core)

add_executable(test_03_stress_test integration/test_03_stress_test.cpp)
target_link_libraries(test_03_stress_test PRIVATE test_helpers grove_core)

add_executable(test_04_race_condition integration/test_04_race_condition.cpp)
target_link_libraries(test_04_race_condition PRIVATE test_helpers grove_core)

add_executable(test_05_multimodule integration/test_05_multimodule.cpp)
target_link_libraries(test_05_multimodule PRIVATE test_helpers grove_core)

# Modules de test
add_library(TankModule SHARED modules/TankModule.cpp)
target_link_libraries(TankModule PRIVATE grove_core)

add_library(ProductionModule SHARED modules/ProductionModule.cpp)
target_link_libraries(ProductionModule PRIVATE grove_core)

add_library(MapModule SHARED modules/MapModule.cpp)
target_link_libraries(MapModule PRIVATE grove_core)

add_library(ChaosModule SHARED modules/ChaosModule.cpp)
target_link_libraries(ChaosModule PRIVATE grove_core)

# CTest integration
enable_testing()
add_test(NAME ProductionHotReload COMMAND test_01_production_hotreload)
add_test(NAME ChaosMonkey COMMAND test_02_chaos_monkey)
add_test(NAME StressTest COMMAND test_03_stress_test)
add_test(NAME RaceCondition COMMAND test_04_race_condition)
add_test(NAME MultiModule COMMAND test_05_multimodule)
```

---

## 🎯 Utilisation

### Compiler tous les tests
```bash
cd build
cmake -DBUILD_INTEGRATION_TESTS=ON ..
cmake --build . --target test_helpers
cmake --build . --target TankModule
cmake --build . --target ProductionModule
cmake --build .
```

### Exécuter tous les tests
```bash
ctest --output-on-failure
```

### Exécuter un test individuel
```bash
./test_01_production_hotreload
```

### Vérifier exit code
```bash
./test_01_production_hotreload
echo $?  # 0 = PASS, 1 = FAIL
```

---

**Prochaine étape**: `seuils_success.md` (tous les seuils pass/fail)
