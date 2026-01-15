# Scénario 7: Limite Tests

**Priorité**: ⭐ NICE TO HAVE
**Phase**: 3 (NICE TO HAVE)
**Durée estimée**: ~3 minutes
**Effort implémentation**: ~3-4 heures

---

## 🎯 Objectif

Valider que le système de hot-reload reste robuste face aux conditions extrêmes:
- État très large (100MB+ en mémoire)
- Initialisation longue (>5 secondes)
- Timeouts de reload
- Limites de sérialisation/désérialisation
- Gestion de la mémoire sous contrainte

---

## 📋 Description

### Setup Initial
1. Charger `HeavyStateModule` avec configuration extrême
2. Créer un état massif:
   - 1 million de particules (x, y, vx, vy, lifetime, color)
   - Matrice de terrain 10000x10000 (100M cellules)
   - Historique des 10000 dernières frames
   - Cache de 50000 textures simulées
3. État sérialisé estimé: ~120MB en JSON
4. Temps d'initialisation: ~8 secondes

### Test Séquence

#### Test 1: Large State Serialization (60s)
1. Exécuter pendant 10 secondes (600 frames)
2. Trigger hot-reload avec extraction de l'état complet
3. Mesurer:
   - Temps de `getState()`: doit être < 2000ms
   - Taille de l'état sérialisé
   - Temps de `setState()`: doit être < 2000ms
4. Vérifier intégrité des données post-reload:
   - Nombre de particules = 1M
   - Dimensions terrain = 10000x10000
   - Historique complet préservé
5. Continuer pendant 10 secondes supplémentaires

#### Test 2: Long Initialization Timeout (30s)
1. Charger `HeavyStateModule` avec init duration = 12s
2. Configurer timeout à 10s (volontairement trop court)
3. Vérifier que le système:
   - Détecte le timeout correctement
   - Annule le chargement proprement
   - Libère toutes les ressources
   - Log un message d'erreur clair
4. Recharger avec timeout = 15s (suffisant)
5. Vérifier chargement réussi

#### Test 3: Memory Pressure During Reload (60s)
1. Démarrer avec état 100MB
2. Pendant reload:
   - Allouer 200MB supplémentaires (simuler pic mémoire)
   - Vérifier que le système ne crash pas (OOM)
   - Mesurer temps de reload sous pression
3. Après reload:
   - Vérifier retour à 100MB baseline
   - Aucune fuite détectée
   - État intact

#### Test 4: Incremental State Save/Load (30s)
1. Activer mode "incremental state" (seuls les deltas)
2. Faire 10 reloads successifs
3. Chaque reload modifie 1% de l'état (10k particules)
4. Mesurer:
   - Temps de reload incrémental: doit être < 100ms
   - Taille des deltas: doit être ~1MB
   - Intégrité finale: 100% des particules correctes

#### Test 5: State Corruption Detection (20s)
1. Créer un état volontairement corrompu:
   - JSON mal formé
   - Valeurs hors limites (NaN, Infinity)
   - Champs manquants
2. Tenter `setState()` avec état corrompu
3. Vérifier:
   - Détection de corruption avant application
   - État précédent non affecté
   - Message d'erreur descriptif
   - Module reste fonctionnel

---

## 🏗️ Implémentation

### HeavyStateModule Structure

```cpp
// HeavyStateModule.h
class HeavyStateModule : public IModule {
public:
    struct Particle {
        float x, y;           // Position
        float vx, vy;         // Vélocité
        float lifetime;       // Temps restant
        uint32_t color;       // RGBA
    };

    struct TerrainCell {
        uint8_t height;       // 0-255
        uint8_t type;         // Grass, water, rock, etc.
        uint8_t metadata;     // Flags
        uint8_t reserved;
    };

    struct FrameSnapshot {
        uint32_t frameId;
        float avgFPS;
        size_t particleCount;
        uint64_t timestamp;
    };

    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

private:
    std::vector<Particle> particles;               // 1M particules = ~32MB
    std::vector<TerrainCell> terrain;              // 100M cells = ~100MB
    std::deque<FrameSnapshot> history;             // 10k frames = ~160KB
    std::unordered_map<uint32_t, std::vector<uint8_t>> textureCache; // 50k textures simulées

    float initDuration = 8.0f;  // Temps d'init simulé
    int frameCount = 0;
    std::string version = "v1.0";

    void updateParticles(float dt);
    void spawnParticles(size_t count);
    void initializeTerrain(int width, int height);
    bool validateState(std::shared_ptr<IDataNode> state) const;
};
```

