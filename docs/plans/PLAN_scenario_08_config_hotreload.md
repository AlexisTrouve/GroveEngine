# Scénario 8: Config Hot-Reload

**Priorité**: ⭐ NICE TO HAVE
**Phase**: 3 (NICE TO HAVE)
**Durée estimée**: ~1 minute
**Effort implémentation**: ~3-4 heures

---

## 🎯 Objectif

Valider que le système peut modifier la configuration d'un module à la volée sans redémarrage, avec:
- Changement de paramètres en temps réel
- Reconfiguration sans perte de state
- Support de multiples types de configuration (scalaires, listes, objets)
- Validation des valeurs et rollback en cas d'erreur

---

## 📋 Description

### Setup Initial
1. Charger `ConfigurableModule` avec configuration de base:
   - `spawnRate`: 10 entités/seconde
   - `maxEntities`: 50
   - `entitySpeed`: 5.0
   - `colors`: ["red", "blue"]
   - `physics.gravity`: 9.8
   - `physics.friction`: 0.5
2. Spawner des entités pendant 10 secondes
3. Vérifier que les entités respectent la config initiale

### Phase 1: Config Hot-Reload Simple
1. À t=10s, modifier configuration:
   - `spawnRate`: 20 (doubler le taux de spawn)
   - `entitySpeed`: 10.0 (doubler la vitesse)
2. Appliquer config via `updateConfig()` sans reload du module
3. Vérifier que:
   - Nouvelles entités utilisent nouvelle config
   - Entités existantes **gardent** leur config d'origine (continuité)
   - Pas de perte de state

### Phase 2: Config Hot-Reload Complexe
1. À t=20s, modifier configuration avancée:
   - `maxEntities`: 100 (augmenter limite)
   - `colors`: ["green", "yellow", "purple"] (nouvelles couleurs)
   - `physics.gravity`: 1.6 (gravité lunaire)
2. Vérifier que:
   - Nouvelles entités spawnent avec nouvelles couleurs
   - Physique appliquée correctement
   - Limite respectée

### Phase 3: Config Invalide + Rollback
1. À t=30s, tenter d'appliquer config invalide:
   - `spawnRate`: -5 (négatif = invalide)
   - `maxEntities`: 1000000 (trop grand)
2. Vérifier que:
   - Config invalide rejetée
   - Config précédente (valide) restaurée
   - Aucune corruption de state
   - Erreur loggée clairement

### Phase 4: Config Partielle
1. À t=40s, modifier seulement une partie de la config:
   - `entitySpeed`: 2.0
   - (laisser tous les autres paramètres inchangés)
2. Vérifier que:
   - Seul `entitySpeed` est modifié
   - Autres paramètres préservés
   - Merge correct

---

## 🏗️ Implémentation

### ConfigurableModule Structure

```cpp
// ConfigurableModule.h
class ConfigurableModule : public IModule {
public:
    struct Entity {
        float x, y;
        float vx, vy;
        std::string color;
        float speed;      // Config snapshot à la création
        int id;
    };

    struct Config {
        int spawnRate;         // Entités par seconde
        int maxEntities;       // Limite totale
        float entitySpeed;     // Vitesse de déplacement
        std::vector<std::string> colors;  // Couleurs disponibles

        struct Physics {
            float gravity;
            float friction;
        } physics;

        // Validation
        bool validate(std::string& errorMsg) const {
            if (spawnRate < 0 || spawnRate > 1000) {
                errorMsg = "spawnRate must be in [0, 1000]";
                return false;
            }
            if (maxEntities < 1 || maxEntities > 10000) {
                errorMsg = "maxEntities must be in [1, 10000]";
                return false;
            }
            if (entitySpeed < 0.0f) {
                errorMsg = "entitySpeed must be >= 0";
                return false;
            }
            if (colors.empty()) {
                errorMsg = "colors list cannot be empty";
                return false;
            }
            return true;
        }
    };

    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;

    // NOUVELLE API: Config hot-reload
    bool updateConfig(std::shared_ptr<IDataNode> newConfig);

    bool isIdle() const override { return true; }

private:
    Config currentConfig;
    Config previousConfig;  // Pour rollback
    std::vector<Entity> entities;
    float spawnAccumulator = 0.0f;
    int nextEntityId = 0;

    void spawnEntity();
    void updateEntity(Entity& entity, float dt);
    Config parseConfig(std::shared_ptr<IDataNode> configNode);
};
```

### Configuration Format (JSON)

```json
{
    "spawnRate": 10,
    "maxEntities": 50,
    "entitySpeed": 5.0,
    "colors": ["red", "blue"],
    "physics": {
        "gravity": 9.8,
        "friction": 0.5
    }
}
```

