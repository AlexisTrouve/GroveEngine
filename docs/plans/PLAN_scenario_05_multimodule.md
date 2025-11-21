# Scénario 5: Multi-Module Orchestration

**Priorité**: ⭐⭐ IMPORTANT
**Phase**: 2 (SHOULD HAVE)
**Durée estimée**: ~2 minutes
**Effort implémentation**: ~4-6 heures

---

## 🎯 Objectif

Valider que le hot-reload d'un module spécifique n'affecte pas les autres modules:
- Isolation complète entre modules
- Ordre d'exécution préservé
- State non corrompu dans modules non-reloadés
- Communication inter-modules fonctionnelle (si applicable)

**Critique pour systèmes multi-modules en production.**

---

## 📋 Description

### Setup
Charger 3 modules avec dépendances:
1. **MapModule**: Grille 100x100, pas de dépendance
2. **TankModule**: Dépend de MapModule (positions valides)
3. **ProductionModule**: Spawne des tanks, dépend de TankModule

### Scénario
1. Exécuter pendant 30 secondes avec les 3 modules
2. Hot-reload **ProductionModule** uniquement à t=15s
3. Vérifier que:
   - MapModule non affecté (state identique)
   - TankModule non affecté (tanks toujours présents)
   - ProductionModule rechargé (version mise à jour)
   - Ordre d'exécution toujours: Map → Tank → Production

---

## 🏗️ Implémentation

### Module Dependencies

```cpp
// MapModule.h
class MapModule : public IModule {
public:
    bool isPositionValid(float x, float y) const {
        int ix = static_cast<int>(x);
        int iy = static_cast<int>(y);
        return ix >= 0 && ix < mapSize && iy >= 0 && iy < mapSize;
    }

    void process(float deltaTime) override {
        // Update fog of war, etc.
        frameCount++;
    }

private:
    int mapSize = 100;
    int frameCount = 0;
};

// TankModule.h
class TankModule : public IModule {
public:
    void setMapModule(MapModule* map) { mapModule = map; }

    void process(float deltaTime) override {
        for (auto& tank : tanks) {
            // Vérifier position valide via MapModule
            if (mapModule && !mapModule->isPositionValid(tank.x, tank.y)) {
                // Correction position
                tank.x = std::clamp(tank.x, 0.0f, 99.0f);
                tank.y = std::clamp(tank.y, 0.0f, 99.0f);
            }

            // Update position
            tank.x += tank.vx * deltaTime;
            tank.y += tank.vy * deltaTime;
        }
        frameCount++;
    }

    int getTankCount() const { return tanks.size(); }

private:
    std::vector<Tank> tanks;
    MapModule* mapModule = nullptr;
    int frameCount = 0;
};

// ProductionModule.h
class ProductionModule : public IModule {
public:
    void setTankModule(TankModule* tanks) { tankModule = tanks; }

    void process(float deltaTime) override {
        timeSinceLastSpawn += deltaTime;

        if (timeSinceLastSpawn >= 1.0f) {
            // Notifier TankModule de spawner un tank
            // (Simplification: on log juste ici)
            spawned++;
            timeSinceLastSpawn -= 1.0f;
            logger->debug("Spawned tank #{}", spawned);
        }
        frameCount++;
    }

private:
    TankModule* tankModule = nullptr;
    int spawned = 0;
    float timeSinceLastSpawn = 0.0f;
    int frameCount = 0;
    std::string version = "v1.0";
};
```

### Test Principal

