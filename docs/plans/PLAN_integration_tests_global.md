# Plan Global - Tests d'Intégration GroveEngine

## 🎯 Objectif

Implémenter une suite complète de tests d'intégration end-to-end pour valider la robustesse du système de hot-reload et de gestion de modules du GroveEngine.

**Contraintes:**
- ✅ 100% automatique (zéro interaction utilisateur)
- ✅ Reproductible (seed fixe pour aléatoire)
- ✅ Métriques automatiques (temps, mémoire, CPU)
- ✅ Pass/Fail clair (exit code 0/1)
- ✅ CI/CD ready

---

## 📋 Scénarios de Test (Priorité)

### Phase 1: MUST HAVE (~2-3 jours)
| Scénario | Description | Durée | Criticité |
|----------|-------------|-------|-----------|
| **1. Production Hot-Reload** | Hot-reload avec state complexe en conditions réelles | ~30s | ⭐⭐⭐ |
| **2. Chaos Monkey** | Failures aléatoires (crashes, corruptions) | ~5min | ⭐⭐⭐ |
| **3. Stress Test** | Long-running avec reloads répétés | ~10min | ⭐⭐⭐ |

### Phase 2: SHOULD HAVE (~1-2 jours)
| Scénario | Description | Durée | Criticité |
|----------|-------------|-------|-----------|
| **4. Race Condition Hunter** | Compilation concurrente + reload | ~10min | ⭐⭐ |
| **5. Multi-Module Orchestration** | Interactions entre modules | ~1min | ⭐⭐ |

### Phase 3: NICE TO HAVE (~1 jour)
| Scénario | Description | Durée | Criticité |
|----------|-------------|-------|-----------|
| **6. Error Recovery** | Crash detection + auto-recovery | ~2min | ⭐ |
| **7. Limite Tests** | Large state, long init, timeouts | ~3min | ⭐ |
| **8. Config Hot-Reload** | Changement config à la volée | ~1min | ⭐ |

---

## 🏗️ Architecture des Tests

### Structure de Fichiers
```
tests/
  ├─ integration/
  │   ├─ test_01_production_hotreload.cpp
  │   ├─ test_02_chaos_monkey.cpp
  │   ├─ test_03_stress_test.cpp
  │   ├─ test_04_race_condition.cpp
  │   ├─ test_05_multimodule.cpp
  │   ├─ test_06_error_recovery.cpp
  │   ├─ test_07_limits.cpp
  │   └─ test_08_config_hotreload.cpp
  │
  ├─ modules/
  │   ├─ TankModule.cpp          # Module réaliste avec state complexe
  │   ├─ ProductionModule.cpp    # Auto-spawn entities
  │   ├─ MapModule.cpp           # Gestion carte/terrain
  │   ├─ CrashModule.cpp         # Crash contrôlé pour tests
  │   └─ HeavyStateModule.cpp    # State 100MB
  │
  └─ helpers/
      ├─ TestMetrics.h/.cpp      # Collecte métriques (memory, CPU, FPS)
      ├─ TestAssertions.h        # Macros ASSERT_*
      ├─ AutoCompiler.h/.cpp     # Trigger compilation automatique
      └─ TestReporter.h/.cpp     # Génération rapports pass/fail
```

### Composants Communs

#### 1. TestMetrics
```cpp
class TestMetrics {
public:
    void recordReloadTime(float ms);
    void recordMemoryUsage(size_t bytes);
    void recordFPS(float fps);
    void recordCrash(const std::string& reason);

    // Rapport final
    void printReport();
    bool meetsThresholds(const Thresholds& t);
};
```

#### 2. TestAssertions
```cpp
#define ASSERT_TRUE(cond, msg)
#define ASSERT_EQ(actual, expected)
#define ASSERT_WITHIN(actual, expected, tolerance)
#define ASSERT_LT(value, max)
#define ASSERT_GT(value, min)
```

#### 3. AutoCompiler
```cpp
class AutoCompiler {
public:
    void compileModuleAsync(const std::string& moduleName);
    void compileModuleSync(const std::string& moduleName);
    bool isCompiling() const;
    void waitForCompletion();
};
```

#### 4. TestReporter
```cpp
class TestReporter {
public:
    void setScenarioName(const std::string& name);
    void addMetric(const std::string& key, float value);
    void addAssertion(const std::string& name, bool passed);

    void printFinalReport();
    int getExitCode(); // 0 = pass, 1 = fail
};
```

---

## 📊 Métriques Collectées

### Toutes les Tests
- ✅ **Temps de reload** (avg, min, max, p99)
- ✅ **Memory usage** (initial, final, growth, peak)
- ✅ **FPS** (min, avg, max, stddev)
- ✅ **Nombre de crashes** (expected vs unexpected)
- ✅ **File descriptors** (détection leaks)

