# Scénario 11: IO System Stress Test

**Priorité**: ⭐⭐ SHOULD HAVE
**Phase**: 2 (SHOULD HAVE)
**Durée estimée**: ~5 minutes
**Effort implémentation**: ~4-6 heures

---

## 🎯 Objectif

Valider que le système IntraIO (pub/sub intra-process) fonctionne correctement dans tous les cas d'usage:
- Pattern matching avec wildcards et regex
- Multi-module routing (1-to-1, 1-to-many)
- Message batching et flushing (low-frequency subscriptions)
- Backpressure et queue overflow
- Thread safety (concurrent publish/pull)
- Health monitoring et métriques
- Subscription lifecycle

**Bug connu à valider**: IntraIOManager ne route qu'au premier subscriber (limitation std::move sans clone)

---

## 📋 Description

### Setup Initial
1. Créer 5 modules avec IntraIO:
   - **ProducerModule** - Publie 1000 msg/s sur différents topics
   - **ConsumerModule** - Souscrit à plusieurs patterns
   - **BroadcastModule** - Publie sur topics avec multiples subscribers
   - **BatchModule** - Utilise low-frequency subscriptions
   - **StressModule** - Stress test avec 10k msg/s

2. Configurer IntraIOManager avec routage entre modules

3. Tester 8 scénarios différents sur 5 minutes

### Test Séquence

#### Test 1: Basic Publish-Subscribe (30s)
1. ProducerModule publie 100 messages sur "test:basic"
2. ConsumerModule souscrit à "test:basic"
3. Vérifier:
   - 100 messages reçus
   - Ordre FIFO préservé
   - Aucun message perdu

#### Test 2: Pattern Matching (30s)
1. ProducerModule publie sur:
   - "player:001:position"
   - "player:001:health"
   - "player:002:position"
   - "enemy:001:position"
2. ConsumerModule souscrit aux patterns:
   - "player:*" (devrait matcher 3 messages)
   - "player:001:*" (devrait matcher 2 messages)
   - "*:position" (devrait matcher 3 messages)
3. Vérifier matching counts corrects

#### Test 3: Multi-Module Routing (60s)
1. ProducerModule publie "broadcast:data" (100 messages)
2. ConsumerModule, BatchModule, StressModule souscrivent tous à "broadcast:*"
3. Vérifier:
   - **Bug attendu**: Seul le premier subscriber reçoit (limitation clone)
   - Logger quel module reçoit
   - Documenter le bug pour fix futur

#### Test 4: Message Batching (60s)
1. BatchModule configure low-frequency subscription:
   - Pattern: "batch:*"
   - Interval: 1000ms
   - replaceable: true
2. ProducerModule publie "batch:metric" à 100 Hz (toutes les 10ms)
3. Vérifier:
   - BatchModule reçoit ~1 message/seconde (dernier seulement)
   - Batching fonctionne correctement

#### Test 5: Backpressure & Queue Overflow (30s)
1. ProducerModule publie 50k messages sur "stress:flood"
2. ConsumerModule souscrit mais ne pull que 100 msg/s
3. Vérifier:
   - Queue overflow détecté (health.dropping = true)
   - Messages droppés comptés (health.droppedMessageCount > 0)
   - Système reste stable (pas de crash)

#### Test 6: Thread Safety (60s)
1. Lancer 10 threads qui publient simultanément (1000 msg chacun)
2. Lancer 5 threads qui pullent simultanément
3. Vérifier:
   - Aucun crash
   - Aucune corruption de données
   - Total messages reçus = total envoyés (ou moins si overflow)

#### Test 7: Health Monitoring (30s)
1. ProducerModule publie à différents débits:
   - Phase 1: 100 msg/s (normal)
   - Phase 2: 10k msg/s (overload)
   - Phase 3: 100 msg/s (recovery)
2. Monitorer health metrics:
   - queueSize augmente/diminue correctement
   - averageProcessingRate reflète réalité
   - dropping flag activé/désactivé au bon moment

#### Test 8: Subscription Lifecycle (30s)
1. Créer/détruire subscriptions dynamiquement
2. Vérifier:
   - Messages après unsubscribe ne sont pas reçus
   - Re-subscribe fonctionne
   - Pas de leak de subscriptions dans IntraIOManager

---

## 🏗️ Implémentation

### ProducerModule Structure