```cpp
// test_05_multimodule.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestReporter.h"

int main() {
    TestReporter reporter("Multi-Module Orchestration");
    TestMetrics metrics;

    std::cout << "================================================================================\n";
    std::cout << "MULTI-MODULE ORCHESTRATION TEST\n";
    std::cout << "================================================================================\n\n";

    // === SETUP - Charger 3 modules ===
    DebugEngine engine;

    engine.loadModule("MapModule", "build/modules/libMapModule.so");
    engine.loadModule("TankModule", "build/modules/libTankModule.so");
    engine.loadModule("ProductionModule", "build/modules/libProductionModule.so");

    auto mapConfig = createJsonConfig({{"mapSize", 100}});
    auto tankConfig = createJsonConfig({{"tankCount", 50}});
    auto prodConfig = createJsonConfig({{"version", "v1.0"}});

    engine.initializeModule("MapModule", mapConfig);
    engine.initializeModule("TankModule", tankConfig);
    engine.initializeModule("ProductionModule", prodConfig);

    std::cout << "Loaded 3 modules: Map, Tank, Production\n";

    // === PHASE 1: Pre-Reload (15s) ===
    std::cout << "\nPhase 1: Running 15s before reload...\n";

    for (int frame = 0; frame < 900; frame++) { // 15s * 60 FPS
        engine.update(1.0f / 60.0f);

        if (frame % 300 == 0) { // Progress toutes les 5s
            std::cout << "  Frame " << frame << "/900\n";
        }
    }

    // Snapshot states AVANT reload
    auto mapStateBefore = engine.getModuleState("MapModule");
    auto tankStateBefore = engine.getModuleState("TankModule");
    auto prodStateBefore = engine.getModuleState("ProductionModule");

    auto* mapJsonBefore = dynamic_cast<JsonDataNode*>(mapStateBefore.get());
    auto* tankJsonBefore = dynamic_cast<JsonDataNode*>(tankStateBefore.get());
    auto* prodJsonBefore = dynamic_cast<JsonDataNode*>(prodStateBefore.get());

    int mapFramesBefore = mapJsonBefore->getJsonData()["frameCount"];
    int tankFramesBefore = tankJsonBefore->getJsonData()["frameCount"];
    int tankCountBefore = tankJsonBefore->getJsonData()["tanks"].size();
    std::string prodVersionBefore = prodJsonBefore->getJsonData()["version"];

    std::cout << "\nState snapshot BEFORE reload:\n";
    std::cout << "  MapModule frames:     " << mapFramesBefore << "\n";
    std::cout << "  TankModule frames:    " << tankFramesBefore << "\n";
    std::cout << "  TankModule tanks:     " << tankCountBefore << "\n";
    std::cout << "  ProductionModule ver: " << prodVersionBefore << "\n\n";

    // === HOT-RELOAD ProductionModule UNIQUEMENT ===
    std::cout << "Hot-reloading ProductionModule ONLY...\n";

    // Modifier version dans source
    modifySourceFile("tests/modules/ProductionModule.cpp", "v1.0", "v2.0 HOT-RELOADED");

    // Recompiler
    system("cmake --build build --target ProductionModule 2>&1 | grep -v '^\\['");

    // Reload
    auto reloadStart = std::chrono::high_resolution_clock::now();
    engine.reloadModule("ProductionModule");
    auto reloadEnd = std::chrono::high_resolution_clock::now();

    float reloadTime = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();
    metrics.recordReloadTime(reloadTime);

    std::cout << "  → Reload completed in " << reloadTime << "ms\n\n";

    // === VÉRIFICATIONS POST-RELOAD ===
    std::cout << "Verifying isolation...\n";

    auto mapStateAfter = engine.getModuleState("MapModule");
    auto tankStateAfter = engine.getModuleState("TankModule");
    auto prodStateAfter = engine.getModuleState("ProductionModule");

    auto* mapJsonAfter = dynamic_cast<JsonDataNode*>(mapStateAfter.get());
    auto* tankJsonAfter = dynamic_cast<JsonDataNode*>(tankStateAfter.get());
    auto* prodJsonAfter = dynamic_cast<JsonDataNode*>(prodStateAfter.get());

    int mapFramesAfter = mapJsonAfter->getJsonData()["frameCount"];
    int tankFramesAfter = tankJsonAfter->getJsonData()["frameCount"];
    int tankCountAfter = tankJsonAfter->getJsonData()["tanks"].size();
    std::string prodVersionAfter = prodJsonAfter->getJsonData()["version"];

    std::cout << "\nState snapshot AFTER reload:\n";
    std::cout << "  MapModule frames:     " << mapFramesAfter << "\n";
    std::cout << "  TankModule frames:    " << tankFramesAfter << "\n";
    std::cout << "  TankModule tanks:     " << tankCountAfter << "\n";
    std::cout << "  ProductionModule ver: " << prodVersionAfter << "\n\n";

    // Assertions: Isolation
    bool mapUnaffected = (mapFramesAfter == mapFramesBefore);
    bool tankUnaffected = (tankFramesAfter == tankFramesBefore) && (tankCountAfter == tankCountBefore);
    bool prodReloaded = (prodVersionAfter.find("v2.0") != std::string::npos);

    ASSERT_TRUE(mapUnaffected, "MapModule should be UNAFFECTED by ProductionModule reload");
    reporter.addAssertion("map_unaffected", mapUnaffected);

    ASSERT_TRUE(tankUnaffected, "TankModule should be UNAFFECTED by ProductionModule reload");
    reporter.addAssertion("tank_unaffected", tankUnaffected);

    ASSERT_TRUE(prodReloaded, "ProductionModule should be RELOADED with new version");
    reporter.addAssertion("production_reloaded", prodReloaded);

    // === PHASE 2: Post-Reload (15s) ===
    std::cout << "\nPhase 2: Running 15s after reload...\n";

    for (int frame = 0; frame < 900; frame++) { // 15s * 60 FPS
        engine.update(1.0f / 60.0f);

        if (frame % 300 == 0) {
            std::cout << "  Frame " << frame << "/900\n";
        }
    }

    // Vérifier ordre d'exécution (détection via logs ou instrumentation)
    // Pour l'instant, vérifier que tous les modules ont continué de process
    auto mapStateFinal = engine.getModuleState("MapModule");
    auto tankStateFinal = engine.getModuleState("TankModule");
    auto prodStateFinal = engine.getModuleState("ProductionModule");

    auto* mapJsonFinal = dynamic_cast<JsonDataNode*>(mapStateFinal.get());
    auto* tankJsonFinal = dynamic_cast<JsonDataNode*>(tankStateFinal.get());
    auto* prodJsonFinal = dynamic_cast<JsonDataNode*>(prodStateFinal.get());

    int mapFramesFinal = mapJsonFinal->getJsonData()["frameCount"];
    int tankFramesFinal = tankJsonFinal->getJsonData()["frameCount"];
    int prodFramesFinal = prodJsonFinal->getJsonData()["frameCount"];

    // Tous devraient avoir ~1800 frames (30s * 60 FPS)
    ASSERT_WITHIN(mapFramesFinal, 1800, 50, "MapModule should have ~1800 frames");
    ASSERT_WITHIN(tankFramesFinal, 1800, 50, "TankModule should have ~1800 frames");
    ASSERT_WITHIN(prodFramesFinal, 900, 50, "ProductionModule should have ~900 frames (restarted)");

    reporter.addMetric("map_frames_final", mapFramesFinal);
    reporter.addMetric("tank_frames_final", tankFramesFinal);
    reporter.addMetric("prod_frames_final", prodFramesFinal);

    // === VÉRIFICATIONS FINALES ===

    ASSERT_LT(reloadTime, 1000.0f, "Reload time should be < 1s");
    reporter.addMetric("reload_time_ms", reloadTime);

    // === RAPPORT FINAL ===
    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "MULTI-MODULE ORCHESTRATION SUMMARY\n";
    std::cout << "================================================================================\n";
    std::cout << "Isolation Test:\n";
    std::cout << "  MapModule unaffected:      " << (mapUnaffected ? "✓" : "✗") << "\n";
    std::cout << "  TankModule unaffected:     " << (tankUnaffected ? "✓" : "✗") << "\n";
    std::cout << "  ProductionModule reloaded: " << (prodReloaded ? "✓" : "✗") << "\n\n";

    std::cout << "Final Frame Counts:\n";
    std::cout << "  MapModule:        " << mapFramesFinal << " (~1800 expected)\n";
    std::cout << "  TankModule:       " << tankFramesFinal << " (~1800 expected)\n";
    std::cout << "  ProductionModule: " << prodFramesFinal << " (~900 expected, restarted)\n\n";

    std::cout << "Performance:\n";
    std::cout << "  Reload time:      " << reloadTime << "ms\n";
    std::cout << "================================================================================\n\n";

    reporter.printFinalReport();

    return reporter.getExitCode();
}
```

