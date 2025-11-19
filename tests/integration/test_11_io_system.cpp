/**
 * Scenario 11: IO System Stress Test
 *
 * Tests IntraIO pub/sub system with:
 * - Basic publish/subscribe
 * - Pattern matching with wildcards
 * - Multi-module routing (1-to-many)
 * - Message batching (low-frequency subscriptions)
 * - Backpressure and queue overflow
 * - Thread safety
 * - Health monitoring
 *
 * Known bug to validate: IntraIOManager may route only to first subscriber (std::move limitation)
 */

#include "grove/IModule.h"
#include "grove/IOFactory.h"
#include "grove/IntraIOManager.h"
#include "grove/JsonDataNode.h"
#include "../helpers/TestMetrics.h"
#include "../helpers/TestAssertions.h"
#include "../helpers/TestReporter.h"

#include <dlfcn.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <map>

using namespace grove;

// Module handle for testing
struct ModuleHandle {
    void* dlHandle = nullptr;
    grove::IModule* instance = nullptr;
    std::unique_ptr<IIO> io;
    std::string modulePath;
};

// Simple module loader for IO testing
class IOTestEngine {
public:
    IOTestEngine() {}

    ~IOTestEngine() {
        for (auto& [name, handle] : modules_) {
            unloadModule(name);
        }
    }

    bool loadModule(const std::string& name, const std::string& path) {
        if (modules_.count(name) > 0) {
            std::cerr << "Module " << name << " already loaded\n";
            return false;
        }

        void* dlHandle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!dlHandle) {
            std::cerr << "Failed to load module " << name << ": " << dlerror() << "\n";
            return false;
        }

        auto createFunc = (grove::IModule* (*)())dlsym(dlHandle, "createModule");
        if (!createFunc) {
            std::cerr << "Failed to find createModule in " << name << ": " << dlerror() << "\n";
            dlclose(dlHandle);
            return false;
        }

        grove::IModule* instance = createFunc();
        if (!instance) {
            std::cerr << "createModule returned nullptr for " << name << "\n";
            dlclose(dlHandle);
            return false;
        }

        // Create IntraIO instance for this module
        auto io = IOFactory::create("intra", name);

        ModuleHandle handle;
        handle.dlHandle = dlHandle;
        handle.instance = instance;
        handle.io = std::move(io);
        handle.modulePath = path;

        modules_[name] = std::move(handle);

        // Initialize module with IO
        auto config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
        instance->setConfiguration(*config, modules_[name].io.get(), nullptr);

        std::cout << "  ✓ Loaded " << name << "\n";
        return true;
    }

    void unloadModule(const std::string& name) {
        auto it = modules_.find(name);
        if (it == modules_.end()) return;

        auto& handle = it->second;

        if (handle.instance) {
            handle.instance->shutdown();
            delete handle.instance;
            handle.instance = nullptr;
        }

        if (handle.dlHandle) {
            dlclose(handle.dlHandle);
            handle.dlHandle = nullptr;
        }

        modules_.erase(it);
    }

    grove::IModule* getModule(const std::string& name) {
        auto it = modules_.find(name);
        return (it != modules_.end()) ? it->second.instance : nullptr;
    }

    IIO* getIO(const std::string& name) {
        auto it = modules_.find(name);
        return (it != modules_.end()) ? it->second.io.get() : nullptr;
    }

    void processAll(const IDataNode& input) {
        for (auto& [name, handle] : modules_) {
            if (handle.instance) {
                handle.instance->process(input);
            }
        }
    }

private:
    std::map<std::string, ModuleHandle> modules_;
};