### Test Principal

```cpp
// test_08_config_hotreload.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestAssertions.h"
#include "helpers/TestReporter.h"

int main() {
    TestReporter reporter("Config Hot-Reload");
    TestMetrics metrics;

    // === SETUP ===
    DebugEngine engine;
    engine.loadModule("ConfigurableModule", "build/modules/libConfigurableModule.so");

    // Config initiale
    auto config = createJsonConfig({
        {"spawnRate", 10},
        {"maxEntities", 50},
        {"entitySpeed", 5.0},
        {"colors", json::array({"red", "blue"})},
        {"physics", {
            {"gravity", 9.8},
            {"friction", 0.5}
        }}
    });
    engine.initializeModule("ConfigurableModule", config);

    std::cout << "=== Phase 0: Initial config (10s) ===\n";

    // === PHASE 0: Baseline (10s) ===
    for (int i = 0; i < 600; i++) { // 10s * 60 FPS
        engine.update(1.0f/60.0f);
        metrics.recordFPS(60.0f);
    }

    auto state0 = engine.getModuleState("ConfigurableModule");
    auto json0 = getJsonFromState(state0);
    int entityCount0 = json0["entities"].size();

    // On devrait avoir ~100 entités (10/s * 10s)
    ASSERT_WITHIN(entityCount0, 100, 10, "Should have ~100 entities after 10s");
    reporter.addAssertion("initial_spawn_rate", true);

    // Vérifier vitesse des entités
    for (const auto& entity : json0["entities"]) {
        float speed = entity["speed"];
        ASSERT_EQ(speed, 5.0f, "Initial entity speed should be 5.0");
    }

    std::cout << "✓ Baseline: " << entityCount0 << " entities spawned\n";

    // === PHASE 1: Simple Config Change (10s) ===
    std::cout << "\n=== Phase 1: Doubling spawn rate and speed (10s) ===\n";

    auto newConfig1 = createJsonConfig({
        {"spawnRate", 20},      // Double spawn rate
        {"maxEntities", 50},
        {"entitySpeed", 10.0},  // Double speed
        {"colors", json::array({"red", "blue"})},
        {"physics", {
            {"gravity", 9.8},
            {"friction", 0.5}
        }}
    });

    bool updateResult1 = engine.updateModuleConfig("ConfigurableModule", newConfig1);
    ASSERT_TRUE(updateResult1, "Config update should succeed");
    reporter.addAssertion("config_update_simple", updateResult1);

    for (int i = 0; i < 600; i++) { // 10s * 60 FPS
        engine.update(1.0f/60.0f);
    }

    auto state1 = engine.getModuleState("ConfigurableModule");
    auto json1 = getJsonFromState(state1);
    int entityCount1 = json1["entities"].size();

    // On devrait maintenant avoir ~100 (initial) + ~200 (20/s * 10s) = ~300 entités
    // Mais max = 50, donc on devrait avoir exactement 50
    ASSERT_EQ(entityCount1, 50, "Should respect maxEntities limit");
    reporter.addAssertion("max_entities_respected", entityCount1 == 50);

    // Vérifier que les NOUVELLES entités ont speed = 10.0
    int newEntityCount = 0;
    for (const auto& entity : json1["entities"]) {
        if (entity["id"] >= entityCount0) { // Nouvelle entité
            float speed = entity["speed"];
            ASSERT_EQ(speed, 10.0f, "New entities should have speed 10.0");
            newEntityCount++;
        }
    }
    std::cout << "✓ Config applied: " << newEntityCount << " new entities with speed 10.0\n";

    // === PHASE 2: Complex Config Change (10s) ===
    std::cout << "\n=== Phase 2: Complex config changes (10s) ===\n";

    auto newConfig2 = createJsonConfig({
        {"spawnRate", 15},
        {"maxEntities", 100},    // Augmenter limite
        {"entitySpeed", 7.5},
        {"colors", json::array({"green", "yellow", "purple"})},  // Nouvelles couleurs
        {"physics", {
            {"gravity", 1.6},    // Gravité lunaire
            {"friction", 0.2}
        }}
    });

    bool updateResult2 = engine.updateModuleConfig("ConfigurableModule", newConfig2);
    ASSERT_TRUE(updateResult2, "Config update 2 should succeed");

    int entitiesBeforePhase2 = entityCount1;

    for (int i = 0; i < 600; i++) { // 10s * 60 FPS
        engine.update(1.0f/60.0f);
    }

    auto state2 = engine.getModuleState("ConfigurableModule");
    auto json2 = getJsonFromState(state2);
    int entityCount2 = json2["entities"].size();

    // Vérifier que la limite a augmenté
    ASSERT_LT(entitiesBeforePhase2, entityCount2, "Entity count should have increased");
    ASSERT_LE(entityCount2, 100, "Should respect new maxEntities = 100");

    // Vérifier couleurs des nouvelles entités
    std::set<std::string> newColors;
    for (const auto& entity : json2["entities"]) {
        if (entity["id"] >= entitiesBeforePhase2) {
            newColors.insert(entity["color"]);
        }
    }

    bool hasNewColors = newColors.count("green") || newColors.count("yellow") || newColors.count("purple");
    ASSERT_TRUE(hasNewColors, "New entities should use new color palette");
    reporter.addAssertion("new_colors_applied", hasNewColors);

    std::cout << "✓ Complex config applied: " << entityCount2 << " total entities, new colors: ";
    for (const auto& color : newColors) std::cout << color << " ";
    std::cout << "\n";

    // === PHASE 3: Invalid Config + Rollback (5s) ===
    std::cout << "\n=== Phase 3: Invalid config rejection (5s) ===\n";

    auto invalidConfig = createJsonConfig({
        {"spawnRate", -5},       // INVALIDE: négatif
        {"maxEntities", 1000000}, // INVALIDE: trop grand
        {"entitySpeed", 5.0},
        {"colors", json::array({"red"})},
        {"physics", {
            {"gravity", 9.8},
            {"friction", 0.5}
        }}
    });

    bool updateResult3 = engine.updateModuleConfig("ConfigurableModule", invalidConfig);
    ASSERT_FALSE(updateResult3, "Invalid config should be rejected");
    reporter.addAssertion("invalid_config_rejected", !updateResult3);

    // Continuer l'exécution - devrait utiliser la config précédente (valide)
    for (int i = 0; i < 300; i++) { // 5s * 60 FPS
        engine.update(1.0f/60.0f);
    }

    auto state3 = engine.getModuleState("ConfigurableModule");
    auto json3 = getJsonFromState(state3);

    // Vérifier que la config précédente (Phase 2) est toujours active
    // On devrait avoir spawn à 15/s, pas -5 ni 0
    int entityCount3 = json3["entities"].size();
    ASSERT_GT(entityCount3, entityCount2, "Should continue spawning with previous valid config");
    reporter.addAssertion("config_rollback_works", entityCount3 > entityCount2);

    std::cout << "✓ Rollback successful: config unchanged, " << (entityCount3 - entityCount2) << " entities spawned\n";

    // === PHASE 4: Partial Config Update (5s) ===
    std::cout << "\n=== Phase 4: Partial config update (5s) ===\n";

    auto partialConfig = createJsonConfig({
        {"entitySpeed", 2.0}  // Modifier seulement la vitesse
    });

    bool updateResult4 = engine.updateModuleConfigPartial("ConfigurableModule", partialConfig);
    ASSERT_TRUE(updateResult4, "Partial config update should succeed");

    for (int i = 0; i < 300; i++) { // 5s * 60 FPS
        engine.update(1.0f/60.0f);
    }

    auto state4 = engine.getModuleState("ConfigurableModule");
    auto json4 = getJsonFromState(state4);

    // Vérifier que les nouvelles entités ont speed = 2.0
    // Et que les autres paramètres (colors, etc.) sont inchangés de Phase 2
    bool foundNewSpeed = false;
    bool foundOldColors = false;

    for (const auto& entity : json4["entities"]) {
        if (entity["id"] >= entityCount3) {
            if (entity["speed"] == 2.0f) foundNewSpeed = true;
            std::string color = entity["color"];
            if (color == "green" || color == "yellow" || color == "purple") {
                foundOldColors = true;
            }
        }
    }

    ASSERT_TRUE(foundNewSpeed, "New entities should have updated speed");
    ASSERT_TRUE(foundOldColors, "Colors should be preserved from Phase 2");
    reporter.addAssertion("partial_update_works", foundNewSpeed && foundOldColors);

    std::cout << "✓ Partial update: speed changed, other params preserved\n";

    // === VÉRIFICATIONS FINALES ===

    // Memory stability
    size_t memGrowth = metrics.getMemoryGrowth();
    ASSERT_LT(memGrowth, 10 * 1024 * 1024, "Memory growth should be < 10MB");
    reporter.addMetric("memory_growth_mb", memGrowth / (1024.0f * 1024.0f));

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
| **config_update_time_ms** | Temps d'application de nouvelle config | < 50ms |
| **memory_growth_mb** | Croissance mémoire totale | < 10MB |
| **config_rollback_time_ms** | Temps de rollback si invalide | < 10ms |
| **partial_update_accuracy** | Précision du merge partiel | 100% |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ Config update appliquée en < 50ms
2. ✅ Nouvelles entités utilisent nouvelle config
3. ✅ Entités existantes préservent leur config (continuité)
4. ✅ Config invalide rejetée + rollback automatique
5. ✅ Partial update fusionne correctement
6. ✅ Aucun crash

### NICE TO HAVE
1. ✅ Config update en < 10ms (très rapide)
2. ✅ Validation détaillée avec messages d'erreur clairs
3. ✅ Support de nested objects (physics.gravity)
4. ✅ Historique des configs (undo/redo)

---

## 🔧 API Nécessaires dans IModule

### Nouvelle méthode à ajouter

```cpp
class IModule {
public:
    // Méthodes existantes
    virtual void initialize(std::shared_ptr<IDataNode> config) = 0;
    virtual void process(float deltaTime) = 0;
    virtual std::shared_ptr<IDataNode> getState() const = 0;
    virtual void setState(std::shared_ptr<IDataNode> state) = 0;
    virtual bool isIdle() const = 0;