### Spécifiques
- **Chaos Monkey**: Taux de recovery (%)
- **Stress Test**: Durée totale exécution
- **Race Condition**: Taux de succès compilation (%)
- **Multi-Module**: Temps isolation (impact reload d'un module sur autres)

---

## 🎯 Seuils de Succès (Thresholds)

### Production Hot-Reload
```yaml
reload_time_avg: < 500ms
reload_time_max: < 1000ms
memory_growth: < 5MB
fps_min: > 30
state_preservation: 100%
```

### Chaos Monkey
```yaml
engine_alive: true
memory_growth: < 10MB
recovery_rate: > 95%
no_deadlocks: true
```

### Stress Test
```yaml
duration: 10 minutes
reload_count: ~120 (toutes les 5s)
memory_growth: < 20MB
reload_time_p99: < 1000ms
fd_leaks: 0
```

### Race Condition
```yaml
compilation_cycles: 1000
crash_count: 0
corrupted_loads: 0
reload_success_rate: > 99%
```

### Multi-Module
```yaml
isolated_reload: true (autres modules non affectés)
execution_order_preserved: true
state_sync: 100%
```

---

## 🚀 Plan d'Exécution

### Semaine 1: Fondations + Phase 1
**Jour 1-2:**
- Créer architecture helpers (TestMetrics, Assertions, etc.)
- Implémenter TankModule (module réaliste)
- Scénario 1: Production Hot-Reload

**Jour 3:**
- Scénario 2: Chaos Monkey

**Jour 4:**
- Scénario 3: Stress Test

**Jour 5:**
- Tests + corrections Phase 1

### Semaine 2: Phase 2 + Phase 3
**Jour 6-7:**
- Scénario 4: Race Condition Hunter
- Scénario 5: Multi-Module Orchestration

**Jour 8-9:**
- Scénarios 6, 7, 8 (error recovery, limites, config)

**Jour 10:**
- Documentation finale
- CI/CD integration (GitHub Actions)

---

## 📝 Format des Rapports

### Exemple de sortie attendue
```
================================================================================
TEST: Production Hot-Reload
================================================================================

Setup:
  - Module: TankModule
  - Entities: 50 tanks
  - Duration: 30 seconds
  - Reload trigger: After 15s

Metrics:
  ✓ Reload time: 487ms (threshold: < 1000ms)
  ✓ Memory growth: 2.3MB (threshold: < 5MB)
  ✓ FPS min: 58 (threshold: > 30)
  ✓ FPS avg: 60
  ✓ State preservation: 50/50 tanks (100%)

Assertions:
  ✓ All tanks present after reload
  ✓ Positions preserved (error < 0.01)
  ✓ Velocities preserved
  ✓ No crashes

Result: ✅ PASSED

================================================================================
```

---

## 🔧 Commandes d'Exécution

### Compilation
```bash
cd build
cmake -DBUILD_INTEGRATION_TESTS=ON ..
cmake --build . --target integration_tests
```

### Exécution
```bash
# Tous les tests
ctest --output-on-failure

# Test individuel
./test_01_production_hotreload
./test_02_chaos_monkey --duration 300  # 5 minutes

# Avec verbosité
./test_03_stress_test --verbose

# CI mode (pas de couleurs)
./test_04_race_condition --ci
```

### Analyse
```bash
# Génération rapport JSON
./test_01_production_hotreload --output report.json

# Agrégation tous les rapports
python3 scripts/aggregate_test_reports.py
```

---

## 📌 Notes Importantes

1. **Seeds fixes**: Tous les tests avec aléatoire utilisent un seed fixe (reproductibilité)
2. **Timeouts**: Chaque test a un timeout max (évite tests infinis)
3. **Cleanup**: Tous les tests nettoient leurs ressources (fichiers temporaires, etc.)
4. **Isolation**: Chaque test peut tourner indépendamment
5. **Logs**: Niveau de log configurable (DEBUG pour dev, ERROR pour CI)

---

## 📚 Détails par Scénario

Pour les détails complets de chaque scénario, voir:
- `scenario_01_production_hotreload.md`
- `scenario_02_chaos_monkey.md`
- `scenario_03_stress_test.md`
- `scenario_04_race_condition.md`
- `scenario_05_multimodule.md`
- `scenario_06_error_recovery.md` (Phase 3)
- `scenario_07_limits.md` (Phase 3)
- `scenario_08_config_hotreload.md` (Phase 3)

---

**Dernière mise à jour**: 2025-11-13
**Statut**: Planning phase