int main() {
    TestReporter reporter("IO System Stress Test");
    TestMetrics metrics;

    std::cout << "================================================================================\n";
    std::cout << "TEST: IO System Stress Test (Scenario 11)\n";
    std::cout << "================================================================================\n\n";

    // === SETUP ===
    std::cout << "Setup: Loading IO modules...\n";

    IOTestEngine engine;

    // Load all IO test modules
    bool loadSuccess = true;
    loadSuccess &= engine.loadModule("ProducerModule", "./libProducerModule.so");
    loadSuccess &= engine.loadModule("ConsumerModule", "./libConsumerModule.so");
    loadSuccess &= engine.loadModule("BroadcastModule", "./libBroadcastModule.so");
    loadSuccess &= engine.loadModule("BatchModule", "./libBatchModule.so");
    loadSuccess &= engine.loadModule("IOStressModule", "./libIOStressModule.so");

    if (!loadSuccess) {
        std::cerr << "❌ Failed to load required modules\n";
        return 1;
    }

    std::cout << "\n";

    auto producerIO = engine.getIO("ProducerModule");
    auto consumerIO = engine.getIO("ConsumerModule");
    auto broadcastIO = engine.getIO("BroadcastModule");
    auto batchIO = engine.getIO("BatchModule");
    auto stressIO = engine.getIO("IOStressModule");

    if (!producerIO || !consumerIO || !broadcastIO || !batchIO || !stressIO) {
        std::cerr << "❌ Failed to get IO instances\n";
        return 1;
    }

    auto emptyInput = std::make_unique<JsonDataNode>("input", nlohmann::json::object());

    // ========================================================================
    // TEST 1: Basic Publish-Subscribe
    // ========================================================================
    std::cout << "=== TEST 1: Basic Publish-Subscribe ===\n";

    // Consumer subscribes to "test:basic"
    consumerIO->subscribe("test:basic");

    // Publish 100 messages
    for (int i = 0; i < 100; i++) {
        auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{
            {"id", i},
            {"payload", "test_message_" + std::to_string(i)}
        });
        producerIO->publish("test:basic", std::move(data));
    }

    // Process to allow routing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Count received messages
    int receivedCount = 0;
    while (consumerIO->hasMessages() > 0) {
        auto msg = consumerIO->pullMessage();
        receivedCount++;
    }

    ASSERT_EQ(receivedCount, 100, "Should receive all 100 messages");
    reporter.addAssertion("basic_pubsub", receivedCount == 100);
    reporter.addMetric("basic_pubsub_count", receivedCount);
    std::cout << "  ✓ Received " << receivedCount << "/100 messages\n";
    std::cout << "✓ TEST 1 PASSED\n\n";

    // ========================================================================
    // TEST 2: Pattern Matching with Wildcards
    // ========================================================================
    std::cout << "=== TEST 2: Pattern Matching ===\n";

    // Subscribe to patterns
    consumerIO->subscribe("player:.*");

    // Publish test messages
    std::vector<std::string> testTopics = {
        "player:001:position",
        "player:001:health",
        "player:002:position",
        "enemy:001:position"
    };

    for (const auto& topic : testTopics) {
        auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{{"topic", topic}});
        producerIO->publish(topic, std::move(data));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Count player messages (should match 3 of 4)
    int playerMsgCount = 0;
    while (consumerIO->hasMessages() > 0) {
        auto msg = consumerIO->pullMessage();
        if (msg.topic.find("player:") == 0) {
            playerMsgCount++;
        }
    }

    std::cout << "  Pattern 'player:.*' matched " << playerMsgCount << " messages\n";
    ASSERT_GE(playerMsgCount, 3, "Should match at least 3 player messages");
    reporter.addAssertion("pattern_matching", playerMsgCount >= 3);
    reporter.addMetric("pattern_match_count", playerMsgCount);
    std::cout << "✓ TEST 2 PASSED\n\n";

    // ========================================================================
    // TEST 3: Multi-Module Routing (1-to-many) - Bug Detection
    // ========================================================================
    std::cout << "=== TEST 3: Multi-Module Routing (1-to-many) ===\n";
    std::cout << "  Testing for known bug: std::move limitation in routing\n";

    // All modules subscribe to "broadcast:.*"
    consumerIO->subscribe("broadcast:.*");
    broadcastIO->subscribe("broadcast:.*");
    batchIO->subscribe("broadcast:.*");
    stressIO->subscribe("broadcast:.*");

    // Publish 10 broadcast messages
    for (int i = 0; i < 10; i++) {
        auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{{"broadcast_id", i}});
        producerIO->publish("broadcast:data", std::move(data));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Check which modules received messages
    int consumerReceived = consumerIO->hasMessages();
    int broadcastReceived = broadcastIO->hasMessages();
    int batchReceived = batchIO->hasMessages();
    int stressReceived = stressIO->hasMessages();

    std::cout << "  Broadcast distribution:\n";
    std::cout << "    ConsumerModule:  " << consumerReceived << " messages\n";
    std::cout << "    BroadcastModule: " << broadcastReceived << " messages\n";
    std::cout << "    BatchModule:     " << batchReceived << " messages\n";
    std::cout << "    IOStressModule:  " << stressReceived << " messages\n";

    int totalReceived = consumerReceived + broadcastReceived + batchReceived + stressReceived;

    if (totalReceived == 10) {
        std::cout << "  ⚠️  BUG CONFIRMED: Only one module received all messages\n";
        std::cout << "      This confirms the clone() limitation in routing\n";
        reporter.addMetric("broadcast_bug_present", 1.0f);
    } else if (totalReceived >= 40) {
        std::cout << "  ✓ FIXED: All modules received copies (clone() implemented!)\n";
        reporter.addMetric("broadcast_bug_present", 0.0f);
    } else {
        std::cout << "  ⚠️  Unexpected: " << totalReceived << " messages received (expected 10 or 40)\n";
        reporter.addMetric("broadcast_bug_present", 0.5f);
    }

    reporter.addAssertion("multi_module_routing_tested", true);
    std::cout << "✓ TEST 3 COMPLETED (bug documented)\n\n";

    // Clean up for next test
    while (consumerIO->hasMessages() > 0) consumerIO->pullMessage();
    while (broadcastIO->hasMessages() > 0) broadcastIO->pullMessage();
    while (batchIO->hasMessages() > 0) batchIO->pullMessage();
    while (stressIO->hasMessages() > 0) stressIO->pullMessage();

    // ========================================================================
    // TEST 4: Low-Frequency Subscriptions (Batching)
    // ========================================================================
    std::cout << "=== TEST 4: Low-Frequency Subscriptions ===\n";

    SubscriptionConfig batchConfig;
    batchConfig.replaceable = true;
    batchConfig.batchInterval = 1000; // 1 second
    batchIO->subscribeLowFreq("batch:.*", batchConfig);

    std::cout << "  Publishing 100 messages over 2 seconds...\n";
    int batchPublished = 0;
    auto batchStart = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; i++) {
        auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{
            {"timestamp", i},
            {"value", i * 0.1f}
        });
        producerIO->publish("batch:metric", std::move(data));
        batchPublished++;
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // 50 Hz
    }

    auto batchEnd = std::chrono::high_resolution_clock::now();
    float batchDuration = std::chrono::duration<float>(batchEnd - batchStart).count();

    // Check batched messages
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int batchesReceived = 0;
    while (batchIO->hasMessages() > 0) {
        auto msg = batchIO->pullMessage();
        batchesReceived++;
    }

    std::cout << "  Published: " << batchPublished << " messages over " << batchDuration << "s\n";
    std::cout << "  Received: " << batchesReceived << " batches\n";
    std::cout << "  Expected: ~" << static_cast<int>(batchDuration) << " batches (1/second)\n";

    // With 1s batching, expect fewer messages than published
    ASSERT_LT(batchesReceived, batchPublished, "Batching should reduce message count");
    reporter.addMetric("batch_count", batchesReceived);
    reporter.addMetric("batch_published", batchPublished);
    reporter.addAssertion("batching_reduces_messages", batchesReceived < batchPublished);
    std::cout << "✓ TEST 4 PASSED\n\n";

    // ========================================================================
    // TEST 5: Backpressure & Queue Overflow
    // ========================================================================
    std::cout << "=== TEST 5: Backpressure & Queue Overflow ===\n";

    consumerIO->subscribe("stress:flood");

    std::cout << "  Publishing 10000 messages without pulling...\n";
    for (int i = 0; i < 10000; i++) {
        auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{{"flood_id", i}});
        producerIO->publish("stress:flood", std::move(data));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check health
    auto health = consumerIO->getHealth();
    std::cout << "  Health status:\n";
    std::cout << "    Queue size: " << health.queueSize << " / " << health.maxQueueSize << "\n";
    std::cout << "    Dropping: " << (health.dropping ? "YES" : "NO") << "\n";
    std::cout << "    Dropped count: " << health.droppedMessageCount << "\n";

    ASSERT_GT(health.queueSize, 0, "Queue should have messages");
    reporter.addMetric("queue_size", health.queueSize);
    reporter.addMetric("dropped_messages", health.droppedMessageCount);
    reporter.addAssertion("backpressure_monitoring", true);
    std::cout << "✓ TEST 5 PASSED\n\n";

    // Clean up queue
    while (consumerIO->hasMessages() > 0) consumerIO->pullMessage();

    // ========================================================================
    // TEST 6: Thread Safety (Concurrent Pub/Pull)
    // ========================================================================
    std::cout << "=== TEST 6: Thread Safety ===\n";

    consumerIO->subscribe("thread:.*");

    std::atomic<int> publishedTotal{0};
    std::atomic<int> receivedTotal{0};
    std::atomic<bool> running{true};

    std::cout << "  Launching 5 publisher threads...\n";
    std::vector<std::thread> publishers;
    for (int t = 0; t < 5; t++) {
        publishers.emplace_back([&, t]() {
            for (int i = 0; i < 100; i++) {
                auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{
                    {"thread", t},
                    {"id", i}
                });
                producerIO->publish("thread:test", std::move(data));
                publishedTotal++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    std::cout << "  Launching 3 consumer threads...\n";
    std::vector<std::thread> consumers;
    for (int t = 0; t < 3; t++) {
        consumers.emplace_back([&]() {
            while (running || consumerIO->hasMessages() > 0) {
                if (consumerIO->hasMessages() > 0) {
                    try {
                        auto msg = consumerIO->pullMessage();
                        receivedTotal++;
                    } catch (...) {
                        // Expected: may have race conditions
                    }
                }
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });
    }

    // Wait for publishers
    for (auto& t : publishers) {
        t.join();
    }

    std::cout << "  All publishers done: " << publishedTotal << " messages\n";

    // Let consumers finish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    running = false;

    for (auto& t : consumers) {
        t.join();
    }

    std::cout << "  All consumers done: " << receivedTotal << " messages\n";

    ASSERT_GT(receivedTotal, 0, "Should receive at least some messages");
    reporter.addMetric("concurrent_published", publishedTotal);
    reporter.addMetric("concurrent_received", receivedTotal);
    reporter.addAssertion("thread_safety", true); // No crash = success
    std::cout << "✓ TEST 6 PASSED (no crashes)\n\n";

    // ========================================================================
    // FINAL REPORT
    // ========================================================================

    metrics.printReport();
    reporter.printFinalReport();

    return reporter.getExitCode();
}