    // NOUVELLE: Config hot-reload
    virtual bool updateConfig(std::shared_ptr<IDataNode> newConfig) {
        // Default implementation: reject
        return false;
    }

    // NOUVELLE: Partial config update
    virtual bool updateConfigPartial(std::shared_ptr<IDataNode> partialConfig) {
        // Default implementation: delegate to full update
        return updateConfig(partialConfig);
    }
};
```

### DebugEngine Support

```cpp
class DebugEngine {
public:
    // Nouvelle méthode
    bool updateModuleConfig(const std::string& moduleName, std::shared_ptr<IDataNode> newConfig) {
        auto it = modules.find(moduleName);
        if (it == modules.end()) return false;

        return it->second->updateConfig(newConfig);
    }

    bool updateModuleConfigPartial(const std::string& moduleName, std::shared_ptr<IDataNode> partialConfig) {
        auto it = modules.find(moduleName);
        if (it == modules.end()) return false;

        return it->second->updateConfigPartial(partialConfig);
    }
};
```

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| Config rejetée alors que valide | Validation trop stricte | FAIL - ajuster règles |
| Config invalide acceptée | Pas de validation | FAIL - implémenter validate() |
| State corrompu après config change | Mauvaise gestion | FAIL - séparer config et state |
| Rollback échoue | Pas de backup | FAIL - sauvegarder prev config |
| Partial update écrase tout | Merge incorrect | FAIL - implémenter merge proper |

---

## 📝 Output Attendu

```
================================================================================
TEST: Config Hot-Reload
================================================================================

