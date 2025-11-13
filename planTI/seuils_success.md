# Seuils de Succès - Critères Pass/Fail

Ce document centralise tous les seuils de succès pour chaque scénario de test.

---

## 🎯 Philosophie des Seuils

### Niveaux de Criticité

- **MUST PASS** ✅: Critères obligatoires. Si un seul échoue → test FAIL
- **SHOULD PASS** ⚠️: Critères recommandés. Si échec → WARNING dans logs
- **NICE TO HAVE** 💡: Critères optimaux. Si échec → INFO dans logs

### Rationale

Les seuils sont définis en fonction de:
1. **Production readiness**: Capacité à tourner en prod 24/7
2. **User experience**: Impact sur la fluidité (60 FPS = 16.67ms/frame)
3. **Resource constraints**: Memory, CPU, file descriptors
4. **Industry standards**: Temps de reload acceptable, uptime

---

## 📊 Scénario 1: Production Hot-Reload

### MUST PASS ✅

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `reload_time_ms` | **< 1000ms** | Reload > 1s = freeze visible pour l'utilisateur |
| `memory_growth_mb` | **< 5MB** | Croissance mémoire significative = leak probable |
| `fps_min` | **> 30** | < 30 FPS = jeu injouable |
| `tank_count_preserved` | **50/50 (100%)** | Perte d'entités = bug critique |
| `positions_preserved` | **100%** | Positions incorrectes = désync gameplay |
| `no_crashes` | **true** | Crash = inacceptable |

### SHOULD PASS ⚠️

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `reload_time_ms` | **< 500ms** | Reload plus rapide = meilleure UX |
| `fps_min` | **> 50** | > 50 FPS = expérience très fluide |

### NICE TO HAVE 💡

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `memory_growth_mb` | **< 1MB** | Memory growth minimal = système quasi-parfait |
| `reload_time_ms` | **< 300ms** | Reload imperceptible |

---

## 📊 Scénario 2: Chaos Monkey

### MUST PASS ✅

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `engine_alive` | **true** | Engine mort = test fail total |
| `no_deadlocks` | **true** | Deadlock = système bloqué |
| `recovery_rate_percent` | **> 95%** | Recovery < 95% = système fragile |
| `memory_growth_mb` | **< 10MB** | 5 min * 2MB/min = acceptable |

### SHOULD PASS ⚠️

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `recovery_rate_percent` | **= 100%** | Recovery parfaite = robustesse optimale |
| `memory_growth_mb` | **< 5MB** | Quasi stable même avec chaos |

### NICE TO HAVE 💡

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `reload_time_avg_ms` | **< 500ms** | Reload rapide même pendant chaos |

---

## 📊 Scénario 3: Stress Test (10 minutes)

### MUST PASS ✅

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `memory_growth_mb` | **< 20MB** | 10 min → 2MB/min = acceptable |
| `fd_leak` | **= 0** | Leak FD = crash système après N heures |
| `fps_min` | **> 30** | Minimum acceptable pour gameplay |
| `reload_time_p99_ms` | **< 1000ms** | P99 > 1s = dégradation visible |
| `cpu_stddev_percent` | **< 10%** | Stabilité CPU = pas de busy loop |
| `no_crashes` | **true** | Crash = fail |

### SHOULD PASS ⚠️

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `memory_growth_mb` | **< 10MB** | Très stable |
| `reload_time_p99_ms` | **< 750ms** | Excellent |

### NICE TO HAVE 💡

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `memory_growth_mb` | **< 5MB** | Quasi-parfait |
| `fps_min` | **> 50** | Très fluide |

---

## 📊 Scénario 3: Stress Test (1 heure - Nightly)

### MUST PASS ✅

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `memory_growth_mb` | **< 100MB** | 1h → ~1.5MB/min = acceptable |
| `fd_leak` | **= 0** | Critique |
| `fps_min` | **> 30** | Minimum acceptable |
| `reload_time_p99_ms` | **< 1000ms** | Pas de dégradation sur durée |
| `cpu_stddev_percent` | **< 10%** | Stabilité |
| `no_crashes` | **true** | Critique |

### SHOULD PASS ⚠️

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `memory_growth_mb` | **< 50MB** | Très bon |
| `reload_time_p99_ms` | **< 750ms** | Excellent |

---

## 📊 Scénario 4: Race Condition Hunter

### MUST PASS ✅

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `compile_success_rate_percent` | **> 95%** | Quelques échecs compilation OK (disk IO, etc.) |
| `reload_success_rate_percent` | **> 99%** | Presque tous les reloads doivent marcher |
| `corrupted_loads` | **= 0** | .so corrompu = file stability check raté |
| `crashes` | **= 0** | Race condition non gérée = crash |
| `reload_time_avg_ms` | **> 100ms** | Prouve que file stability check fonctionne (attend ~500ms) |

