# Scénario 13: Cross-System Integration (IO + DataNode)

**Priorité**: ⭐⭐ SHOULD HAVE
**Phase**: 2 (SHOULD HAVE)
**Durée estimée**: ~6 minutes
**Effort implémentation**: ~6-8 heures

---

## 🎯 Objectif

Valider que les systèmes IO (IntraIO) et DataNode (IDataTree) fonctionnent correctement **ensemble** dans des cas d'usage réels:
- Config hot-reload → republish via IO
- State persistence via DataNode + message routing via IO
- Multi-module coordination (Module A publie state → Module B lit via DataNode)
- Concurrent access (IO threads + DataNode threads)
- Integration avec hot-reload de modules
- Performance du système complet

**Note**: Ce test valide l'intégration complète du moteur, pas les composants isolés.

---

## 📋 Description

### Setup Initial
1. Créer IDataTree avec structure complète:
   - **config/** - Configuration modules (units, gameplay, network)
   - **data/** - State persistence (player, world, economy)
   - **runtime/** - State temporaire (fps, metrics, active_entities)

2. Créer 4 modules avec IO + DataNode:
   - **ConfigWatcherModule** - Surveille config/, publie changements via IO
   - **PlayerModule** - Gère state joueur, persiste via data/, publie events
   - **EconomyModule** - Souscrit à player events, met à jour economy data/
   - **MetricsModule** - Collecte metrics dans runtime/, publie stats

3. Total: 4 modules communicant via IO et partageant data via DataNode

### Test Séquence

#### Test 1: Config Hot-Reload → IO Broadcast (60s)
1. ConfigWatcherModule souscrit à hot-reload callbacks
2. Modifier `config/gameplay.json` (changer difficulty)
3. Quand callback déclenché:
   - ConfigWatcherModule publie "config:gameplay:changed" via IO
   - PlayerModule souscrit et reçoit notification
   - PlayerModule lit nouvelle config via DataNode
   - PlayerModule ajuste son comportement
4. Vérifier:
   - Callback → publish → subscribe → read chain fonctionne
   - Nouvelle config appliquée dans PlayerModule
   - Latence totale < 100ms

#### Test 2: State Persistence + Event Publishing (60s)
1. PlayerModule crée state:
   - `data/player/profile` - {name, level, gold}
   - `data/player/inventory` - {items[]}
2. PlayerModule sauvegarde via `tree->saveNode()`
3. PlayerModule publie "player:level_up" via IO
4. EconomyModule souscrit à "player:*"
5. EconomyModule reçoit event, lit player data via DataNode
6. EconomyModule calcule bonus, met à jour `data/economy/bonuses`
7. EconomyModule sauvegarde via `tree->saveNode()`
8. Vérifier:
   - Save → publish → subscribe → read → save chain fonctionne
   - Data persistence correcte
   - Pas de race conditions

#### Test 3: Multi-Module State Synchronization (90s)
1. PlayerModule met à jour `data/player/gold` = 1000
2. PlayerModule publie "player:gold:updated" avec {gold: 1000}
3. EconomyModule reçoit event via IO
4. EconomyModule lit `data/player/gold` via DataNode
5. Vérifier cohérence:
   - Valeur dans message IO = valeur dans DataNode
   - Pas de désynchronisation
   - Order des events préservé
6. Répéter 100 fois avec updates rapides
7. Vérifier consistency finale

#### Test 4: Runtime Metrics Collection (60s)
1. MetricsModule collecte metrics toutes les 100ms:
   - `runtime/fps` - FPS actuel
   - `runtime/memory` - Memory usage
   - `runtime/message_count` - Messages IO
2. MetricsModule publie "metrics:snapshot" toutes les secondes
3. ConfigWatcherModule souscrit et log metrics
4. Vérifier:
   - Runtime data pas persisté (pas de fichiers)
   - Metrics publishing fonctionne
   - Low-frequency batching optimise (pas 10 msg/s mais 1 msg/s)

#### Test 5: Concurrent Access (IO + DataNode) (90s)
1. Lancer 4 threads:
   - Thread 1: PlayerModule publie events à 100 Hz
   - Thread 2: EconomyModule lit data/ à 50 Hz
   - Thread 3: MetricsModule écrit runtime/ à 100 Hz
   - Thread 4: ConfigWatcherModule lit config/ à 10 Hz
2. Exécuter pendant 60 secondes
3. Vérifier:
   - Aucun crash
   - Aucune corruption de data
   - Aucun deadlock
   - Performance acceptable (< 10% overhead)

#### Test 6: Hot-Reload Module + Preserve State (90s)
1. PlayerModule a state actif:
   - 50 entities dans `runtime/entities`
   - Gold = 5000 dans `data/player/gold`
   - Active quest dans `runtime/quest`
2. Déclencher hot-reload de PlayerModule:
   - `getState()` extrait tout (data/ + runtime/)
   - Recompile module
   - `setState()` restaure
3. Pendant reload:
   - EconomyModule continue de publier via IO
   - Messages accumulés dans queue PlayerModule
4. Après reload:
   - PlayerModule pull messages accumulés
   - Vérifie state préservé (50 entities, 5000 gold, quest)
   - Continue processing normalement
5. Vérifier:
   - State complet préservé (DataNode + runtime)
   - Messages pas perdus (IO queue)
   - Pas de corruption

#### Test 7: Config Change Cascades (60s)
1. Modifier `config/gameplay.json` → difficulty = "hard"
2. ConfigWatcherModule détecte → publie "config:gameplay:changed"
3. PlayerModule reçoit → reload config → ajuste HP multiplier
4. PlayerModule publie "player:config:updated"
5. EconomyModule reçoit → reload config → ajuste prices
6. EconomyModule publie "economy:config:updated"
7. MetricsModule reçoit → log cascade
8. Vérifier:
   - Cascade complète en < 500ms
   - Tous modules synchronisés
   - Ordre des events correct

#### Test 8: Large State + High-Frequency IO (60s)
1. Créer large DataNode tree (1000 nodes)
2. Publier 10k messages/s via IO
3. Modules lisent DataNode pendant IO flood
4. Mesurer:
   - Latence IO: < 10ms p99
   - Latence DataNode read: < 5ms p99
   - Memory growth: < 20MB
   - CPU usage: < 80%
5. Vérifier:
   - Systèmes restent performants
   - Pas de dégradation mutuelle

---

## 🏗️ Implémentation

### ConfigWatcherModule Structure

```cpp
// ConfigWatcherModule.h
class ConfigWatcherModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

private:
    std::shared_ptr<IIO> io;
    std::shared_ptr<IDataTree> tree;

    void onConfigReloaded();
    void publishConfigChange(const std::string& configName);
};
```

### PlayerModule Structure

```cpp
// PlayerModule.h
class PlayerModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

private:
    std::shared_ptr<IIO> io;
    std::shared_ptr<IDataTree> tree;

    int gold = 0;
    int level = 1;
    std::vector<std::string> inventory;

    void handleConfigChange();
    void savePlayerData();
    void publishLevelUp();
};
```

### Test Principal

```cpp
// test_13_cross_system.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestAssertions.h"
#include "helpers/TestReporter.h"
#include <atomic>
#include <thread>

int main() {
    TestReporter reporter("Cross-System Integration");
    TestMetrics metrics;

    // === SETUP ===
    std::filesystem::create_directories("test_cross/config");
    std::filesystem::create_directories("test_cross/data");

    auto tree = std::make_shared<JsonDataTree>("test_cross");

    DebugEngine engine;
    engine.setDataTree(tree);

    // Charger modules
    engine.loadModule("ConfigWatcherModule", "build/modules/libConfigWatcherModule.so");
    engine.loadModule("PlayerModule", "build/modules/libPlayerModule.so");
    engine.loadModule("EconomyModule", "build/modules/libEconomyModule.so");
    engine.loadModule("MetricsModule", "build/modules/libMetricsModule.so");

    auto config = createJsonConfig({
        {"transport", "intra"},
        {"instanceId", "test_cross"}
    });

    engine.initializeModule("ConfigWatcherModule", config);
    engine.initializeModule("PlayerModule", config);
    engine.initializeModule("EconomyModule", config);
    engine.initializeModule("MetricsModule", config);

    // ========================================================================
    // TEST 1: Config Hot-Reload → IO Broadcast
    // ========================================================================
    std::cout << "\n=== TEST 1: Config Hot-Reload → IO Broadcast ===\n";

    // Créer config initial
    nlohmann::json gameplayConfig = {
        {"difficulty", "normal"},
        {"hpMultiplier", 1.0}
    };

    std::ofstream configFile("test_cross/config/gameplay.json");
    configFile << gameplayConfig.dump(2);
    configFile.close();

    tree->loadConfigFile("gameplay.json");

    // Setup reload callback
    std::atomic<int> configChangedEvents{0};
    auto playerIO = engine.getModuleIO("PlayerModule");

    playerIO->subscribe("config:gameplay:changed", {});

    // ConfigWatcherModule setup callback
    tree->onTreeReloaded([&]() {
        std::cout << "  → Config reloaded, publishing event...\n";
        auto watcherIO = engine.getModuleIO("ConfigWatcherModule");
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{
            {"config", "gameplay"},
            {"timestamp", std::time(nullptr)}
        });
        watcherIO->publish("config:gameplay:changed", std::move(data));
    });

    // Modifier config
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    gameplayConfig["difficulty"] = "hard";
    gameplayConfig["hpMultiplier"] = 1.5;

    std::ofstream configFile2("test_cross/config/gameplay.json");
    configFile2 << gameplayConfig.dump(2);
    configFile2.close();

    auto reloadStart = std::chrono::high_resolution_clock::now();

    // Trigger reload
    tree->reloadIfChanged();

    // Process pour permettre IO routing
    engine.update(1.0f/60.0f);

    // PlayerModule vérifie message
    if (playerIO->hasMessages() > 0) {
        auto msg = playerIO->pullMessage();
        configChangedEvents++;

        // PlayerModule lit nouvelle config
        auto gameplay = tree->getConfigRoot()->getChild("gameplay");
        std::string difficulty = gameplay->getString("difficulty");
        double hpMult = gameplay->getDouble("hpMultiplier");

        std::cout << "  PlayerModule received config change: difficulty=" << difficulty
                  << ", hpMult=" << hpMult << "\n";

        ASSERT_EQ(difficulty, "hard", "Difficulty should be updated");
        ASSERT_EQ(hpMult, 1.5, "HP multiplier should be updated");
    }

    auto reloadEnd = std::chrono::high_resolution_clock::now();
    float reloadLatency = std::chrono::duration<float, std::milli>(reloadEnd - reloadStart).count();

    std::cout << "Total latency (reload + publish + subscribe + read): " << reloadLatency << "ms\n";
    ASSERT_LT(reloadLatency, 100.0f, "Total latency should be < 100ms");
    ASSERT_EQ(configChangedEvents, 1, "Should receive exactly 1 config change event");

    reporter.addMetric("config_reload_latency_ms", reloadLatency);
    reporter.addAssertion("config_hotreload_chain", true);
    std::cout << "✓ TEST 1 PASSED\n";

    // ========================================================================
    // TEST 2: State Persistence + Event Publishing
    // ========================================================================
    std::cout << "\n=== TEST 2: State Persistence + Event Publishing ===\n";

    auto dataRoot = tree->getDataRoot();

    // PlayerModule crée state
    auto player = std::make_shared<JsonDataNode>("player", nlohmann::json::object());
    auto profile = std::make_shared<JsonDataNode>("profile", nlohmann::json{
        {"name", "TestPlayer"},
        {"level", 5},
        {"gold", 1000}
    });
    player->setChild("profile", profile);
    dataRoot->setChild("player", player);

    // Save
    tree->saveNode("data/player");

    // Verify file
    ASSERT_TRUE(std::filesystem::exists("test_cross/data/player/profile.json"),
                "Profile should be saved");

    // PlayerModule publie level up
    auto levelUpData = std::make_unique<JsonDataNode>(nlohmann::json{
        {"event", "level_up"},
        {"newLevel", 6},
        {"goldBonus", 500}
    });
    playerIO->publish("player:level_up", std::move(levelUpData));

    // EconomyModule souscrit
    auto economyIO = engine.getModuleIO("EconomyModule");
    economyIO->subscribe("player:*", {});

    engine.update(1.0f/60.0f);

    // EconomyModule reçoit et traite
    if (economyIO->hasMessages() > 0) {
        auto msg = economyIO->pullMessage();
        std::cout << "  EconomyModule received: " << msg.topic << "\n";

        // EconomyModule lit player data
        auto playerData = tree->getDataRoot()->getChild("player")->getChild("profile");
        int gold = playerData->getInt("gold");
        std::cout << "  Player gold: " << gold << "\n";

        // EconomyModule calcule bonus
        int goldBonus = 500;
        int newGold = gold + goldBonus;

        // Update data
        playerData->setInt("gold", newGold);

        // Create economy bonuses
        auto economy = std::make_shared<JsonDataNode>("economy", nlohmann::json::object());
        auto bonuses = std::make_shared<JsonDataNode>("bonuses", nlohmann::json{
            {"levelUpBonus", goldBonus},
            {"appliedAt", std::time(nullptr)}
        });
        economy->setChild("bonuses", bonuses);
        dataRoot->setChild("economy", economy);

        // Save economy data
        tree->saveNode("data/economy");

        std::cout << "  EconomyModule updated bonuses and saved\n";
    }

    // Verify full chain
    ASSERT_TRUE(std::filesystem::exists("test_cross/data/economy/bonuses.json"),
                "Economy bonuses should be saved");

    reporter.addAssertion("state_persistence_chain", true);
    std::cout << "✓ TEST 2 PASSED\n";

    // ========================================================================
    // TEST 3: Multi-Module State Synchronization
    // ========================================================================
    std::cout << "\n=== TEST 3: Multi-Module State Synchronization ===\n";

    int syncErrors = 0;

    for (int i = 0; i < 100; i++) {
        // PlayerModule met à jour gold
        int goldValue = 1000 + i * 10;
        auto playerProfile = tree->getDataRoot()->getChild("player")->getChild("profile");
        playerProfile->setInt("gold", goldValue);

        // PlayerModule publie event avec valeur
        auto goldUpdate = std::make_unique<JsonDataNode>(nlohmann::json{
            {"event", "gold_updated"},
            {"gold", goldValue}
        });
        playerIO->publish("player:gold:updated", std::move(goldUpdate));

        engine.update(1.0f/60.0f);

        // EconomyModule reçoit et vérifie cohérence
        if (economyIO->hasMessages() > 0) {
            auto msg = economyIO->pullMessage();
            auto* msgData = dynamic_cast<JsonDataNode*>(msg.data.get());
            int msgGold = msgData->getJsonData()["gold"];

            // Lire DataNode
            auto playerData = tree->getDataRoot()->getChild("player")->getChild("profile");
            int dataGold = playerData->getInt("gold");

            if (msgGold != dataGold) {
                std::cerr << "  SYNC ERROR: msg=" << msgGold << " data=" << dataGold << "\n";
                syncErrors++;
            }
        }
    }

    std::cout << "Synchronization errors: " << syncErrors << " / 100\n";
    ASSERT_EQ(syncErrors, 0, "Should have zero synchronization errors");

    reporter.addMetric("sync_errors", syncErrors);
    reporter.addAssertion("state_synchronization", syncErrors == 0);
    std::cout << "✓ TEST 3 PASSED\n";

    // ========================================================================
    // TEST 4: Runtime Metrics Collection
    // ========================================================================
    std::cout << "\n=== TEST 4: Runtime Metrics Collection ===\n";

    auto runtimeRoot = tree->getRuntimeRoot();
    auto metricsIO = engine.getModuleIO("MetricsModule");

    // MetricsModule publie metrics avec low-freq batching
    IIO::SubscriptionConfig metricsConfig;
    metricsConfig.replaceable = true;
    metricsConfig.batchInterval = 1000; // 1 second

    playerIO->subscribeLowFreq("metrics:*", metricsConfig);

    // Simulate 3 seconds de metrics collection
    for (int sec = 0; sec < 3; sec++) {
        for (int i = 0; i < 10; i++) {
            // MetricsModule collecte metrics
            auto metrics = std::make_shared<JsonDataNode>("metrics", nlohmann::json{
                {"fps", 60.0},
                {"memory", 125000000 + i * 1000},
                {"messageCount", i * 100}
            });
            runtimeRoot->setChild("metrics", metrics);

            // Publie snapshot
            auto snapshot = std::make_unique<JsonDataNode>(nlohmann::json{
                {"fps", 60.0},
                {"memory", 125000000 + i * 1000},
                {"timestamp", std::time(nullptr)}
            });
            metricsIO->publish("metrics:snapshot", std::move(snapshot));

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            engine.update(1.0f/60.0f);
        }
    }

    // Vérifier batching
    int snapshotsReceived = 0;
    while (playerIO->hasMessages() > 0) {
        playerIO->pullMessage();
        snapshotsReceived++;
    }

    std::cout << "Snapshots received: " << snapshotsReceived << " (expected ~3 due to batching)\n";
    ASSERT_TRUE(snapshotsReceived >= 2 && snapshotsReceived <= 4,
                "Should receive ~3 batched snapshots");

    // Vérifier runtime pas persisté
    ASSERT_FALSE(std::filesystem::exists("test_cross/runtime"),
                 "Runtime data should not be persisted");

    reporter.addMetric("batched_snapshots", snapshotsReceived);
    reporter.addAssertion("runtime_metrics", true);
    std::cout << "✓ TEST 4 PASSED\n";

    // ========================================================================
    // TEST 5: Concurrent Access (IO + DataNode)
    // ========================================================================
    std::cout << "\n=== TEST 5: Concurrent Access ===\n";

    std::atomic<bool> running{true};
    std::atomic<int> publishCount{0};
    std::atomic<int> readCount{0};
    std::atomic<int> writeCount{0};
    std::atomic<int> errors{0};

    // Thread 1: PlayerModule publie events
    std::thread pubThread([&]() {
        while (running) {
            try {
                auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"id", publishCount++}});
                playerIO->publish("concurrent:test", std::move(data));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } catch (...) {
                errors++;
            }
        }
    });

    // Thread 2: EconomyModule lit data/
    std::thread readThread([&]() {
        while (running) {
            try {
                auto playerData = tree->getDataRoot()->getChild("player");
                if (playerData) {
                    auto profile = playerData->getChild("profile");
                    if (profile) {
                        int gold = profile->getInt("gold", 0);
                        readCount++;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } catch (...) {
                errors++;
            }
        }
    });

    // Thread 3: MetricsModule écrit runtime/
    std::thread writeThread([&]() {
        while (running) {
            try {
                auto metrics = std::make_shared<JsonDataNode>("metrics", nlohmann::json{
                    {"counter", writeCount++}
                });
                runtimeRoot->setChild("metrics", metrics);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } catch (...) {
                errors++;
            }
        }
    });

    // Thread 4: ConfigWatcherModule lit config/
    std::thread configThread([&]() {
        while (running) {
            try {
                auto gameplay = tree->getConfigRoot()->getChild("gameplay");
                if (gameplay) {
                    std::string diff = gameplay->getString("difficulty", "normal");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } catch (...) {
                errors++;
            }
        }
    });

    // Run for 5 seconds
    auto concurrentStart = std::chrono::high_resolution_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    running = false;
    auto concurrentEnd = std::chrono::high_resolution_clock::now();

    pubThread.join();
    readThread.join();
    writeThread.join();
    configThread.join();

    float duration = std::chrono::duration<float>(concurrentEnd - concurrentStart).count();

    std::cout << "Concurrent test ran for " << duration << "s\n";
    std::cout << "  Publishes: " << publishCount << "\n";
    std::cout << "  Reads: " << readCount << "\n";
    std::cout << "  Writes: " << writeCount << "\n";
    std::cout << "  Errors: " << errors << "\n";

    ASSERT_EQ(errors, 0, "Should have zero errors during concurrent access");
    ASSERT_GT(publishCount, 0, "Should have published messages");
    ASSERT_GT(readCount, 0, "Should have read data");
    ASSERT_GT(writeCount, 0, "Should have written data");

    reporter.addMetric("concurrent_publishes", publishCount);
    reporter.addMetric("concurrent_reads", readCount);
    reporter.addMetric("concurrent_writes", writeCount);
    reporter.addMetric("concurrent_errors", errors);
    reporter.addAssertion("concurrent_access", errors == 0);
    std::cout << "✓ TEST 5 PASSED\n";

    // ========================================================================
    // TEST 6: Hot-Reload Module + Preserve State
    // ========================================================================
    std::cout << "\n=== TEST 6: Hot-Reload Module + Preserve State ===\n";

    // PlayerModule crée state complexe
    auto entities = std::make_shared<JsonDataNode>("entities", nlohmann::json::array());
    for (int i = 0; i < 50; i++) {
        entities->getJsonData().push_back({{"id", i}, {"hp", 100}});
    }
    runtimeRoot->setChild("entities", entities);

    auto playerGold = tree->getDataRoot()->getChild("player")->getChild("profile");
    playerGold->setInt("gold", 5000);
    tree->saveNode("data/player/profile");

    auto quest = std::make_shared<JsonDataNode>("quest", nlohmann::json{
        {"active", true},
        {"questId", 42}
    });
    runtimeRoot->setChild("quest", quest);

    std::cout << "State before reload: 50 entities, 5000 gold, quest #42 active\n";

    // EconomyModule publie messages pendant reload
    std::thread spamThread([&]() {
        for (int i = 0; i < 100; i++) {
            auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"spam", i}});
            economyIO->publish("player:spam", std::move(data));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Trigger hot-reload de PlayerModule
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto stateBefore = engine.getModuleState("PlayerModule");

    modifySourceFile("tests/modules/PlayerModule.cpp", "v1.0", "v2.0");
    system("cmake --build build --target PlayerModule 2>&1 > /dev/null");

    engine.reloadModule("PlayerModule");

    spamThread.join();

    // Vérifier state après reload
    auto stateAfter = engine.getModuleState("PlayerModule");

    auto entitiesAfter = runtimeRoot->getChild("entities");
    int entityCount = entitiesAfter->getJsonData().size();
    std::cout << "Entities after reload: " << entityCount << "\n";
    ASSERT_EQ(entityCount, 50, "Should preserve 50 entities");

    auto goldAfter = tree->getDataRoot()->getChild("player")->getChild("profile");
    int goldValue = goldAfter->getInt("gold");
    std::cout << "Gold after reload: " << goldValue << "\n";
    ASSERT_EQ(goldValue, 5000, "Should preserve 5000 gold");

    auto questAfter = runtimeRoot->getChild("quest");
    bool questActive = questAfter->getBool("active");
    int questId = questAfter->getInt("questId");
    std::cout << "Quest after reload: active=" << questActive << ", id=" << questId << "\n";
    ASSERT_EQ(questActive, true, "Quest should still be active");
    ASSERT_EQ(questId, 42, "Quest ID should be preserved");

    // Vérifier messages pas perdus
    int spamReceived = 0;
    while (playerIO->hasMessages() > 0) {
        playerIO->pullMessage();
        spamReceived++;
    }
    std::cout << "Spam messages received after reload: " << spamReceived << "\n";
    ASSERT_GT(spamReceived, 0, "Should receive queued messages after reload");

    reporter.addAssertion("hotreload_preserve_state", true);
    reporter.addMetric("spam_messages_queued", spamReceived);
    std::cout << "✓ TEST 6 PASSED\n";

    // ========================================================================
    // TEST 7: Config Change Cascades
    // ========================================================================
    std::cout << "\n=== TEST 7: Config Change Cascades ===\n";

    // Subscribe chain
    playerIO->subscribe("config:*", {});
    economyIO->subscribe("player:*", {});
    metricsIO->subscribe("economy:*", {});

    auto cascadeStart = std::chrono::high_resolution_clock::now();

    // 1. Modifier config
    gameplayConfig["difficulty"] = "extreme";
    std::ofstream configFile3("test_cross/config/gameplay.json");
    configFile3 << gameplayConfig.dump(2);
    configFile3.close();

    // 2. Trigger reload
    tree->reloadIfChanged();
    auto watcherIO = engine.getModuleIO("ConfigWatcherModule");
    watcherIO->publish("config:gameplay:changed", std::make_unique<JsonDataNode>(nlohmann::json{{"config", "gameplay"}}));

    engine.update(1.0f/60.0f);

    // 3. PlayerModule reçoit et publie
    if (playerIO->hasMessages() > 0) {
        playerIO->pullMessage();
        playerIO->publish("player:config:updated", std::make_unique<JsonDataNode>(nlohmann::json{{"hpMult", 2.0}}));
    }

    engine.update(1.0f/60.0f);

    // 4. EconomyModule reçoit et publie
    if (economyIO->hasMessages() > 0) {
        economyIO->pullMessage();
        economyIO->publish("economy:config:updated", std::make_unique<JsonDataNode>(nlohmann::json{{"pricesMult", 1.5}}));
    }

    engine.update(1.0f/60.0f);

    // 5. MetricsModule reçoit et log
    if (metricsIO->hasMessages() > 0) {
        metricsIO->pullMessage();
        std::cout << "  → Cascade complete!\n";
    }

    auto cascadeEnd = std::chrono::high_resolution_clock::now();
    float cascadeTime = std::chrono::duration<float, std::milli>(cascadeEnd - cascadeStart).count();

    std::cout << "Cascade latency: " << cascadeTime << "ms\n";
    ASSERT_LT(cascadeTime, 500.0f, "Cascade should complete in < 500ms");

    reporter.addMetric("cascade_latency_ms", cascadeTime);
    reporter.addAssertion("config_cascade", true);
    std::cout << "✓ TEST 7 PASSED\n";

    // ========================================================================
    // TEST 8: Large State + High-Frequency IO
    // ========================================================================
    std::cout << "\n=== TEST 8: Large State + High-Frequency IO ===\n";

    // Créer large tree (1000 nodes)
    auto largeRoot = tree->getDataRoot();
    for (int i = 0; i < 100; i++) {
        auto category = std::make_shared<JsonDataNode>("cat_" + std::to_string(i), nlohmann::json::object());
        for (int j = 0; j < 10; j++) {
            auto item = std::make_shared<JsonDataNode>("item_" + std::to_string(j), nlohmann::json{
                {"id", i * 10 + j},
                {"value", (i * 10 + j) * 100}
            });
            category->setChild("item_" + std::to_string(j), item);
        }
        largeRoot->setChild("cat_" + std::to_string(i), category);
    }

    std::cout << "Created large DataNode tree (1000 nodes)\n";

    // High-frequency IO + concurrent DataNode reads
    std::atomic<int> ioPublished{0};
    std::atomic<int> dataReads{0};
    std::vector<float> ioLatencies;
    std::vector<float> dataLatencies;

    running = true;

    std::thread ioThread([&]() {
        while (running) {
            auto start = std::chrono::high_resolution_clock::now();
            auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"id", ioPublished++}});
            playerIO->publish("stress:test", std::move(data));
            auto end = std::chrono::high_resolution_clock::now();

            float latency = std::chrono::duration<float, std::milli>(end - start).count();
            ioLatencies.push_back(latency);

            // Target: 10k msg/s = 0.1ms interval
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    std::thread dataThread([&]() {
        while (running) {
            auto start = std::chrono::high_resolution_clock::now();
            auto cat = largeRoot->getChild("cat_50");
            if (cat) {
                auto item = cat->getChild("item_5");
                if (item) {
                    int value = item->getInt("value", 0);
                    dataReads++;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();

            float latency = std::chrono::duration<float, std::milli>(end - start).count();
            dataLatencies.push_back(latency);

            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    auto memBefore = getCurrentMemoryUsage();

    std::this_thread::sleep_for(std::chrono::seconds(5));
    running = false;

    ioThread.join();
    dataThread.join();

    auto memAfter = getCurrentMemoryUsage();
    long memGrowth = static_cast<long>(memAfter) - static_cast<long>(memBefore);

    // Calculate p99 latencies
    std::sort(ioLatencies.begin(), ioLatencies.end());
    std::sort(dataLatencies.begin(), dataLatencies.end());

    float ioP99 = ioLatencies[static_cast<size_t>(ioLatencies.size() * 0.99)];
    float dataP99 = dataLatencies[static_cast<size_t>(dataLatencies.size() * 0.99)];

    std::cout << "Performance results:\n";
    std::cout << "  IO published: " << ioPublished << " messages\n";
    std::cout << "  IO p99 latency: " << ioP99 << "ms\n";
    std::cout << "  DataNode reads: " << dataReads << "\n";
    std::cout << "  DataNode p99 latency: " << dataP99 << "ms\n";
    std::cout << "  Memory growth: " << (memGrowth / 1024.0 / 1024.0) << "MB\n";

    ASSERT_LT(ioP99, 10.0f, "IO p99 latency should be < 10ms");
    ASSERT_LT(dataP99, 5.0f, "DataNode p99 latency should be < 5ms");
    ASSERT_LT(memGrowth, 20 * 1024 * 1024, "Memory growth should be < 20MB");

    reporter.addMetric("io_p99_latency_ms", ioP99);
    reporter.addMetric("datanode_p99_latency_ms", dataP99);
    reporter.addMetric("memory_growth_mb", memGrowth / 1024.0 / 1024.0);
    reporter.addAssertion("performance_under_load", true);
    std::cout << "✓ TEST 8 PASSED\n";

    // ========================================================================
    // CLEANUP
    // ========================================================================
    std::filesystem::remove_all("test_cross");

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
| **config_reload_latency_ms** | Latence reload→publish→subscribe→read | < 100ms |
| **sync_errors** | Erreurs synchronisation IO/DataNode | 0 |
| **batched_snapshots** | Snapshots reçus avec batching | 2-4 |
| **concurrent_publishes** | Messages publiés en concurrence | > 0 |
| **concurrent_reads** | Lectures DataNode concurrentes | > 0 |
| **concurrent_writes** | Écritures DataNode concurrentes | > 0 |
| **concurrent_errors** | Erreurs pendant concurrence | 0 |
| **spam_messages_queued** | Messages queued pendant reload | > 0 |
| **cascade_latency_ms** | Latence cascade config changes | < 500ms |
| **io_p99_latency_ms** | P99 latence IO sous charge | < 10ms |
| **datanode_p99_latency_ms** | P99 latence DataNode sous charge | < 5ms |
| **memory_growth_mb** | Croissance mémoire sous charge | < 20MB |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ Config hot-reload chain fonctionne (< 100ms)
2. ✅ State persistence + event publishing chain fonctionne
3. ✅ Synchronization IO/DataNode sans erreurs
4. ✅ Runtime metrics avec batching
5. ✅ Concurrent access sans crashes/corruption
6. ✅ Hot-reload préserve state complet
7. ✅ Messages IO pas perdus pendant reload
8. ✅ Config cascades propagent correctement
9. ✅ Performance acceptable sous charge

### NICE TO HAVE
1. ✅ Config reload latency < 50ms (optimal)
2. ✅ Cascade latency < 200ms (optimal)
3. ✅ IO p99 < 5ms (optimal)
4. ✅ DataNode p99 < 2ms (optimal)

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| Config change pas propagé | Callback pas déclenché | FAIL - fix onTreeReloaded |
| Sync errors > 0 | Race condition IO/DataNode | FAIL - add locking |
| Messages perdus | Queue overflow pendant reload | WARN - increase queue size |
| Concurrent crashes | Missing mutex | FAIL - add thread safety |
| State corrompu après reload | setState() bug | FAIL - fix state restoration |
| Cascade timeout | Deadlock dans chain | FAIL - fix event routing |
| Performance degradation | O(n²) algorithm | FAIL - optimize |
| Memory leak | Resources not freed | FAIL - fix destructors |

---

## 📝 Output Attendu

```
================================================================================
TEST: Cross-System Integration (IO + DataNode)
================================================================================

=== TEST 1: Config Hot-Reload → IO Broadcast ===
  → Config reloaded, publishing event...
  PlayerModule received config change: difficulty=hard, hpMult=1.5
Total latency (reload + publish + subscribe + read): 87ms
✓ TEST 1 PASSED

=== TEST 2: State Persistence + Event Publishing ===
  EconomyModule received: player:level_up
  Player gold: 1000
  EconomyModule updated bonuses and saved
✓ TEST 2 PASSED

=== TEST 3: Multi-Module State Synchronization ===
Synchronization errors: 0 / 100
✓ TEST 3 PASSED

=== TEST 4: Runtime Metrics Collection ===
Snapshots received: 3 (expected ~3 due to batching)
✓ TEST 4 PASSED

=== TEST 5: Concurrent Access ===
Concurrent test ran for 5.001s
  Publishes: 487
  Reads: 243
  Writes: 489
  Errors: 0
✓ TEST 5 PASSED

=== TEST 6: Hot-Reload Module + Preserve State ===
State before reload: 50 entities, 5000 gold, quest #42 active
Entities after reload: 50
Gold after reload: 5000
Quest after reload: active=true, id=42
Spam messages received after reload: 94
✓ TEST 6 PASSED

=== TEST 7: Config Change Cascades ===
  → Cascade complete!
Cascade latency: 234ms
✓ TEST 7 PASSED

=== TEST 8: Large State + High-Frequency IO ===
Created large DataNode tree (1000 nodes)
Performance results:
  IO published: 48723 messages
  IO p99 latency: 8.3ms
  DataNode reads: 9745
  DataNode p99 latency: 3.2ms
  Memory growth: 14.7MB
✓ TEST 8 PASSED

================================================================================
METRICS
================================================================================
  Config reload latency:     87ms           (threshold: < 100ms) ✓
  Sync errors:               0              (threshold: 0)       ✓
  Batched snapshots:         3
  Concurrent publishes:      487
  Concurrent reads:          243
  Concurrent writes:         489
  Concurrent errors:         0              (threshold: 0)       ✓
  Spam messages queued:      94
  Cascade latency:           234ms          (threshold: < 500ms) ✓
  IO p99 latency:            8.3ms          (threshold: < 10ms)  ✓
  DataNode p99 latency:      3.2ms          (threshold: < 5ms)   ✓
  Memory growth:             14.7MB         (threshold: < 20MB)  ✓

================================================================================
ASSERTIONS
================================================================================
  ✓ config_hotreload_chain
  ✓ state_persistence_chain
  ✓ state_synchronization
  ✓ runtime_metrics
  ✓ concurrent_access
  ✓ hotreload_preserve_state
  ✓ config_cascade
  ✓ performance_under_load

Result: ✅ PASSED (8/8 tests)

================================================================================
```

---

## 📅 Planning

**Jour 1 (4h):**
- Implémenter ConfigWatcherModule, PlayerModule, EconomyModule, MetricsModule
- Setup IDataTree avec structure config/data/runtime
- Tests 1-3 (config reload, persistence, sync)

**Jour 2 (4h):**
- Tests 4-6 (metrics, concurrent, hot-reload)
- Tests 7-8 (cascades, performance)
- Debug + validation

---

**Conclusion**: Ces 3 nouveaux scénarios (11, 12, 13) complètent la suite de tests d'intégration en couvrant les systèmes IO et DataNode, ainsi que leur intégration.