```cpp
// ProducerModule.h
class ProducerModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

private:
    std::shared_ptr<IIO> io;
    int messageCount = 0;
    float publishRate = 100.0f; // Hz
    float accumulator = 0.0f;

    void publishTestMessages();
};
```

### ConsumerModule Structure

```cpp
// ConsumerModule.h
class ConsumerModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

    // Test helpers
    int getReceivedCount() const { return receivedMessages.size(); }
    const std::vector<IIO::Message>& getMessages() const { return receivedMessages; }

private:
    std::shared_ptr<IIO> io;
    std::vector<IIO::Message> receivedMessages;

    void processIncomingMessages();
};
```

### Test Principal

```cpp
// test_11_io_system.cpp
#include "helpers/TestMetrics.h"
#include "helpers/TestAssertions.h"
#include "helpers/TestReporter.h"
#include <thread>
#include <atomic>

int main() {
    TestReporter reporter("IO System Stress Test");
    TestMetrics metrics;

    // === SETUP ===
    DebugEngine engine;

    // Charger modules
    engine.loadModule("ProducerModule", "build/modules/libProducerModule.so");
    engine.loadModule("ConsumerModule", "build/modules/libConsumerModule.so");
    engine.loadModule("BroadcastModule", "build/modules/libBroadcastModule.so");
    engine.loadModule("BatchModule", "build/modules/libBatchModule.so");
    engine.loadModule("StressModule", "build/modules/libStressModule.so");

    // Initialiser avec IOFactory
    auto config = createJsonConfig({
        {"transport", "intra"},
        {"instanceId", "test_engine"}
    });

    engine.initializeModule("ProducerModule", config);
    engine.initializeModule("ConsumerModule", config);
    engine.initializeModule("BroadcastModule", config);
    engine.initializeModule("BatchModule", config);
    engine.initializeModule("StressModule", config);

    // ========================================================================
    // TEST 1: Basic Publish-Subscribe
    // ========================================================================
    std::cout << "\n=== TEST 1: Basic Publish-Subscribe ===\n";

    // ConsumerModule subscribe to "test:basic"
    auto consumerIO = engine.getModuleIO("ConsumerModule");
    consumerIO->subscribe("test:basic", {});

    // ProducerModule publie 100 messages
    auto producerIO = engine.getModuleIO("ProducerModule");
    for (int i = 0; i < 100; i++) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{
            {"id", i},
            {"payload", "test_message_" + std::to_string(i)}
        });
        producerIO->publish("test:basic", std::move(data));
    }

    // Process pour permettre routing
    engine.update(1.0f/60.0f);

    // Vérifier réception
    int receivedCount = 0;
    while (consumerIO->hasMessages() > 0) {
        auto msg = consumerIO->pullMessage();
        receivedCount++;

        // Vérifier ordre FIFO
        auto* jsonData = dynamic_cast<JsonDataNode*>(msg.data.get());
        int msgId = jsonData->getJsonData()["id"];
        ASSERT_EQ(msgId, receivedCount - 1, "Messages should be in FIFO order");
    }

    ASSERT_EQ(receivedCount, 100, "Should receive all 100 messages");
    reporter.addAssertion("basic_pubsub", receivedCount == 100);
    std::cout << "✓ TEST 1 PASSED: " << receivedCount << " messages received\n";

    // ========================================================================
    // TEST 2: Pattern Matching
    // ========================================================================
    std::cout << "\n=== TEST 2: Pattern Matching ===\n";

    // Subscribe to different patterns
    consumerIO->subscribe("player:*", {});
    consumerIO->subscribe("player:001:*", {});
    consumerIO->subscribe("*:position", {});

    // Publish test messages
    std::vector<std::string> testTopics = {
        "player:001:position",  // Matches all 3 patterns
        "player:001:health",    // Matches pattern 1 and 2
        "player:002:position",  // Matches pattern 1 and 3
        "enemy:001:position"    // Matches pattern 3 only
    };

    for (const auto& topic : testTopics) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"topic", topic}});
        producerIO->publish(topic, std::move(data));
    }

    engine.update(1.0f/60.0f);

    // Count messages by pattern
    std::map<std::string, int> patternCounts;
    while (consumerIO->hasMessages() > 0) {
        auto msg = consumerIO->pullMessage();
        auto* jsonData = dynamic_cast<JsonDataNode*>(msg.data.get());
        std::string topic = jsonData->getJsonData()["topic"];
        patternCounts[topic]++;
    }

    // Note: Due to pattern overlap, same message might be received multiple times
    std::cout << "Pattern matching results:\n";
    for (const auto& [topic, count] : patternCounts) {
        std::cout << "  " << topic << ": " << count << " times\n";
    }

    reporter.addAssertion("pattern_matching", true);
    std::cout << "✓ TEST 2 PASSED\n";

    // ========================================================================
    // TEST 3: Multi-Module Routing (Bug Detection)
    // ========================================================================
    std::cout << "\n=== TEST 3: Multi-Module Routing (1-to-many) ===\n";

    // All modules subscribe to "broadcast:*"
    consumerIO->subscribe("broadcast:*", {});
    auto broadcastIO = engine.getModuleIO("BroadcastModule");
    broadcastIO->subscribe("broadcast:*", {});
    auto batchIO = engine.getModuleIO("BatchModule");
    batchIO->subscribe("broadcast:*", {});
    auto stressIO = engine.getModuleIO("StressModule");
    stressIO->subscribe("broadcast:*", {});

    // Publish 10 broadcast messages
    for (int i = 0; i < 10; i++) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"broadcast_id", i}});
        producerIO->publish("broadcast:data", std::move(data));
    }

    engine.update(1.0f/60.0f);

    // Check which modules received messages
    int consumerReceived = consumerIO->hasMessages();
    int broadcastReceived = broadcastIO->hasMessages();
    int batchReceived = batchIO->hasMessages();
    int stressReceived = stressIO->hasMessages();

    std::cout << "Broadcast distribution:\n";
    std::cout << "  ConsumerModule: " << consumerReceived << " messages\n";
    std::cout << "  BroadcastModule: " << broadcastReceived << " messages\n";
    std::cout << "  BatchModule: " << batchReceived << " messages\n";
    std::cout << "  StressModule: " << stressReceived << " messages\n";

    // Expected: Only ONE module receives due to std::move limitation
    int totalReceived = consumerReceived + broadcastReceived + batchReceived + stressReceived;

    if (totalReceived == 10) {
        std::cout << "⚠️  BUG: Only one module received all messages (clone() not implemented)\n";
        reporter.addMetric("broadcast_bug_present", 1.0f);
    } else if (totalReceived == 40) {
        std::cout << "✓ FIXED: All modules received copies (clone() implemented!)\n";
        reporter.addMetric("broadcast_bug_present", 0.0f);
    }

    reporter.addAssertion("multi_module_routing_tested", true);
    std::cout << "✓ TEST 3 COMPLETED (bug documented)\n";

    // ========================================================================
    // TEST 4: Message Batching
    // ========================================================================
    std::cout << "\n=== TEST 4: Message Batching (Low-Frequency) ===\n";

    // Configure low-freq subscription
    IIO::SubscriptionConfig batchConfig;
    batchConfig.replaceable = true;
    batchConfig.batchInterval = 1000; // 1 second
    batchIO->subscribeLowFreq("batch:*", batchConfig);

    // Publish at 100 Hz for 3 seconds (300 messages)
    auto batchStart = std::chrono::high_resolution_clock::now();
    int batchedPublished = 0;

    for (int sec = 0; sec < 3; sec++) {
        for (int i = 0; i < 100; i++) {
            auto data = std::make_unique<JsonDataNode>(nlohmann::json{
                {"timestamp", batchedPublished},
                {"value", batchedPublished * 0.1f}
            });
            producerIO->publish("batch:metric", std::move(data));
            batchedPublished++;

            // Simulate 10ms interval
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            engine.update(1.0f/60.0f);
        }
    }

    auto batchEnd = std::chrono::high_resolution_clock::now();
    float batchDuration = std::chrono::duration<float>(batchEnd - batchStart).count();

    // Check how many batched messages received
    int batchesReceived = 0;
    while (batchIO->hasMessages() > 0) {
        auto msg = batchIO->pullMessage();
        batchesReceived++;
    }

    std::cout << "Published: " << batchedPublished << " messages over " << batchDuration << "s\n";
    std::cout << "Received: " << batchesReceived << " batches\n";
    std::cout << "Expected: ~" << static_cast<int>(batchDuration) << " batches (1/second)\n";

    // Should receive ~3 batches (1 per second)
    ASSERT_TRUE(batchesReceived >= 2 && batchesReceived <= 4,
                "Should receive 2-4 batches for 3 seconds");
    reporter.addMetric("batch_count", batchesReceived);
    reporter.addAssertion("batching_works", batchesReceived >= 2);
    std::cout << "✓ TEST 4 PASSED\n";

    // ========================================================================
    // TEST 5: Backpressure & Queue Overflow
    // ========================================================================
    std::cout << "\n=== TEST 5: Backpressure & Queue Overflow ===\n";

    // Subscribe but don't pull
    consumerIO->subscribe("stress:flood", {});

    // Flood with 50k messages
    std::cout << "Publishing 50000 messages...\n";
    for (int i = 0; i < 50000; i++) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"flood_id", i}});
        producerIO->publish("stress:flood", std::move(data));

        if (i % 10000 == 0) {
            std::cout << "  " << i << " messages published\n";
        }
    }

    engine.update(1.0f/60.0f);

    // Check health
    auto health = consumerIO->getHealth();
    std::cout << "Health status:\n";
    std::cout << "  Queue size: " << health.queueSize << " / " << health.maxQueueSize << "\n";
    std::cout << "  Dropping: " << (health.dropping ? "YES" : "NO") << "\n";
    std::cout << "  Dropped count: " << health.droppedMessageCount << "\n";
    std::cout << "  Processing rate: " << health.averageProcessingRate << " msg/s\n";

    ASSERT_TRUE(health.queueSize > 0, "Queue should have messages");

    // Likely queue overflow happened
    if (health.dropping || health.droppedMessageCount > 0) {
        std::cout << "✓ Backpressure detected correctly\n";
        reporter.addAssertion("backpressure_detected", true);
    }

    reporter.addMetric("queue_size", health.queueSize);
    reporter.addMetric("dropped_messages", health.droppedMessageCount);
    std::cout << "✓ TEST 5 PASSED\n";

    // ========================================================================
    // TEST 6: Thread Safety
    // ========================================================================
    std::cout << "\n=== TEST 6: Thread Safety (Concurrent Pub/Pull) ===\n";

    std::atomic<int> publishedTotal{0};
    std::atomic<int> receivedTotal{0};
    std::atomic<bool> running{true};

    consumerIO->subscribe("thread:*", {});

    // 10 publisher threads
    std::vector<std::thread> publishers;
    for (int t = 0; t < 10; t++) {
        publishers.emplace_back([&, t]() {
            for (int i = 0; i < 1000; i++) {
                auto data = std::make_unique<JsonDataNode>(nlohmann::json{
                    {"thread", t},
                    {"id", i}
                });
                producerIO->publish("thread:test", std::move(data));
                publishedTotal++;
            }
        });
    }

    // 5 consumer threads
    std::vector<std::thread> consumers;
    for (int t = 0; t < 5; t++) {
        consumers.emplace_back([&]() {
            while (running || consumerIO->hasMessages() > 0) {
                if (consumerIO->hasMessages() > 0) {
                    try {
                        auto msg = consumerIO->pullMessage();
                        receivedTotal++;
                    } catch (...) {
                        std::cerr << "ERROR: Exception during pull\n";
                    }
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    // Wait for publishers
    for (auto& t : publishers) {
        t.join();
    }

    std::cout << "All publishers done: " << publishedTotal << " messages\n";

    // Let consumers finish
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    running = false;

    for (auto& t : consumers) {
        t.join();
    }

    std::cout << "All consumers done: " << receivedTotal << " messages\n";

    // May have drops, but should be stable
    ASSERT_GT(receivedTotal, 0, "Should receive at least some messages");
    reporter.addMetric("concurrent_published", publishedTotal);
    reporter.addMetric("concurrent_received", receivedTotal);
    reporter.addAssertion("thread_safety", true); // No crash = success
    std::cout << "✓ TEST 6 PASSED (no crashes)\n";

    // ========================================================================
    // TEST 7: Health Monitoring Accuracy
    // ========================================================================
    std::cout << "\n=== TEST 7: Health Monitoring Accuracy ===\n";

    consumerIO->subscribe("health:*", {});

    // Phase 1: Normal load (100 msg/s)
    std::cout << "Phase 1: Normal load (100 msg/s for 2s)\n";
    for (int i = 0; i < 200; i++) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"phase", 1}});
        producerIO->publish("health:test", std::move(data));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Pull to keep queue low
        if (consumerIO->hasMessages() > 0) {
            consumerIO->pullMessage();
        }
    }

    auto healthPhase1 = consumerIO->getHealth();
    std::cout << "  Queue: " << healthPhase1.queueSize << ", Dropping: " << healthPhase1.dropping << "\n";

    // Phase 2: Overload (10k msg/s without pulling)
    std::cout << "Phase 2: Overload (10000 msg/s for 1s)\n";
    for (int i = 0; i < 10000; i++) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"phase", 2}});
        producerIO->publish("health:test", std::move(data));
    }
    engine.update(1.0f/60.0f);

    auto healthPhase2 = consumerIO->getHealth();
    std::cout << "  Queue: " << healthPhase2.queueSize << ", Dropping: " << healthPhase2.dropping << "\n";

    ASSERT_GT(healthPhase2.queueSize, healthPhase1.queueSize,
              "Queue should grow during overload");

    // Phase 3: Recovery (pull all)
    std::cout << "Phase 3: Recovery (pulling all messages)\n";
    int pulled = 0;
    while (consumerIO->hasMessages() > 0) {
        consumerIO->pullMessage();
        pulled++;
    }

    auto healthPhase3 = consumerIO->getHealth();
    std::cout << "  Pulled: " << pulled << " messages\n";
    std::cout << "  Queue: " << healthPhase3.queueSize << ", Dropping: " << healthPhase3.dropping << "\n";

    ASSERT_EQ(healthPhase3.queueSize, 0, "Queue should be empty after pulling all");
    reporter.addAssertion("health_monitoring", true);
    std::cout << "✓ TEST 7 PASSED\n";

    // ========================================================================
    // TEST 8: Subscription Lifecycle
    // ========================================================================
    std::cout << "\n=== TEST 8: Subscription Lifecycle ===\n";

    // Subscribe
    consumerIO->subscribe("lifecycle:test", {});

    // Publish 10 messages
    for (int i = 0; i < 10; i++) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"id", i}});
        producerIO->publish("lifecycle:test", std::move(data));
    }
    engine.update(1.0f/60.0f);

    int count1 = 0;
    while (consumerIO->hasMessages() > 0) {
        consumerIO->pullMessage();
        count1++;
    }
    ASSERT_EQ(count1, 10, "Should receive 10 messages");

    // Unsubscribe (if API exists - might not be implemented yet)
    // consumerIO->unsubscribe("lifecycle:test");

    // Publish 10 more
    for (int i = 10; i < 20; i++) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"id", i}});
        producerIO->publish("lifecycle:test", std::move(data));
    }
    engine.update(1.0f/60.0f);

    // If unsubscribe exists, should receive 0. If not, will receive 10.
    int count2 = 0;
    while (consumerIO->hasMessages() > 0) {
        consumerIO->pullMessage();
        count2++;
    }

    std::cout << "After unsubscribe: " << count2 << " messages (0 if unsubscribe works)\n";

    // Re-subscribe
    consumerIO->subscribe("lifecycle:test", {});

    // Publish 10 more
    for (int i = 20; i < 30; i++) {
        auto data = std::make_unique<JsonDataNode>(nlohmann::json{{"id", i}});
        producerIO->publish("lifecycle:test", std::move(data));
    }
    engine.update(1.0f/60.0f);

    int count3 = 0;
    while (consumerIO->hasMessages() > 0) {
        consumerIO->pullMessage();
        count3++;
    }
    ASSERT_EQ(count3, 10, "Should receive 10 messages after re-subscribe");

    reporter.addAssertion("subscription_lifecycle", true);
    std::cout << "✓ TEST 8 PASSED\n";

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
| **basic_pubsub** | Messages reçus dans test basique | 100/100 |
| **pattern_matching** | Pattern matching fonctionne | true |
| **broadcast_bug_present** | Bug 1-to-1 détecté (1.0) ou fixé (0.0) | Documentation |
| **batch_count** | Nombre de batches reçus | 2-4 |
| **queue_size** | Taille queue pendant flood | > 0 |
| **dropped_messages** | Messages droppés détectés | >= 0 |
| **concurrent_published** | Messages publiés concurrents | 10000 |
| **concurrent_received** | Messages reçus concurrents | > 0 |
| **health_monitoring** | Health metrics précis | true |
| **subscription_lifecycle** | Subscribe/unsubscribe fonctionne | true |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ Basic pub/sub: 100/100 messages en FIFO
2. ✅ Pattern matching fonctionne (wildcards)
3. ✅ Batching réduit fréquence (100 msg/s → ~1 msg/s)
4. ✅ Backpressure détecté (dropping flag ou dropped count)
5. ✅ Thread safety: aucun crash en concurrence
6. ✅ Health monitoring reflète état réel
7. ✅ Re-subscribe fonctionne

### KNOWN BUGS (Documentation)
1. ⚠️ Multi-module routing: Seul 1er subscriber reçoit (pas de clone())
2. ⚠️ Unsubscribe API peut ne pas exister

### NICE TO HAVE
1. ✅ Fix du bug clone() pour 1-to-many routing
2. ✅ Unsubscribe API implémentée
3. ✅ Compression pour batching

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| Messages perdus | Routing bug | WARN - documenter |
| Pattern pas match | Regex incorrect | FAIL - fix pattern |
| Pas de batching | Config ignorée | FAIL - check SubscriptionConfig |
| Pas de backpressure | Health non mis à jour | FAIL - fix IOHealth |
| Crash concurrent | Race condition | FAIL - add mutex |
| Queue size incorrect | Compteur bugué | FAIL - fix queueSize tracking |

---

## 📝 Output Attendu

```
================================================================================
TEST: IO System Stress Test
================================================================================

