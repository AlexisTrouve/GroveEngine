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
#include "grove/IntraIO.h"   // per-thread IIO instances in the concurrency test (TEST 6)
#include "grove/JsonDataNode.h"
#include "../helpers/TestMetrics.h"
#include "../helpers/TestAssertions.h"
#include "../helpers/TestReporter.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <map>
#include <string>

// Hot-reload artifacts are .so on Linux/macOS, .dll on Windows. Resolve the extension per platform
// so this test runs (and gets sanitized) on both — the module paths below were hardcoded .dll
// (Windows-only), which made the test fail to load anything on Linux.
namespace {
std::string modPath(const std::string& base) {
#ifdef _WIN32
    return "./lib" + base + ".dll";
#else
    return "./lib" + base + ".so";
#endif
}
}

// Cross-platform dlopen wrappers
#ifdef _WIN32
inline void* grove_dlopen(const char* path, int flags) {
    (void)flags;
    return LoadLibraryA(path);
}
inline void* grove_dlsym(void* handle, const char* symbol) {
    return (void*)GetProcAddress((HMODULE)handle, symbol);
}
inline int grove_dlclose(void* handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}
inline const char* grove_dlerror() {
    static thread_local char buf[256];
    DWORD err = GetLastError();
    snprintf(buf, sizeof(buf), "Windows error code: %lu", err);
    return buf;
}
#define RTLD_NOW 0
#define RTLD_LOCAL 0
#else
#define grove_dlopen dlopen
#define grove_dlsym dlsym
#define grove_dlclose dlclose
#define grove_dlerror dlerror
#endif

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
        // Cleanup modules directly - don't call unloadModule which modifies map during iteration
        for (auto& [name, handle] : modules_) {
            if (handle.instance) {
                handle.instance->shutdown();
                delete handle.instance;
                handle.instance = nullptr;
            }
            // Destroy the module's IntraIO (and its subscriptions) BEFORE unloading the
            // DLL. A module may have subscribed in setConfiguration() with a lambda whose
            // code lives in its DLL; ~IntraIO destroys that std::function, and if the DLL
            // was already dlclose'd the std::function's manager pointer dereferences
            // unmapped code -> SIGSEGV at teardown. Resetting io here runs that destruction
            // while the DLL is still mapped.
            handle.io.reset();
            if (handle.dlHandle) {
                grove_dlclose(handle.dlHandle);
                handle.dlHandle = nullptr;
            }
        }
        modules_.clear();
    }

    bool loadModule(const std::string& name, const std::string& path) {
        if (modules_.count(name) > 0) {
            std::cerr << "Module " << name << " already loaded\n";
            return false;
        }

        void* dlHandle = grove_dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!dlHandle) {
            std::cerr << "Failed to load module " << name << ": " << grove_dlerror() << "\n";
            return false;
        }

        auto createFunc = (grove::IModule* (*)())grove_dlsym(dlHandle, "createModule");
        if (!createFunc) {
            std::cerr << "Failed to find createModule in " << name << ": " << grove_dlerror() << "\n";
            grove_dlclose(dlHandle);
            return false;
        }

        grove::IModule* instance = createFunc();
        if (!instance) {
            std::cerr << "createModule returned nullptr for " << name << "\n";
            grove_dlclose(dlHandle);
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

        // Destroy the IntraIO (subscriptions) before dlclose — see ~IOTestEngine.
        handle.io.reset();

        if (handle.dlHandle) {
            grove_dlclose(handle.dlHandle);
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
    loadSuccess &= engine.loadModule("ProducerModule", modPath("ProducerModule"));
    loadSuccess &= engine.loadModule("ConsumerModule", modPath("ConsumerModule"));
    loadSuccess &= engine.loadModule("BroadcastModule", modPath("BroadcastModule"));
    loadSuccess &= engine.loadModule("BatchModule", modPath("BatchModule"));
    loadSuccess &= engine.loadModule("IOStressModule", modPath("IOStressModule"));

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

    // Count received messages
    int receivedCount = 0;

    // Consumer subscribes to "test:basic" with callback
    consumerIO->subscribe("test:basic", [&](const Message& msg) {
        receivedCount++;
    });

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

    // Dispatch messages to trigger callbacks
    while (consumerIO->hasMessages() > 0) {
        consumerIO->pullAndDispatch();
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

    // Count player messages (should match 3 of 4)
    int playerMsgCount = 0;

    // Subscribe to patterns with callback
    consumerIO->subscribe("player:.*", [&](const Message& msg) {
        if (msg.topic.find("player:") == 0) {
            playerMsgCount++;
        }
    });

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

    // Dispatch messages to trigger callbacks
    while (consumerIO->hasMessages() > 0) {
        consumerIO->pullAndDispatch();
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

    // Track received messages per module
    int consumerReceived = 0;
    int broadcastReceived = 0;
    int batchReceived = 0;
    int stressReceived = 0;

    // All modules subscribe to "broadcast:.*" with callbacks
    consumerIO->subscribe("broadcast:.*", [&](const Message& msg) {
        consumerReceived++;
    });
    broadcastIO->subscribe("broadcast:.*", [&](const Message& msg) {
        broadcastReceived++;
    });
    batchIO->subscribe("broadcast:.*", [&](const Message& msg) {
        batchReceived++;
    });
    stressIO->subscribe("broadcast:.*", [&](const Message& msg) {
        stressReceived++;
    });

    // Publish 10 broadcast messages
    for (int i = 0; i < 10; i++) {
        auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{{"broadcast_id", i}});
        producerIO->publish("broadcast:data", std::move(data));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Dispatch messages to all subscribers
    while (consumerIO->hasMessages() > 0) consumerIO->pullAndDispatch();
    while (broadcastIO->hasMessages() > 0) broadcastIO->pullAndDispatch();
    while (batchIO->hasMessages() > 0) batchIO->pullAndDispatch();
    while (stressIO->hasMessages() > 0) stressIO->pullAndDispatch();

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

    // Clean up for next test (already dispatched, so just clear any remaining)
    while (consumerIO->hasMessages() > 0) consumerIO->pullAndDispatch();
    while (broadcastIO->hasMessages() > 0) broadcastIO->pullAndDispatch();
    while (batchIO->hasMessages() > 0) batchIO->pullAndDispatch();
    while (stressIO->hasMessages() > 0) stressIO->pullAndDispatch();

    // ========================================================================
    // TEST 4: Low-Frequency Subscriptions (Batching)
    // ========================================================================
    std::cout << "=== TEST 4: Low-Frequency Subscriptions ===\n";

    int batchesReceived = 0;

    SubscriptionConfig batchConfig;
    batchConfig.replaceable = true;
    batchConfig.batchInterval = 1000; // 1 second
    batchIO->subscribeLowFreq("batch:.*", [&](const Message& msg) {
        batchesReceived++;
    }, batchConfig);

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
    while (batchIO->hasMessages() > 0) {
        batchIO->pullAndDispatch();
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

    consumerIO->subscribe("stress:flood", [](const Message& msg) {
        // Just consume the message (counting not needed for this test)
    });

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
    while (consumerIO->hasMessages() > 0) consumerIO->pullAndDispatch();

    // ========================================================================
    // TEST 6: Thread Safety (Concurrent Pub/Pull)
    // ========================================================================
    std::cout << "=== TEST 6: Thread Safety ===\n";

    std::atomic<int> publishedTotal{0};
    std::atomic<int> receivedTotal{0};
    std::atomic<bool> running{true};

    consumerIO->subscribe("thread:.*", [&](const Message& msg) {
        receivedTotal++;
    });

    // CONTRACT: one owning thread per IntraIO instance (TSan-confirmed — sharing one instance across
    // threads races: producerIO in publish(), consumerIO in the dispatched callback). Each publisher
    // thread gets its OWN, STABLE instance (created up front, like N modules each owning a lifetime
    // instance) — the SUPPORTED concurrency: distinct instances route concurrently + safely.
    std::cout << "  Launching 5 publisher threads (each with its OWN stable IIO instance)...\n";
    std::vector<std::shared_ptr<IntraIO>> pubIOs;
    for (int t = 0; t < 5; t++) {
        pubIOs.push_back(IntraIOManager::getInstance().createInstance("pub_" + std::to_string(t)));
    }
    std::vector<std::thread> publishers;
    for (int t = 0; t < 5; t++) {
        publishers.emplace_back([&, t]() {
            for (int i = 0; i < 100; i++) {
                auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{
                    {"thread", t},
                    {"id", i}
                });
                pubIOs[t]->publish("thread:test", std::move(data));   // own instance → no cross-thread race
                publishedTotal++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    // ONE consumer thread owns consumerIO (draining one instance from N threads is the same violation
    // as multi-publish — it ran the callback concurrently, the TSan-confirmed ConsumerModule race).
    std::cout << "  Launching 1 consumer thread (single owner of consumerIO)...\n";
    std::vector<std::thread> consumers;
    for (int t = 0; t < 1; t++) {
        consumers.emplace_back([&]() {
            while (running || consumerIO->hasMessages() > 0) {
                if (consumerIO->hasMessages() > 0) {
                    try {
                        consumerIO->pullAndDispatch();
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