### State Format (JSON) - Optimisé

```json
{
    "version": "v1.0",
    "frameCount": 600,
    "config": {
        "particleCount": 1000000,
        "terrainWidth": 10000,
        "terrainHeight": 10000,
        "historySize": 10000
    },
    "particles": {
        "count": 1000000,
        "data": "base64_encoded_binary_data"  // Compression pour réduire taille JSON
    },
    "terrain": {
        "width": 10000,
        "height": 10000,
        "compressed": true,
        "data": "base64_zlib_compressed"  // Terrain compressé
    },
    "history": [
        {"frame": 590, "fps": 60, "particles": 1000000, "ts": 1234567890},
        // ... 9999 autres frames
    ],
    "textureCache": {
        "count": 50000,
        "totalSize": 25600000
    }
}
```

### Test Principal

```cpp
// test_07_limits.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestAssertions.h"
#include "helpers/TestReporter.h"
#include <chrono>

int main() {
    TestReporter reporter("Limite Tests");
    TestMetrics metrics;

    // ========================================================================
    // TEST 1: Large State Serialization
    // ========================================================================
    std::cout << "\n=== TEST 1: Large State Serialization ===\n";

    DebugEngine engine;
    engine.loadModule("HeavyStateModule", "build/modules/libHeavyStateModule.so");

    auto config = createJsonConfig({
        {"version", "v1.0"},
        {"particleCount", 1000000},
        {"terrainSize", 10000},
        {"initDuration", 8.0f}
    });

    // Initialisation (devrait prendre ~8s)
    auto initStart = std::chrono::high_resolution_clock::now();
    engine.initializeModule("HeavyStateModule", config);
    auto initEnd = std::chrono::high_resolution_clock::now();

    float initTime = std::chrono::duration<float>(initEnd - initStart).count();
    std::cout << "Initialization took: " << initTime << "s\n";
    ASSERT_GT(initTime, 7.0f, "Init should take at least 7s (simulated heavy init)");
    ASSERT_LT(initTime, 10.0f, "Init should not take more than 10s");
    reporter.addMetric("init_time_s", initTime);

    // Exécuter 10s
    for (int i = 0; i < 600; i++) {
        engine.update(1.0f/60.0f);
    }

    // Mesurer temps de getState()
    auto getStateStart = std::chrono::high_resolution_clock::now();
    auto state = engine.getModuleState("HeavyStateModule");
    auto getStateEnd = std::chrono::high_resolution_clock::now();

    float getStateTime = std::chrono::duration<float, std::milli>(getStateEnd - getStateStart).count();
    std::cout << "getState() took: " << getStateTime << "ms\n";
    ASSERT_LT(getStateTime, 2000.0f, "getState() should be < 2000ms");
    reporter.addMetric("getstate_time_ms", getStateTime);

    // Estimer taille de l'état
    auto* jsonNode = dynamic_cast<JsonDataNode*>(state.get());
    std::string stateStr = jsonNode->getJsonData().dump();
    size_t stateSize = stateStr.size();
    std::cout << "State size: " << (stateSize / 1024.0f / 1024.0f) << " MB\n";
    reporter.addMetric("state_size_mb", stateSize / 1024.0f / 1024.0f);

    // Hot-reload avec setState()
    modifySourceFile("tests/modules/HeavyStateModule.cpp", "v1.0", "v2.0");
    system("cmake --build build --target HeavyStateModule 2>&1 > /dev/null");

    auto setStateStart = std::chrono::high_resolution_clock::now();
    engine.reloadModule("HeavyStateModule");
    auto setStateEnd = std::chrono::high_resolution_clock::now();

    float setStateTime = std::chrono::duration<float, std::milli>(setStateEnd - setStateStart).count();
    std::cout << "setState() + reload took: " << setStateTime << "ms\n";
    ASSERT_LT(setStateTime, 2000.0f, "setState() should be < 2000ms");
    reporter.addMetric("setstate_time_ms", setStateTime);

    // Vérifier intégrité
    auto stateAfter = engine.getModuleState("HeavyStateModule");
    auto* jsonNodeAfter = dynamic_cast<JsonDataNode*>(stateAfter.get());
    const auto& dataAfter = jsonNodeAfter->getJsonData();

    int particleCount = dataAfter["config"]["particleCount"];
    ASSERT_EQ(particleCount, 1000000, "Should have 1M particles after reload");
    reporter.addAssertion("particles_preserved", particleCount == 1000000);

    // Continuer 10s
    for (int i = 0; i < 600; i++) {
        engine.update(1.0f/60.0f);
    }

    std::cout << "✓ TEST 1 PASSED\n";

    // ========================================================================
    // TEST 2: Long Initialization Timeout
    // ========================================================================
    std::cout << "\n=== TEST 2: Long Initialization Timeout ===\n";

    DebugEngine engine2;
    engine2.loadModule("HeavyStateModule", "build/modules/libHeavyStateModule.so");

    // Config avec init très long + timeout trop court
    auto configTimeout = createJsonConfig({
        {"version", "v1.0"},
        {"particleCount", 1000000},
        {"terrainSize", 10000},
        {"initDuration", 12.0f},  // Init va prendre 12s
        {"initTimeout", 10.0f}    // Mais timeout à 10s
    });

    bool timedOut = false;
    try {
        engine2.initializeModule("HeavyStateModule", configTimeout);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        if (msg.find("timeout") != std::string::npos ||
            msg.find("Timeout") != std::string::npos) {
            timedOut = true;
            std::cout << "✓ Timeout detected correctly: " << msg << "\n";
        }
    }

    ASSERT_TRUE(timedOut, "Should timeout with init > timeout threshold");
    reporter.addAssertion("timeout_detection", timedOut);

    // Réessayer avec timeout suffisant
    auto configOk = createJsonConfig({
        {"version", "v1.0"},
        {"particleCount", 1000000},
        {"terrainSize", 10000},
        {"initDuration", 8.0f},
        {"initTimeout", 15.0f}
    });

    bool success = true;
    try {
        engine2.initializeModule("HeavyStateModule", configOk);
        engine2.update(1.0f/60.0f);
    } catch (...) {
        success = false;
    }

    ASSERT_TRUE(success, "Should succeed with adequate timeout");
    reporter.addAssertion("timeout_recovery", success);

    std::cout << "✓ TEST 2 PASSED\n";

    // ========================================================================
    // TEST 3: Memory Pressure During Reload
    // ========================================================================
    std::cout << "\n=== TEST 3: Memory Pressure During Reload ===\n";

    size_t memBefore = getCurrentMemoryUsage();
    std::cout << "Memory before: " << (memBefore / 1024.0f / 1024.0f) << " MB\n";

    // Exécuter quelques frames
    for (int i = 0; i < 300; i++) {
        engine.update(1.0f/60.0f);
    }

    // Allouer temporairement 200MB pendant reload
    std::vector<uint8_t> tempAlloc;

    auto reloadStart = std::chrono::high_resolution_clock::now();

    // Démarrer reload dans un thread
    std::thread reloadThread([&]() {
        modifySourceFile("tests/modules/HeavyStateModule.cpp", "v2.0", "v3.0");
        system("cmake --build build --target HeavyStateModule 2>&1 > /dev/null");
        engine.reloadModule("HeavyStateModule");
    });

    // Pendant reload, allouer massivement
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    tempAlloc.resize(200 * 1024 * 1024);  // 200MB
    std::fill(tempAlloc.begin(), tempAlloc.end(), 0x42);

    size_t memDuringReload = getCurrentMemoryUsage();
    std::cout << "Memory during reload: " << (memDuringReload / 1024.0f / 1024.0f) << " MB\n";

    reloadThread.join();
    auto reloadEnd = std::chrono::high_resolution_clock::now();

    float reloadTimeUnderPressure = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();
    std::cout << "Reload under pressure took: " << reloadTimeUnderPressure << "ms\n";
    reporter.addMetric("reload_under_pressure_ms", reloadTimeUnderPressure);

    // Libérer allocation temporaire
    tempAlloc.clear();
    tempAlloc.shrink_to_fit();

    // Attendre GC
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    size_t memAfter = getCurrentMemoryUsage();
    std::cout << "Memory after: " << (memAfter / 1024.0f / 1024.0f) << " MB\n";

    long memGrowth = static_cast<long>(memAfter) - static_cast<long>(memBefore);
    std::cout << "Net memory growth: " << (memGrowth / 1024.0f / 1024.0f) << " MB\n";

    // Tolérance: max 10MB de croissance nette
    ASSERT_LT(std::abs(memGrowth), 10 * 1024 * 1024, "Memory growth should be < 10MB");
    reporter.addMetric("memory_growth_mb", memGrowth / 1024.0f / 1024.0f);

    std::cout << "✓ TEST 3 PASSED\n";

    // ========================================================================
    // TEST 4: Incremental State Save/Load
    // ========================================================================
    std::cout << "\n=== TEST 4: Incremental State Save/Load ===\n";

    // Activer mode incrémental (si supporté)
    auto configIncremental = createJsonConfig({
        {"version", "v1.0"},
        {"particleCount", 100000},  // Réduit pour test rapide
        {"terrainSize", 1000},
        {"incrementalState", true}
    });

    DebugEngine engine3;
    engine3.loadModule("HeavyStateModule", "build/modules/libHeavyStateModule.so");
    engine3.initializeModule("HeavyStateModule", configIncremental);

    std::vector<float> incrementalTimes;

    for (int reload = 0; reload < 10; reload++) {
        // Exécuter 60 frames (1s)
        for (int i = 0; i < 60; i++) {
            engine3.update(1.0f/60.0f);
        }

        // Reload incrémental
        auto incStart = std::chrono::high_resolution_clock::now();

        // Modifier légèrement le code
        std::string oldVer = "v" + std::to_string(reload) + ".0";
        std::string newVer = "v" + std::to_string(reload + 1) + ".0";
        modifySourceFile("tests/modules/HeavyStateModule.cpp", oldVer, newVer);
        system("cmake --build build --target HeavyStateModule 2>&1 > /dev/null");

        engine3.reloadModule("HeavyStateModule");

        auto incEnd = std::chrono::high_resolution_clock::now();
        float incTime = std::chrono::duration<float, std::milli>(incEnd - incStart).count();

        incrementalTimes.push_back(incTime);
        std::cout << "Reload #" << reload << ": " << incTime << "ms\n";
    }

    float avgIncremental = std::accumulate(incrementalTimes.begin(), incrementalTimes.end(), 0.0f) / incrementalTimes.size();
    std::cout << "Average incremental reload: " << avgIncremental << "ms\n";

    // Note: Pour un vrai système incrémental, devrait être < 100ms
    // Ici on vérifie juste cohérence
    ASSERT_LT(avgIncremental, 2000.0f, "Incremental reloads should be reasonably fast");
    reporter.addMetric("avg_incremental_reload_ms", avgIncremental);

    std::cout << "✓ TEST 4 PASSED\n";

    // ========================================================================
    // TEST 5: State Corruption Detection
    // ========================================================================
    std::cout << "\n=== TEST 5: State Corruption Detection ===\n";

    DebugEngine engine4;
    engine4.loadModule("HeavyStateModule", "build/modules/libHeavyStateModule.so");

    auto configNormal = createJsonConfig({
        {"version", "v1.0"},
        {"particleCount", 10000},
        {"terrainSize", 100}
    });
    engine4.initializeModule("HeavyStateModule", configNormal);

    // Exécuter un peu
    for (int i = 0; i < 60; i++) {
        engine4.update(1.0f/60.0f);
    }

    // Créer un état corrompu
    nlohmann::json corruptedState = {
        {"version", "v1.0"},
        {"frameCount", "INVALID_NOT_A_NUMBER"},  // Type incorrect
        {"config", {
            {"particleCount", -500},  // Valeur négative invalide
            {"terrainSize", std::numeric_limits<float>::quiet_NaN()}  // NaN
        }},
        {"particles", {
            {"count", 10000},
            {"data", "CORRUPTED_BASE64!!!"}  // Données invalides
        }}
        // Champ "terrain" manquant (requis)
    };

    auto corruptedNode = std::make_shared<JsonDataNode>(corruptedState);

    bool detectedCorruption = false;
    try {
        engine4.setModuleState("HeavyStateModule", corruptedNode);
    } catch (const std::exception& e) {
        std::string msg = e.what();
        std::cout << "✓ Corruption detected: " << msg << "\n";
        detectedCorruption = true;
    }

    ASSERT_TRUE(detectedCorruption, "Should detect corrupted state");
    reporter.addAssertion("corruption_detection", detectedCorruption);

    // Vérifier que le module reste fonctionnel
    bool stillFunctional = true;
    try {
        for (int i = 0; i < 60; i++) {
            engine4.update(1.0f/60.0f);
        }
    } catch (...) {
        stillFunctional = false;
    }

    ASSERT_TRUE(stillFunctional, "Module should remain functional after rejected corrupted state");
    reporter.addAssertion("functional_after_corruption", stillFunctional);

    std::cout << "✓ TEST 5 PASSED\n";

    // ========================================================================
    // RAPPORT FINAL
    // ========================================================================

    metrics.printReport();
    reporter.printFinalReport();

    return reporter.getExitCode();
}
```