---

## 📊 Métriques Collectées

| Métrique | Description | Seuil |
|----------|-------------|-------|
| **map_unaffected** | MapModule non affecté par reload | true |
| **tank_unaffected** | TankModule non affecté par reload | true |
| **production_reloaded** | ProductionModule bien rechargé | true |
| **reload_time_ms** | Temps de reload | < 1000ms |
| **map_frames_final** | Frames processées par MapModule | ~1800 |
| **tank_frames_final** | Frames processées par TankModule | ~1800 |
| **prod_frames_final** | Frames processées par ProductionModule | ~900 (reset) |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ MapModule unaffected (frameCount et state identiques)
2. ✅ TankModule unaffected (tankCount et frameCount identiques)
3. ✅ ProductionModule reloaded (version mise à jour)
4. ✅ Reload time < 1s
5. ✅ Aucun crash
6. ✅ Ordre d'exécution préservé (Map → Tank → Production)

### NICE TO HAVE
1. ✅ Reload time < 500ms
2. ✅ Zero impact sur FPS pendant reload
3. ✅ Communication inter-modules fonctionne après reload

---

## 🔧 Execution Order Verification

### Option 1: Instrumentation dans SequentialModuleSystem

```cpp
void SequentialModuleSystem::update(float deltaTime) {
    logger->trace("╔════════════════════════════════════════");
    logger->trace("║ UPDATE CYCLE START");

    for (const auto& [name, module] : modules) {
        logger->trace("║ → Processing: {}", name);
        module->process(deltaTime);
    }

    logger->trace("║ UPDATE CYCLE END");
    logger->trace("╚════════════════════════════════════════");
}
```