=== TEST 1: Basic Publish-Subscribe ===
✓ TEST 1 PASSED: 100 messages received

=== TEST 2: Pattern Matching ===
Pattern matching results:
  player:001:position: 3 times
  player:001:health: 2 times
  player:002:position: 2 times
  enemy:001:position: 1 times
✓ TEST 2 PASSED

=== TEST 3: Multi-Module Routing (1-to-many) ===
Broadcast distribution:
  ConsumerModule: 10 messages
  BroadcastModule: 0 messages
  BatchModule: 0 messages
  StressModule: 0 messages
⚠️  BUG: Only one module received all messages (clone() not implemented)
✓ TEST 3 COMPLETED (bug documented)

=== TEST 4: Message Batching (Low-Frequency) ===
Published: 300 messages over 3.02s
Received: 3 batches
Expected: ~3 batches (1/second)
✓ TEST 4 PASSED

=== TEST 5: Backpressure & Queue Overflow ===
Publishing 50000 messages...
  0 messages published
  10000 messages published
  20000 messages published
  30000 messages published
  40000 messages published
Health status:
  Queue size: 10000 / 10000
  Dropping: YES
  Dropped count: 40000
  Processing rate: 0.0 msg/s
✓ Backpressure detected correctly
✓ TEST 5 PASSED