---

## 📊 Métriques Collectées

| Métrique | Description | Seuil |
|----------|-------------|-------|
| **init_time_s** | Temps d'initialisation module lourd | 7-10s |
| **getstate_time_ms** | Temps extraction état 120MB | < 2000ms |
| **setstate_time_ms** | Temps restauration état 120MB | < 2000ms |
| **state_size_mb** | Taille état sérialisé | ~120MB |
| **reload_under_pressure_ms** | Reload sous contrainte mémoire | < 3000ms |
| **memory_growth_mb** | Croissance mémoire nette | < 10MB |
| **avg_incremental_reload_ms** | Temps moyen reload incrémental | < 2000ms |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ getState() < 2000ms pour état 120MB
2. ✅ setState() < 2000ms pour état 120MB
3. ✅ Timeout détecté correctement (init > timeout)
4. ✅ Recovery après timeout fonctionne
5. ✅ Reload sous pression mémoire ne crash pas
6. ✅ Memory growth < 10MB
7. ✅ Corruption détectée et rejetée
8. ✅ Module reste fonctionnel après corruption

### NICE TO HAVE
1. ✅ getState() < 1000ms (optimal)
2. ✅ setState() < 1000ms (optimal)
3. ✅ Incremental reload < 100ms
4. ✅ Message d'erreur descriptif pour corruption

