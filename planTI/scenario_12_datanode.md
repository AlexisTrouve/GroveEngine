# Scénario 12: DataNode Integration Test

**Priorité**: ⭐⭐ SHOULD HAVE
**Phase**: 2 (SHOULD HAVE)
**Durée estimée**: ~4 minutes
**Effort implémentation**: ~5-7 heures

---

## 🎯 Objectif

Valider que le système DataNode (IDataTree/JsonDataTree) fonctionne correctement pour tous les cas d'usage:
- Tree navigation (exact match, pattern matching, queries)
- Hot-reload system (file watch, callbacks, isolation)
- Persistence (save/load, data integrity)
- Hash system (data hash, tree hash, change detection)
- Read-only enforcement (config/ vs data/ vs runtime/)
- Type safety et defaults
- Performance avec large trees (1000+ nodes)

**Note**: Le DataNode est le système central de configuration et persistence du moteur.

---

## 📋 Description

### Setup Initial
1. Créer un IDataTree avec structure complète:
   - **config/** - Configuration read-only avec 500 nodes
   - **data/** - Persistence read-write avec 300 nodes
   - **runtime/** - State temporaire avec 200 nodes
2. Total: ~1000 nodes dans l'arbre
3. Fichiers JSON sur disque pour config/ et data/

### Test Séquence

#### Test 1: Tree Navigation & Exact Matching (30s)
1. Créer hiérarchie: `config/units/tanks/heavy_mk1`
2. Tester navigation:
   - `getChild("units")` → `getChild("tanks")` → `getChild("heavy_mk1")`
   - `getChildrenByName("heavy_mk1")` - direct children only
   - `getPath()` - verify full path
3. Vérifier:
   - Nodes trouvés correctement
   - Path correct: "config/units/tanks/heavy_mk1"
   - getChild retourne nullptr si non trouvé

#### Test 2: Pattern Matching (Wildcards) (30s)
1. Créer nodes:
   - `config/units/tanks/heavy_mk1`
   - `config/units/tanks/heavy_mk2`
   - `config/units/infantry/heavy_trooper`
   - `config/units/aircraft/light_fighter`
2. Tester patterns:
   - `getChildrenByNameMatch("*heavy*")` → 3 matches
   - `getChildrenByNameMatch("tanks/*")` → 2 matches
   - `getChildrenByNameMatch("*_mk*")` → 2 matches
3. Vérifier tous les matches corrects

#### Test 3: Property-Based Queries (30s)
1. Créer nodes avec propriétés:
   - `heavy_mk1`: armor=150, speed=30, cost=1000
   - `heavy_mk2`: armor=180, speed=25, cost=1200
   - `light_fighter`: armor=50, speed=120, cost=800
2. Query predicates:
   - `queryByProperty("armor", val > 100)` → 2 units
   - `queryByProperty("speed", val > 50)` → 1 unit
   - `queryByProperty("cost", val <= 1000)` → 2 units
3. Vérifier résultats des queries

#### Test 4: Hot-Reload System (60s)
1. Créer `config/gameplay.json` sur disque
2. Charger dans tree avec `onTreeReloaded` callback
3. Modifier fichier sur disque (changer valeur)
4. Appeler `checkForChanges()` → devrait détecter changement
5. Appeler `reloadIfChanged()` → callback déclenché
6. Vérifier:
   - Callback appelé exactement 1 fois
   - Nouvelles valeurs chargées
   - Anciens nodes remplacés

#### Test 5: Hot-Reload Isolation (30s)
1. Charger 2 fichiers: `config/units.json` et `config/maps.json`
2. Modifier seulement `units.json`
3. Vérifier:
   - `checkForChanges()` détecte seulement units.json
   - Reload ne touche pas maps.json
   - Callback reçoit info sur quel fichier changé

#### Test 6: Persistence (Save/Load) (60s)
1. Créer structure data/:
   - `data/player/stats` - {kills: 42, deaths: 3}
   - `data/player/inventory` - {gold: 1000, items: [...]}
   - `data/world/time` - {day: 5, hour: 14}
2. Appeler `saveData()` → écrit sur disque
3. Vérifier fichiers créés:
   - `data/player/stats.json`
   - `data/player/inventory.json`
   - `data/world/time.json`
4. Charger dans nouveau tree
5. Vérifier data identique (deep comparison)

#### Test 7: Selective Save (30s)
1. Modifier seulement `data/player/stats`
2. Appeler `saveNode("data/player/stats")`
3. Vérifier:
   - Seulement stats.json écrit
   - Autres fichiers non modifiés (mtime identique)

#### Test 8: Hash System (Data Hash) (30s)
1. Créer node avec data: `{value: 42}`
2. Calculer `getDataHash()`
3. Modifier data: `{value: 43}`
4. Recalculer hash
5. Vérifier hashes différents

#### Test 9: Hash System (Tree Hash) (30s)
1. Créer arbre:
   ```
   root
     ├─ child1 {data: 1}
     └─ child2 {data: 2}
   ```
2. Calculer `getTreeHash()`
3. Modifier child1 data
4. Recalculer tree hash
5. Vérifier hashes différents (propagation)

#### Test 10: Read-Only Enforcement (30s)
1. Tenter `setChild()` sur node config/
2. Devrait throw exception
3. Vérifier:
   - Exception levée
   - Message descriptif
   - Config/ non modifié

#### Test 11: Type Safety & Defaults (20s)
1. Créer node: `{armor: 150, name: "Tank"}`
2. Tester accès:
   - `getInt("armor")` → 150
   - `getInt("missing", 100)` → 100 (default)
   - `getString("name")` → "Tank"
   - `getBool("active", true)` → true (default)
   - `getDouble("speed")` → throw ou default

#### Test 12: Deep Tree Performance (30s)
1. Créer tree avec 1000 nodes:
   - 10 catégories
   - 10 subcatégories each
   - 10 items each
2. Mesurer temps:
   - Pattern matching "*" (tous nodes): < 100ms
   - Query by property: < 50ms
   - Tree hash calculation: < 200ms
3. Vérifier performance acceptable

---

## 🏗️ Implémentation

### Test Module Structure

```cpp
// DataNodeTestModule.h
class DataNodeTestModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

    // Test helpers
    void createTestTree();
    void testNavigation();
    void testPatternMatching();
    void testQueries();
    void testHotReload();
    void testPersistence();
    void testHashes();
    void testReadOnly();
    void testTypeAccess();
    void testPerformance();

private:
    std::shared_ptr<IDataTree> tree;
    int reloadCallbackCount = 0;
};
```

### Test Principal

```cpp
// test_12_datanode.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestAssertions.h"
#include "helpers/TestReporter.h"
#include <fstream>

int main() {
    TestReporter reporter("DataNode Integration Test");
    TestMetrics metrics;

    // === SETUP ===
    std::filesystem::create_directories("test_data/config");
    std::filesystem::create_directories("test_data/data");

    // Créer IDataTree
    auto tree = std::make_shared<JsonDataTree>("test_data");

    // ========================================================================
    // TEST 1: Tree Navigation & Exact Matching
    // ========================================================================
    std::cout << "\n=== TEST 1: Tree Navigation & Exact Matching ===\n";

    // Créer hiérarchie
    auto configRoot = tree->getConfigRoot();
    auto units = std::make_shared<JsonDataNode>("units", nlohmann::json::object());
    auto tanks = std::make_shared<JsonDataNode>("tanks", nlohmann::json::object());
    auto heavyMk1 = std::make_shared<JsonDataNode>("heavy_mk1", nlohmann::json{
        {"armor", 150},
        {"speed", 30},
        {"cost", 1000}
    });

    tanks->setChild("heavy_mk1", heavyMk1);
    units->setChild("tanks", tanks);
    configRoot->setChild("units", units);

    // Navigation
    auto foundUnits = configRoot->getChild("units");
    ASSERT_TRUE(foundUnits != nullptr, "Should find units node");

    auto foundTanks = foundUnits->getChild("tanks");
    ASSERT_TRUE(foundTanks != nullptr, "Should find tanks node");

    auto foundHeavy = foundTanks->getChild("heavy_mk1");
    ASSERT_TRUE(foundHeavy != nullptr, "Should find heavy_mk1 node");

    // Path
    std::string path = foundHeavy->getPath();
    std::cout << "Path: " << path << "\n";
    ASSERT_TRUE(path.find("heavy_mk1") != std::string::npos, "Path should contain node name");

    // Not found
    auto notFound = foundTanks->getChild("does_not_exist");
    ASSERT_TRUE(notFound == nullptr, "Should return nullptr for missing child");

    reporter.addAssertion("navigation_exact", true);
    std::cout << "✓ TEST 1 PASSED\n";

    // ========================================================================
    // TEST 2: Pattern Matching (Wildcards)
    // ========================================================================
    std::cout << "\n=== TEST 2: Pattern Matching ===\n";

    // Ajouter plus de nodes
    auto heavyMk2 = std::make_shared<JsonDataNode>("heavy_mk2", nlohmann::json{
        {"armor", 180},
        {"speed", 25},
        {"cost", 1200}
    });
    tanks->setChild("heavy_mk2", heavyMk2);

    auto infantry = std::make_shared<JsonDataNode>("infantry", nlohmann::json::object());
    auto heavyTrooper = std::make_shared<JsonDataNode>("heavy_trooper", nlohmann::json{
        {"armor", 120},
        {"speed", 15},
        {"cost", 500}
    });
    infantry->setChild("heavy_trooper", heavyTrooper);
    units->setChild("infantry", infantry);

    auto aircraft = std::make_shared<JsonDataNode>("aircraft", nlohmann::json::object());
    auto lightFighter = std::make_shared<JsonDataNode>("light_fighter", nlohmann::json{
        {"armor", 50},
        {"speed", 120},
        {"cost", 800}
    });
    aircraft->setChild("light_fighter", lightFighter);
    units->setChild("aircraft", aircraft);

    // Pattern: *heavy*
    auto heavyUnits = configRoot->getChildrenByNameMatch("*heavy*");
    std::cout << "Pattern '*heavy*' matched: " << heavyUnits.size() << " nodes\n";
    for (const auto& node : heavyUnits) {
        std::cout << "  - " << node->getName() << "\n";
    }
    // Should match: heavy_mk1, heavy_mk2, heavy_trooper
    ASSERT_EQ(heavyUnits.size(), 3, "Should match 3 'heavy' units");
    reporter.addMetric("pattern_heavy_count", heavyUnits.size());

    // Pattern: *_mk*
    auto mkUnits = configRoot->getChildrenByNameMatch("*_mk*");
    std::cout << "Pattern '*_mk*' matched: " << mkUnits.size() << " nodes\n";
    // Should match: heavy_mk1, heavy_mk2
    ASSERT_EQ(mkUnits.size(), 2, "Should match 2 '_mk' units");

    reporter.addAssertion("pattern_matching", true);
    std::cout << "✓ TEST 2 PASSED\n";

    // ========================================================================
    // TEST 3: Property-Based Queries
    // ========================================================================
    std::cout << "\n=== TEST 3: Property-Based Queries ===\n";

    // Query: armor > 100
    auto armoredUnits = configRoot->queryByProperty("armor",
        [](const IDataValue& val) {
            return val.isNumber() && val.asInt() >= 100;
        });

    std::cout << "Units with armor >= 100: " << armoredUnits.size() << "\n";
    for (const auto& node : armoredUnits) {
        int armor = node->getInt("armor");
        std::cout << "  - " << node->getName() << " (armor=" << armor << ")\n";
        ASSERT_GE(armor, 100, "Armor should be >= 100");
    }
    // Should match: heavy_mk1 (150), heavy_mk2 (180), heavy_trooper (120)
    ASSERT_EQ(armoredUnits.size(), 3, "Should find 3 armored units");

    // Query: speed > 50
    auto fastUnits = configRoot->queryByProperty("speed",
        [](const IDataValue& val) {
            return val.isNumber() && val.asInt() > 50;
        });

    std::cout << "Units with speed > 50: " << fastUnits.size() << "\n";
    // Should match: light_fighter (120)
    ASSERT_EQ(fastUnits.size(), 1, "Should find 1 fast unit");

    reporter.addAssertion("property_queries", true);
    std::cout << "✓ TEST 3 PASSED\n";

    // ========================================================================
    // TEST 4: Hot-Reload System
    // ========================================================================
    std::cout << "\n=== TEST 4: Hot-Reload System ===\n";

    // Créer fichier config
    nlohmann::json gameplayConfig = {
        {"difficulty", "normal"},
        {"maxPlayers", 4},
        {"timeLimit", 3600}
    };

    std::ofstream configFile("test_data/config/gameplay.json");
    configFile << gameplayConfig.dump(2);
    configFile.close();

    // Charger dans tree
    tree->loadConfigFile("gameplay.json");

    // Setup callback
    int callbackCount = 0;
    tree->onTreeReloaded([&callbackCount]() {
        callbackCount++;
        std::cout << "  → Reload callback triggered (count=" << callbackCount << ")\n";
    });

    // Vérifier contenu initial
    auto gameplay = configRoot->getChild("gameplay");
    ASSERT_TRUE(gameplay != nullptr, "gameplay node should exist");
    std::string difficulty = gameplay->getString("difficulty");
    ASSERT_EQ(difficulty, "normal", "Initial difficulty should be 'normal'");

    std::cout << "Initial difficulty: " << difficulty << "\n";

    // Modifier fichier
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    gameplayConfig["difficulty"] = "hard";
    gameplayConfig["maxPlayers"] = 8;

    std::ofstream configFile2("test_data/config/gameplay.json");
    configFile2 << gameplayConfig.dump(2);
    configFile2.close();

    // Force file timestamp update
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check for changes
    bool hasChanges = tree->checkForChanges();
    std::cout << "Has changes: " << (hasChanges ? "YES" : "NO") << "\n";
    ASSERT_TRUE(hasChanges, "Should detect file modification");

    // Reload
    bool reloaded = tree->reloadIfChanged();
    std::cout << "Reloaded: " << (reloaded ? "YES" : "NO") << "\n";
    ASSERT_TRUE(reloaded, "Should reload changed file");

    // Vérifier callback
    ASSERT_EQ(callbackCount, 1, "Callback should be called exactly once");

    // Vérifier nouvelles valeurs
    gameplay = configRoot->getChild("gameplay");
    difficulty = gameplay->getString("difficulty");
    int maxPlayers = gameplay->getInt("maxPlayers");

    std::cout << "After reload - difficulty: " << difficulty << ", maxPlayers: " << maxPlayers << "\n";
    ASSERT_EQ(difficulty, "hard", "Difficulty should be updated to 'hard'");
    ASSERT_EQ(maxPlayers, 8, "maxPlayers should be updated to 8");

    reporter.addAssertion("hot_reload", true);
    reporter.addMetric("reload_callback_count", callbackCount);
    std::cout << "✓ TEST 4 PASSED\n";

    // ========================================================================
    // TEST 5: Hot-Reload Isolation
    // ========================================================================
    std::cout << "\n=== TEST 5: Hot-Reload Isolation ===\n";

    // Créer second fichier
    nlohmann::json mapsConfig = {
        {"defaultMap", "desert"},
        {"mapCount", 10}
    };

    std::ofstream mapsFile("test_data/config/maps.json");
    mapsFile << mapsConfig.dump(2);
    mapsFile.close();

    tree->loadConfigFile("maps.json");

    // Modifier seulement gameplay.json
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    gameplayConfig["difficulty"] = "extreme";

    std::ofstream configFile3("test_data/config/gameplay.json");
    configFile3 << gameplayConfig.dump(2);
    configFile3.close();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check changes
    hasChanges = tree->checkForChanges();
    ASSERT_TRUE(hasChanges, "Should detect gameplay.json change");

    // Verify maps.json not affected
    auto maps = configRoot->getChild("maps");
    std::string defaultMap = maps->getString("defaultMap");
    ASSERT_EQ(defaultMap, "desert", "maps.json should not be affected");

    reloaded = tree->reloadIfChanged();
    ASSERT_TRUE(reloaded, "Should reload only changed file");

    // Verify maps still intact
    maps = configRoot->getChild("maps");
    defaultMap = maps->getString("defaultMap");
    ASSERT_EQ(defaultMap, "desert", "maps.json should still be 'desert' after isolated reload");

    reporter.addAssertion("reload_isolation", true);
    std::cout << "✓ TEST 5 PASSED\n";

    // ========================================================================
    // TEST 6: Persistence (Save/Load)
    // ========================================================================
    std::cout << "\n=== TEST 6: Persistence (Save/Load) ===\n";

    auto dataRoot = tree->getDataRoot();

    // Créer structure data/
    auto player = std::make_shared<JsonDataNode>("player", nlohmann::json::object());
    auto stats = std::make_shared<JsonDataNode>("stats", nlohmann::json{
        {"kills", 42},
        {"deaths", 3},
        {"level", 15}
    });
    auto inventory = std::make_shared<JsonDataNode>("inventory", nlohmann::json{
        {"gold", 1000},
        {"items", nlohmann::json::array({"sword", "shield", "potion"})}
    });

    player->setChild("stats", stats);
    player->setChild("inventory", inventory);
    dataRoot->setChild("player", player);

    auto world = std::make_shared<JsonDataNode>("world", nlohmann::json::object());
    auto time = std::make_shared<JsonDataNode>("time", nlohmann::json{
        {"day", 5},
        {"hour", 14},
        {"minute", 30}
    });
    world->setChild("time", time);
    dataRoot->setChild("world", world);

    // Save all data
    tree->saveData();

    // Vérifier fichiers créés
    ASSERT_TRUE(std::filesystem::exists("test_data/data/player/stats.json"),
                "stats.json should exist");
    ASSERT_TRUE(std::filesystem::exists("test_data/data/player/inventory.json"),
                "inventory.json should exist");
    ASSERT_TRUE(std::filesystem::exists("test_data/data/world/time.json"),
                "time.json should exist");

    std::cout << "Files saved successfully\n";

    // Créer nouveau tree et charger
    auto tree2 = std::make_shared<JsonDataTree>("test_data");
    tree2->loadDataDirectory();

    auto dataRoot2 = tree2->getDataRoot();
    auto player2 = dataRoot2->getChild("player");
    ASSERT_TRUE(player2 != nullptr, "player node should load");

    auto stats2 = player2->getChild("stats");
    int kills = stats2->getInt("kills");
    int deaths = stats2->getInt("deaths");

    std::cout << "Loaded: kills=" << kills << ", deaths=" << deaths << "\n";
    ASSERT_EQ(kills, 42, "kills should be preserved");
    ASSERT_EQ(deaths, 3, "deaths should be preserved");

    reporter.addAssertion("persistence", true);
    std::cout << "✓ TEST 6 PASSED\n";

    // ========================================================================
    // TEST 7: Selective Save
    // ========================================================================
    std::cout << "\n=== TEST 7: Selective Save ===\n";

    // Get mtime of inventory.json before
    auto inventoryPath = std::filesystem::path("test_data/data/player/inventory.json");
    auto mtimeBefore = std::filesystem::last_write_time(inventoryPath);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Modify only stats
    stats->setInt("kills", 100);

    // Save only stats
    tree->saveNode("data/player/stats");

    // Check inventory.json not modified
    auto mtimeAfter = std::filesystem::last_write_time(inventoryPath);

    ASSERT_EQ(mtimeBefore, mtimeAfter, "inventory.json should not be modified");

    // Load stats and verify
    auto tree3 = std::make_shared<JsonDataTree>("test_data");
    tree3->loadDataDirectory();
    auto stats3 = tree3->getDataRoot()->getChild("player")->getChild("stats");
    int newKills = stats3->getInt("kills");

    ASSERT_EQ(newKills, 100, "Selective save should update only stats");

    reporter.addAssertion("selective_save", true);
    std::cout << "✓ TEST 7 PASSED\n";

    // ========================================================================
    // TEST 8: Hash System (Data Hash)
    // ========================================================================
    std::cout << "\n=== TEST 8: Hash System (Data Hash) ===\n";

    auto testNode = std::make_shared<JsonDataNode>("test", nlohmann::json{
        {"value", 42}
    });

    std::string hash1 = testNode->getDataHash();
    std::cout << "Hash 1: " << hash1.substr(0, 16) << "...\n";

    // Modify data
    testNode->setInt("value", 43);

    std::string hash2 = testNode->getDataHash();
    std::cout << "Hash 2: " << hash2.substr(0, 16) << "...\n";

    ASSERT_TRUE(hash1 != hash2, "Hashes should differ after data change");

    reporter.addAssertion("data_hash", true);
    std::cout << "✓ TEST 8 PASSED\n";

    // ========================================================================
    // TEST 9: Hash System (Tree Hash)
    // ========================================================================
    std::cout << "\n=== TEST 9: Hash System (Tree Hash) ===\n";

    auto root = std::make_shared<JsonDataNode>("root", nlohmann::json::object());
    auto child1 = std::make_shared<JsonDataNode>("child1", nlohmann::json{{"data", 1}});
    auto child2 = std::make_shared<JsonDataNode>("child2", nlohmann::json{{"data", 2}});

    root->setChild("child1", child1);
    root->setChild("child2", child2);

    std::string treeHash1 = root->getTreeHash();
    std::cout << "Tree Hash 1: " << treeHash1.substr(0, 16) << "...\n";

    // Modify child1
    child1->setInt("data", 999);

    std::string treeHash2 = root->getTreeHash();
    std::cout << "Tree Hash 2: " << treeHash2.substr(0, 16) << "...\n";

    ASSERT_TRUE(treeHash1 != treeHash2, "Tree hash should change when child changes");

    reporter.addAssertion("tree_hash", true);
    std::cout << "✓ TEST 9 PASSED\n";

    // ========================================================================
    // TEST 10: Read-Only Enforcement
    // ========================================================================
    std::cout << "\n=== TEST 10: Read-Only Enforcement ===\n";

    auto readOnlyNode = configRoot->getChild("gameplay");

    bool exceptionThrown = false;
    try {
        auto newChild = std::make_shared<JsonDataNode>("illegal", nlohmann::json{{"bad", true}});
        readOnlyNode->setChild("illegal", newChild);
    } catch (const std::runtime_error& e) {
        std::cout << "✓ Exception thrown: " << e.what() << "\n";
        exceptionThrown = true;
    }

    ASSERT_TRUE(exceptionThrown, "Should throw exception when modifying read-only node");

    reporter.addAssertion("readonly_enforcement", true);
    std::cout << "✓ TEST 10 PASSED\n";

    // ========================================================================
    // TEST 11: Type Safety & Defaults
    // ========================================================================
    std::cout << "\n=== TEST 11: Type Safety & Defaults ===\n";

    auto typeNode = std::make_shared<JsonDataNode>("types", nlohmann::json{
        {"armor", 150},
        {"name", "Tank"},
        {"active", true},
        {"speed", 30.5}
    });

    int armor = typeNode->getInt("armor");
    ASSERT_EQ(armor, 150, "getInt should return correct value");

    int missing = typeNode->getInt("missing", 100);
    ASSERT_EQ(missing, 100, "getInt with default should return default");

    std::string name = typeNode->getString("name");
    ASSERT_EQ(name, "Tank", "getString should return correct value");

    bool active = typeNode->getBool("active");
    ASSERT_EQ(active, true, "getBool should return correct value");

    bool defaultBool = typeNode->getBool("nothere", false);
    ASSERT_EQ(defaultBool, false, "getBool with default should return default");

    double speed = typeNode->getDouble("speed");
    ASSERT_EQ(speed, 30.5, "getDouble should return correct value");

    reporter.addAssertion("type_safety", true);
    std::cout << "✓ TEST 11 PASSED\n";

    // ========================================================================
    // TEST 12: Deep Tree Performance
    // ========================================================================
    std::cout << "\n=== TEST 12: Deep Tree Performance ===\n";

    auto perfRoot = std::make_shared<JsonDataNode>("perf", nlohmann::json::object());

    // Create 1000 nodes: 10 x 10 x 10
    int nodeCount = 0;
    for (int cat = 0; cat < 10; cat++) {
        auto category = std::make_shared<JsonDataNode>("cat_" + std::to_string(cat),
                                                        nlohmann::json::object());

        for (int sub = 0; sub < 10; sub++) {
            auto subcategory = std::make_shared<JsonDataNode>("sub_" + std::to_string(sub),
                                                               nlohmann::json::object());

            for (int item = 0; item < 10; item++) {
                auto itemNode = std::make_shared<JsonDataNode>("item_" + std::to_string(item),
                                                                nlohmann::json{
                                                                    {"id", nodeCount},
                                                                    {"value", nodeCount * 10}
                                                                });
                subcategory->setChild("item_" + std::to_string(item), itemNode);
                nodeCount++;
            }

            category->setChild("sub_" + std::to_string(sub), subcategory);
        }

        perfRoot->setChild("cat_" + std::to_string(cat), category);
    }

    std::cout << "Created " << nodeCount << " nodes\n";
    ASSERT_EQ(nodeCount, 1000, "Should create 1000 nodes");

    // Pattern matching: find all items
    auto start = std::chrono::high_resolution_clock::now();
    auto allItems = perfRoot->getChildrenByNameMatch("item_*");
    auto end = std::chrono::high_resolution_clock::now();

    float patternTime = std::chrono::duration<float, std::milli>(end - start).count();
    std::cout << "Pattern matching found " << allItems.size() << " items in " << patternTime << "ms\n";

    ASSERT_EQ(allItems.size(), 1000, "Should find all 1000 items");
    ASSERT_LT(patternTime, 100.0f, "Pattern matching should be < 100ms");
    reporter.addMetric("pattern_time_ms", patternTime);

    // Query by property
    start = std::chrono::high_resolution_clock::now();
    auto queryResults = perfRoot->queryByProperty("value",
        [](const IDataValue& val) {
            return val.isNumber() && val.asInt() > 5000;
        });
    end = std::chrono::high_resolution_clock::now();

    float queryTime = std::chrono::duration<float, std::milli>(end - start).count();
    std::cout << "Query found " << queryResults.size() << " results in " << queryTime << "ms\n";

    ASSERT_LT(queryTime, 50.0f, "Query should be < 50ms");
    reporter.addMetric("query_time_ms", queryTime);

    // Tree hash
    start = std::chrono::high_resolution_clock::now();
    std::string treeHash = perfRoot->getTreeHash();
    end = std::chrono::high_resolution_clock::now();

    float hashTime = std::chrono::duration<float, std::milli>(end - start).count();
    std::cout << "Tree hash calculated in " << hashTime << "ms\n";

    ASSERT_LT(hashTime, 200.0f, "Tree hash should be < 200ms");
    reporter.addMetric("treehash_time_ms", hashTime);

    reporter.addAssertion("performance", true);
    std::cout << "✓ TEST 12 PASSED\n";

    // ========================================================================
    // CLEANUP
    // ========================================================================
    std::filesystem::remove_all("test_data");

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
| **pattern_heavy_count** | Nodes matchés par pattern "*heavy*" | 3 |
| **reload_callback_count** | Callbacks déclenchés lors reload | 1 |
| **pattern_time_ms** | Temps pattern matching 1000 nodes | < 100ms |
| **query_time_ms** | Temps property query 1000 nodes | < 50ms |
| **treehash_time_ms** | Temps calcul tree hash 1000 nodes | < 200ms |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ Navigation exacte fonctionne (getChild, getPath)
2. ✅ Pattern matching trouve tous les matches
3. ✅ Property queries retournent résultats corrects
4. ✅ Hot-reload détecte changements fichier
5. ✅ Hot-reload callback déclenché
6. ✅ Hot-reload isolation (un fichier modifié n'affecte pas autres)
7. ✅ Persistence save/load préserve data
8. ✅ Selective save modifie seulement node ciblé
9. ✅ Data hash change quand data modifié
10. ✅ Tree hash change quand children modifiés
11. ✅ Read-only nodes throw exception si modifiés
12. ✅ Type access avec defaults fonctionne
13. ✅ Performance acceptable sur 1000 nodes

### NICE TO HAVE
1. ✅ Pattern matching < 50ms (optimal)
2. ✅ Query < 25ms (optimal)
3. ✅ Tree hash < 100ms (optimal)

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| Pattern pas match | Regex incorrecte | FAIL - fix wildcard conversion |
| Query vide | Predicate trop strict | WARN - vérifier logique |
| Hot-reload pas détecté | File watch bug | FAIL - fix checkForChanges() |
| Callback pas appelé | onTreeReloaded bug | FAIL - fix callback system |
| Persistence data corrompu | JSON malformé | FAIL - add validation |
| Hash identiques | Hash calculation bug | FAIL - fix getDataHash() |
| Read-only pas enforced | isReadOnly check manquant | FAIL - add check |
| Type mismatch crash | Pas de default handling | FAIL - add try/catch |
| Performance > seuils | Algorithme O(n²) | FAIL - optimize |

---

## 📝 Output Attendu

```
================================================================================
TEST: DataNode Integration Test
================================================================================

=== TEST 1: Tree Navigation & Exact Matching ===
Path: config/units/tanks/heavy_mk1
✓ TEST 1 PASSED

=== TEST 2: Pattern Matching ===
Pattern '*heavy*' matched: 3 nodes
  - heavy_mk1
  - heavy_mk2
  - heavy_trooper
Pattern '*_mk*' matched: 2 nodes
✓ TEST 2 PASSED

=== TEST 3: Property-Based Queries ===
Units with armor >= 100: 3
  - heavy_mk1 (armor=150)
  - heavy_mk2 (armor=180)
  - heavy_trooper (armor=120)
Units with speed > 50: 1
✓ TEST 3 PASSED

=== TEST 4: Hot-Reload System ===
Initial difficulty: normal
Has changes: YES
Reloaded: YES
  → Reload callback triggered (count=1)
After reload - difficulty: hard, maxPlayers: 8
✓ TEST 4 PASSED

=== TEST 5: Hot-Reload Isolation ===
✓ TEST 5 PASSED

=== TEST 6: Persistence (Save/Load) ===
Files saved successfully
Loaded: kills=42, deaths=3
✓ TEST 6 PASSED

=== TEST 7: Selective Save ===
✓ TEST 7 PASSED

=== TEST 8: Hash System (Data Hash) ===
Hash 1: 5d41402abc4b2a76...
Hash 2: 7c6a180b36896a0e...
✓ TEST 8 PASSED

=== TEST 9: Hash System (Tree Hash) ===
Tree Hash 1: a1b2c3d4e5f6g7h8...
Tree Hash 2: 9i8j7k6l5m4n3o2p...
✓ TEST 9 PASSED

=== TEST 10: Read-Only Enforcement ===
✓ Exception thrown: Cannot modify read-only node 'gameplay'
✓ TEST 10 PASSED

=== TEST 11: Type Safety & Defaults ===
✓ TEST 11 PASSED

=== TEST 12: Deep Tree Performance ===
Created 1000 nodes
Pattern matching found 1000 items in 45.3ms
Query found 500 results in 23.7ms
Tree hash calculated in 134.2ms
✓ TEST 12 PASSED

================================================================================
METRICS
================================================================================
  Pattern heavy count:     3
  Reload callback count:   1
  Pattern time:            45.3ms         (threshold: < 100ms) ✓
  Query time:              23.7ms         (threshold: < 50ms)  ✓
  Tree hash time:          134.2ms        (threshold: < 200ms) ✓

================================================================================
ASSERTIONS
================================================================================
  ✓ navigation_exact
  ✓ pattern_matching
  ✓ property_queries
  ✓ hot_reload
  ✓ reload_isolation
  ✓ persistence
  ✓ selective_save
  ✓ data_hash
  ✓ tree_hash
  ✓ readonly_enforcement
  ✓ type_safety
  ✓ performance

Result: ✅ PASSED (12/12 tests)

================================================================================
```

---

## 📅 Planning

**Jour 1 (4h):**
- Setup JsonDataTree avec test directory
- Implémenter tests 1-6 (navigation, patterns, queries, hot-reload, persistence)

**Jour 2 (3h):**
- Implémenter tests 7-12 (selective save, hashes, readonly, types, performance)
- Debug + validation

---

**Prochaine étape**: `scenario_13_cross_system.md`