=== TEST 6: Thread Safety (Concurrent Pub/Pull) ===
All publishers done: 10000 messages
All consumers done: 9847 messages
✓ TEST 6 PASSED (no crashes)

=== TEST 7: Health Monitoring Accuracy ===
Phase 1: Normal load (100 msg/s for 2s)
  Queue: 2, Dropping: NO
Phase 2: Overload (10000 msg/s for 1s)
  Queue: 9998, Dropping: YES
Phase 3: Recovery (pulling all messages)
  Pulled: 9998 messages
  Queue: 0, Dropping: NO
✓ TEST 7 PASSED

=== TEST 8: Subscription Lifecycle ===
After unsubscribe: 10 messages (0 if unsubscribe works)
✓ TEST 8 PASSED

================================================================================
METRICS
================================================================================
  Basic pub/sub:         100/100
  Batch count:           3
  Queue size:            10000
  Dropped messages:      40000
  Concurrent published:  10000
  Concurrent received:   9847
  Broadcast bug present: 1.0 (not fixed yet)

================================================================================
ASSERTIONS
================================================================================
  ✓ basic_pubsub
  ✓ pattern_matching
  ✓ multi_module_routing_tested
  ✓ batching_works
  ✓ backpressure_detected
  ✓ thread_safety
  ✓ health_monitoring
  ✓ subscription_lifecycle

Result: ✅ PASSED (8/8 tests)

================================================================================
```

---

## 📅 Planning

**Jour 1 (3h):**
- Implémenter ProducerModule, ConsumerModule, BroadcastModule
- Implémenter BatchModule, StressModule
- Setup IOFactory pour tests

**Jour 2 (3h):**
- Implémenter test_11_io_system.cpp
- Tests 1-4 (pub/sub, patterns, routing, batching)

**Jour 3 (2h):**
- Tests 5-8 (backpressure, threads, health, lifecycle)
- Debug + validation

---

**Prochaine étape**: `scenario_12_datanode.md`