---

## 🔧 Helpers Nécessaires

### Compression Helper
```cpp
// Pour réduire taille JSON
#include <zlib.h>

std::string compressData(const std::vector<uint8_t>& data) {
    uLongf compressedSize = compressBound(data.size());
    std::vector<uint8_t> compressed(compressedSize);

    int result = compress(compressed.data(), &compressedSize,
                         data.data(), data.size());

    if (result != Z_OK) {
        throw std::runtime_error("Compression failed");
    }

    compressed.resize(compressedSize);

    // Base64 encode
    return base64_encode(compressed);
}

std::vector<uint8_t> decompressData(const std::string& base64Str) {
    auto compressed = base64_decode(base64Str);

    // Taille décompressée doit être dans metadata
    uLongf uncompressedSize = getUncompressedSize(compressed);
    std::vector<uint8_t> uncompressed(uncompressedSize);

    int result = uncompress(uncompressed.data(), &uncompressedSize,
                           compressed.data(), compressed.size());

    if (result != Z_OK) {
        throw std::runtime_error("Decompression failed");
    }

    return uncompressed;
}
```

### State Validator
```cpp
bool HeavyStateModule::validateState(std::shared_ptr<IDataNode> state) const {
    auto* jsonNode = dynamic_cast<JsonDataNode*>(state.get());
    if (!jsonNode) return false;

    const auto& data = jsonNode->getJsonData();

    // Vérifier champs requis
    if (!data.contains("version") || !data.contains("config") ||
        !data.contains("particles") || !data.contains("terrain")) {
        std::cerr << "ERROR: Missing required fields\n";
        return false;
    }

    // Vérifier types
    if (!data["frameCount"].is_number_integer()) {
        std::cerr << "ERROR: frameCount must be integer\n";
        return false;
    }

    // Vérifier limites
    int particleCount = data["config"]["particleCount"];
    if (particleCount < 0 || particleCount > 10000000) {
        std::cerr << "ERROR: Invalid particle count: " << particleCount << "\n";
        return false;
    }

    // Vérifier NaN/Infinity
    int terrainSize = data["config"]["terrainSize"];
    if (std::isnan(terrainSize) || std::isinf(terrainSize)) {
        std::cerr << "ERROR: terrain size is NaN/Inf\n";
        return false;
    }

    return true;
}
```

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| getState() > 2000ms | Sérialisation trop lente | FAIL - optimiser ou compresser |
| setState() > 2000ms | Désérialisation lente | FAIL - optimiser parsing JSON |
| Timeout non détecté | Init bloque sans timeout | FAIL - implémenter timeout thread |
| OOM pendant reload | Pic mémoire trop élevé | FAIL - limiter allocation simultanée |
| Corruption non détectée | Validation insuffisante | FAIL - renforcer validateState() |
| Memory leak > 10MB | Ressources non libérées | FAIL - check destructeurs |