### Option 2: Vérification via frameCount delta

```cpp
// Si l'ordre est Map → Tank → Production:
// - MapModule devrait voir deltaTime en premier
// - Si on reload Production, Map et Tank ne sont pas affectés
// - frameCount de Map et Tank continue linéairement
// - frameCount de Production redémarre à 0
```

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| MapModule affected | State mal isolé | FAIL - fix state management |
| TankModule affected | Shared memory corruption | FAIL - fix isolation |
| Production not reloaded | Reload raté | FAIL - check reload logic |
| Execution order changed | Module registry corrupted | FAIL - fix registry |
| Crash during reload | Dependency on unloaded module | FAIL - fix dependencies |

---

## 📝 Output Attendu

```
================================================================================
MULTI-MODULE ORCHESTRATION TEST
================================================================================

Loaded 3 modules: Map, Tank, Production

Phase 1: Running 15s before reload...
  Frame 0/900
  Frame 300/900
  Frame 600/900

State snapshot BEFORE reload:
  MapModule frames:     900
  TankModule frames:    900
  TankModule tanks:     50
  ProductionModule ver: v1.0

Hot-reloading ProductionModule ONLY...
  → Reload completed in 412ms

Verifying isolation...

State snapshot AFTER reload:
  MapModule frames:     900
  TankModule frames:    900
  TankModule tanks:     50
  ProductionModule ver: v2.0 HOT-RELOADED

Phase 2: Running 15s after reload...
  Frame 0/900
  Frame 300/900
  Frame 600/900

================================================================================
MULTI-MODULE ORCHESTRATION SUMMARY
================================================================================
Isolation Test:
  MapModule unaffected:      ✓
  TankModule unaffected:     ✓
  ProductionModule reloaded: ✓

Final Frame Counts:
  MapModule:        1800 (~1800 expected)
  TankModule:       1800 (~1800 expected)
  ProductionModule: 900 (~900 expected, restarted)

Performance:
  Reload time:      412ms
================================================================================

METRICS
================================================================================
  Map unaffected:        true           ✓
  Tank unaffected:       true           ✓
  Production reloaded:   true           ✓
  Reload time:           412ms          (threshold: < 1000ms) ✓

ASSERTIONS
================================================================================
  ✓ map_unaffected
  ✓ tank_unaffected
  ✓ production_reloaded
  ✓ reload_time < 1s

Result: ✅ PASSED

================================================================================
```

---

## 📅 Planning

**Jour 1 (3h):**
- Implémenter MapModule, adapter TankModule avec dépendances
- Implémenter ProductionModule

**Jour 2 (3h):**
- Implémenter test_05_multimodule.cpp
- Instrumentation pour vérifier ordre d'exécution
- Debug + validation

---

**Prochaine étape**: `architecture_tests.md` (détails des helpers communs)