### SHOULD PASS ⚠️

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `compile_success_rate_percent` | **= 100%** | Compilations toujours OK = environnement stable |
| `reload_success_rate_percent` | **= 100%** | Parfait |

### NICE TO HAVE 💡

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `reload_time_avg_ms` | **< 600ms** | Rapide malgré file stability |

---

## 📊 Scénario 5: Multi-Module Orchestration

### MUST PASS ✅

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `map_unaffected` | **true** | Isolation critique |
| `tank_unaffected` | **true** | Isolation critique |
| `production_reloaded` | **true** | Reload doit marcher |
| `reload_time_ms` | **< 1000ms** | Standard |
| `no_crashes` | **true** | Critique |
| `execution_order_preserved` | **true** | Ordre critique pour dépendances |

### SHOULD PASS ⚠️

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `reload_time_ms` | **< 500ms** | Bon |

### NICE TO HAVE 💡

| Métrique | Seuil | Justification |
|----------|-------|---------------|
| `zero_fps_impact` | **true** | FPS identique avant/pendant/après reload |

---

## 🔧 Implémentation dans les Tests

### Pattern de Vérification

```cpp
// Dans chaque test
TestReporter reporter("Scenario Name");

// MUST PASS assertions
ASSERT_LT(reloadTime, 1000.0f, "Reload time MUST be < 1000ms");
reporter.addAssertion("reload_time_ok", reloadTime < 1000.0f);

// SHOULD PASS (warning only)
if (reloadTime >= 500.0f) {
    std::cout << "⚠️  WARNING: Reload time should be < 500ms (got " << reloadTime << "ms)\n";
}

// NICE TO HAVE (info only)
if (reloadTime < 300.0f) {
    std::cout << "💡 EXCELLENT: Reload time < 300ms\n";
}

// Exit code basé sur MUST PASS uniquement
return reporter.getExitCode(); // 0 si tous MUST PASS OK, 1 sinon
```

### TestReporter Extension

```cpp
// Ajouter dans TestReporter
enum class AssertionLevel {
    MUST_PASS,
    SHOULD_PASS,
    NICE_TO_HAVE
};

void addAssertion(const std::string& name, bool passed, AssertionLevel level);

int getExitCode() const {
    // Fail si au moins un MUST_PASS échoue
    for (const auto& [name, passed, level] : assertions) {
        if (level == AssertionLevel::MUST_PASS && !passed) {
            return 1;
        }
    }
    return 0;
}
```

---

## 📋 Tableau Récapitulatif - MUST PASS

| Scénario | Métriques Critiques | Valeurs |
|----------|---------------------|---------|
| **Production Hot-Reload** | reload_time, memory_growth, fps_min, state_preservation | < 1s, < 5MB, > 30, 100% |
| **Chaos Monkey** | engine_alive, recovery_rate, memory_growth | true, > 95%, < 10MB |
| **Stress Test (10min)** | memory_growth, fd_leak, fps_min, reload_p99 | < 20MB, 0, > 30, < 1s |
| **Stress Test (1h)** | memory_growth, fd_leak, fps_min, reload_p99 | < 100MB, 0, > 30, < 1s |
| **Race Condition** | corrupted_loads, crashes, reload_success | 0, 0, > 99% |
| **Multi-Module** | isolation, reload_ok, execution_order | 100%, true, preserved |

---

## 🎯 Validation Globale

### Pour que la suite de tests PASSE:

✅ **TOUS** les scénarios Phase 1 (1-2-3) doivent PASSER leurs MUST PASS
✅ **Au moins 80%** des scénarios Phase 2 (4-5) doivent PASSER leurs MUST PASS

### Pour déclarer le système "Production Ready":

✅ Tous les scénarios MUST PASS
✅ Au moins 70% des SHOULD PASS
✅ Aucun crash dans aucun scénario
✅ Stress test 1h (nightly) PASSE

---

## 📝 Révision des Seuils

Les seuils peuvent être ajustés après analyse des résultats initiaux si:
1. Hardware différent (plus lent) justifie seuils plus permissifs
2. Optimisations permettent seuils plus stricts
3. Nouvelles fonctionnalités changent les contraintes

**Process de révision**:
1. Documenter la justification dans ce fichier
2. Mettre à jour les scénarios correspondants
3. Re-run tous les tests avec nouveaux seuils
4. Commit changes avec message clair

---

**Dernière mise à jour**: 2025-11-13
**Version des seuils**: 1.0