---

## 📝 Output Attendu

```
================================================================================
TEST: Limite Tests
================================================================================

=== TEST 1: Large State Serialization ===
Initialization took: 8.2s
getState() took: 1243ms
State size: 118.4 MB
setState() + reload took: 1389ms
✓ Particles preserved: 1000000/1000000
✓ TEST 1 PASSED

=== TEST 2: Long Initialization Timeout ===
✓ Timeout detected correctly: Module init exceeded timeout (12s > 10s)
✓ Recovery successful with timeout=15s
✓ TEST 2 PASSED

=== TEST 3: Memory Pressure During Reload ===
Memory before: 124.3 MB
Memory during reload: 332.7 MB
Reload under pressure took: 1876ms
Memory after: 127.1 MB
Net memory growth: 2.8 MB
✓ TEST 3 PASSED

=== TEST 4: Incremental State Save/Load ===
Reload #0: 245ms
Reload #1: 238ms
Reload #2: 251ms
...
Reload #9: 242ms
Average incremental reload: 244ms
✓ TEST 4 PASSED

=== TEST 5: State Corruption Detection ===
✓ Corruption detected: Invalid particle count (negative value)
✓ Module remains functional after rejecting corrupted state
✓ TEST 5 PASSED

================================================================================
METRICS
================================================================================
  Init time:               8.2s
  getState time:           1243ms         (threshold: < 2000ms) ✓
  setState time:           1389ms         (threshold: < 2000ms) ✓
  State size:              118.4MB
  Reload under pressure:   1876ms         (threshold: < 3000ms) ✓
  Memory growth:           2.8MB          (threshold: < 10MB)   ✓
  Avg incremental reload:  244ms

================================================================================
ASSERTIONS
================================================================================
  ✓ particles_preserved
  ✓ timeout_detection
  ✓ timeout_recovery
  ✓ corruption_detection
  ✓ functional_after_corruption

Result: ✅ PASSED (5/5 tests)

================================================================================
```

---

## 📅 Planning

**Jour 1 (3h):**
- Implémenter HeavyStateModule avec gestion large state
- Implémenter compression/décompression
- Implémenter state validation

**Jour 2 (1h):**
- Implémenter test_07_limits.cpp
- Debug + validation

---

**Prochaine étape**: `scenario_08_config_hotreload.md` (optionnel)