=== Phase 0: Initial config (10s) ===
✓ Baseline: 102 entities spawned

=== Phase 1: Doubling spawn rate and speed (10s) ===
✓ Config applied: 47 new entities with speed 10.0

=== Phase 2: Complex config changes (10s) ===
✓ Complex config applied: 98 total entities, new colors: green purple yellow

=== Phase 3: Invalid config rejection (5s) ===
[ERROR] Config validation failed: spawnRate must be in [0, 1000]
[ERROR] Config validation failed: maxEntities must be in [1, 10000]
✓ Rollback successful: config unchanged, 73 entities spawned

=== Phase 4: Partial config update (5s) ===
✓ Partial update: speed changed, other params preserved

================================================================================
METRICS
================================================================================
  Config update time:   8ms           (threshold: < 50ms)  ✓
  Memory growth:        4.2MB         (threshold: < 10MB)  ✓

================================================================================
ASSERTIONS
================================================================================
  ✓ initial_spawn_rate
  ✓ config_update_simple
  ✓ max_entities_respected
  ✓ new_colors_applied
  ✓ invalid_config_rejected
  ✓ config_rollback_works
  ✓ partial_update_works
  ✓ no_crashes

Result: ✅ PASSED

================================================================================
```

---

## 📅 Planning

**Jour 8 (3h):**
- Implémenter `updateConfig()` et `updateConfigPartial()` dans IModule
- Implémenter ConfigurableModule avec validation

**Jour 9 (1h):**
- Implémenter test_08_config_hotreload.cpp
- Debug + validation

---

## 🎯 Valeur Ajoutée

Ce test valide une fonctionnalité **cruciale pour le développement** :
- **Tweaking en temps réel** : Ajuster paramètres de gameplay sans redémarrer
- **A/B testing** : Tester différentes configs rapidement
- **Debug facilité** : Modifier valeurs pour reproduire bugs
- **Production-ready** : Hot-config peut servir en prod (feature flags, tuning)

Contrairement au hot-reload de code (recompilation), le hot-reload de config est **instantané** et ne nécessite pas de rebuild.

---

**Prochaine étape**: Implémentation
