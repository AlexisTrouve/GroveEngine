/**
 * End-to-End Real World Benchmarks
 *
 * Realistic game scenarios to validate overall performance
 * Combines TopicTree routing, IntraIO messaging, and DataNode access
 */

#include "helpers/BenchmarkTimer.h"
#include "helpers/BenchmarkStats.h"
#include "helpers/BenchmarkReporter.h"

#include "grove/IOFactory.h"
#include "grove/IntraIOManager.h"
#include "grove/JsonDataNode.h"

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <memory>
#include <chrono>
#include <fstream>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

using namespace GroveEngine::Benchmark;
using namespace grove;

// Random number generation
static std::mt19937 rng(42);

// Helper to get memory usage (Linux only)
size_t getMemoryUsageMB() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            size_t kb = 0;
            sscanf(line.c_str(), "VmRSS: %zu", &kb);
            return kb / 1024; // Convert to MB
        }
    }
#endif
    return 0;
}

// Mock Module for simulation
// Adapted to current IIO API: subscribe() requires a handler callback,
// and pullMessage() is replaced by pullAndDispatch() (callback-driven dispatch).
class MockModule {
public:
    MockModule(const std::string& name, bool isPublisher)
        : name(name), isPublisher(isPublisher), msgCount(0) {
        io = IOFactory::create("intra", name);
    }

    void subscribe(const std::string& pattern) {
        if (!isPublisher) {
            // Current API requires a MessageHandler callback as 2nd argument.
            // We count received messages via atomic for benchmark metrics.
            io->subscribe(pattern, [this](const grove::Message& /*msg*/) {
                msgCount.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }

    void publish(const std::string& topic, int value) {
        if (isPublisher) {
            auto data = std::make_unique<JsonDataNode>("data", nlohmann::json{
                {"value", value},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
            });
            io->publish(topic, std::move(data));
        }
    }

    int pollMessages() {
        // pullAndDispatch() replaces the old pullMessage() API.
        // It pulls one message and invokes the registered handler callback.
        int count = 0;
        while (io->hasMessages() > 0) {
            io->pullAndDispatch();
            count++;
        }
        return count;
    }

private:
    std::string name;
    bool isPublisher;
    std::unique_ptr<IIO> io;
    std::atomic<int> msgCount;  // counts messages delivered to handler callback
};

// ============================================================================
// Benchmark M: Game Loop Simulation
// ============================================================================

void benchmarkM_game_loop() {
    BenchmarkReporter reporter;
    reporter.printHeader("M: Game Loop Simulation (Realistic Workload)");

    const int numGameLogicModules = 50;
    const int numAIModules = 30;
    const int numRenderModules = 20;
    const int messagesPerSec = 1000;
    const int durationSec = 5; // Reduced from 10 to 5 for faster execution
    const int totalMessages = messagesPerSec * durationSec;

    reporter.printMessage("Configuration:");
    reporter.printResult("  Game logic modules", static_cast<double>(numGameLogicModules), "");
    reporter.printResult("  AI modules", static_cast<double>(numAIModules), "");
    reporter.printResult("  Render modules", static_cast<double>(numRenderModules), "");
    reporter.printResult("  Message rate", static_cast<double>(messagesPerSec), "msg/s");
    reporter.printResult("  Duration", static_cast<double>(durationSec), "s");

    std::cout << "\n";

    // Create modules
    std::vector<std::unique_ptr<MockModule>> modules;

    // Game logic (publishers)
    for (int i = 0; i < numGameLogicModules; ++i) {
        modules.push_back(std::make_unique<MockModule>("game_logic_" + std::to_string(i), true));
    }

    // AI (subscribers)
    for (int i = 0; i < numAIModules; ++i) {
        auto module = std::make_unique<MockModule>("ai_" + std::to_string(i), false);
        module->subscribe("player:*");
        module->subscribe("ai:*");
        modules.push_back(std::move(module));
    }

    // Render (subscribers)
    for (int i = 0; i < numRenderModules; ++i) {
        auto module = std::make_unique<MockModule>("render_" + std::to_string(i), false);
        module->subscribe("render:*");
        module->subscribe("player:*");
        modules.push_back(std::move(module));
    }

    // Warm up
    for (int i = 0; i < 100; ++i) {
        modules[0]->publish("player:test:position", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Run simulation
    std::atomic<int> messagesSent{0};
    std::atomic<bool> running{true};

    BenchmarkTimer totalTimer;
    BenchmarkStats latencyStats;

    totalTimer.start();

    // Publisher thread
    std::thread publisherThread([&]() {
        std::uniform_int_distribution<> moduleDist(0, numGameLogicModules - 1);
        std::uniform_int_distribution<> topicDist(0, 3);

        std::vector<std::string> topics = {
            "player:123:position",
            "ai:enemy:target",
            "render:draw",
            "physics:collision"
        };

        auto startTime = std::chrono::steady_clock::now();
        int targetMessages = totalMessages;

        for (int i = 0; i < targetMessages && running.load(); ++i) {
            int moduleIdx = moduleDist(rng);
            int topicIdx = topicDist(rng);

            modules[moduleIdx]->publish(topics[topicIdx], i);
            messagesSent.fetch_add(1);

            // Rate limiting
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            auto expectedTime = std::chrono::microseconds((i + 1) * 1000000 / messagesPerSec);
            if (elapsed < expectedTime) {
                std::this_thread::sleep_for(expectedTime - elapsed);
            }
        }
    });

    // Let it run
    std::this_thread::sleep_for(std::chrono::seconds(durationSec));
    running.store(false);
    publisherThread.join();

    double totalTime = totalTimer.elapsedMs();

    // Poll remaining messages
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int totalReceived = 0;
    for (auto& module : modules) {
        totalReceived += module->pollMessages();
    }

    // Report
    double actualThroughput = (messagesSent.load() / totalTime) * 1000.0;

    reporter.printMessage("\nResults:\n");

    reporter.printResult("Messages sent", static_cast<double>(messagesSent.load()), "msgs");
    reporter.printResult("Total time", totalTime, "ms");
    reporter.printResult("Throughput", actualThroughput, "msg/s");
    reporter.printResult("Messages received", static_cast<double>(totalReceived), "msgs");

    reporter.printSubseparator();

    bool success = actualThroughput >= messagesPerSec * 0.9; // 90% of target
    if (success) {
        reporter.printSummary("Game loop simulation successful - Target throughput achieved");
    } else {
        reporter.printSummary("Throughput: " + std::to_string(static_cast<int>(actualThroughput)) + " msg/s");
    }
}

// ============================================================================
// Benchmark N: Hot-Reload Under Load
// ============================================================================

void benchmarkN_hotreload_under_load() {
    BenchmarkReporter reporter;
    reporter.printHeader("N: Hot-Reload Under Load");

    reporter.printMessage("Simulating hot-reload by creating/destroying IO instances under load\n");

    const int backgroundMessages = 100;
    const int numModules = 10;

    // Create background modules
    std::vector<std::unique_ptr<MockModule>> modules;
    for (int i = 0; i < numModules; ++i) {
        auto publisher = std::make_unique<MockModule>("bg_pub_" + std::to_string(i), true);
        auto subscriber = std::make_unique<MockModule>("bg_sub_" + std::to_string(i), false);
        subscriber->subscribe("test:*");
        modules.push_back(std::move(publisher));
        modules.push_back(std::move(subscriber));
    }

    // Start background load
    std::atomic<bool> running{true};
    std::thread backgroundThread([&]() {
        int counter = 0;
        while (running.load()) {
            modules[0]->publish("test:message", counter++);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Simulate hot-reload
    BenchmarkTimer reloadTimer;
    reloadTimer.start();

    // "Unload" module (set to nullptr)
    modules[0].reset();

    // Small delay (simulates reload time)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // "Reload" module
    modules[0] = std::make_unique<MockModule>("bg_pub_0_reloaded", true);

    double reloadTime = reloadTimer.elapsedMs();

    // Stop background
    running.store(false);
    backgroundThread.join();

    // Report
    reporter.printResult("Reload time", reloadTime, "ms");
    reporter.printResult("Target", 50.0, "ms");

    reporter.printSubseparator();

    if (reloadTime < 50.0) {
        reporter.printSummary("Hot-reload overhead acceptable (<50ms)");
    } else {
        reporter.printSummary("Reload time: " + std::to_string(reloadTime) + "ms");
    }
}

// ============================================================================
// Benchmark O: Memory Footprint
// ============================================================================

void benchmarkO_memory_footprint() {
    BenchmarkReporter reporter;
    reporter.printHeader("O: Memory Footprint Analysis");

    const int numTopics = 1000; // Reduced from 10000 for faster execution
    const int numSubscribers = 100; // Reduced from 1000

    reporter.printMessage("Configuration:");
    reporter.printResult("  Topics to create", static_cast<double>(numTopics), "");
    reporter.printResult("  Subscribers to create", static_cast<double>(numSubscribers), "");

    std::cout << "\n";

    size_t memBefore = getMemoryUsageMB();

    // Create topics via publishers
    std::vector<std::unique_ptr<MockModule>> publishers;
    for (int i = 0; i < numTopics; ++i) {
        auto pub = std::make_unique<MockModule>("topic_" + std::to_string(i), true);
        pub->publish("topic:" + std::to_string(i), i);
        if (i % 100 == 0) {
            publishers.push_back(std::move(pub)); // Keep some alive
        }
    }

    size_t memAfterTopics = getMemoryUsageMB();

    // Create subscribers
    std::vector<std::unique_ptr<MockModule>> subscribers;
    for (int i = 0; i < numSubscribers; ++i) {
        auto sub = std::make_unique<MockModule>("sub_" + std::to_string(i), false);
        sub->subscribe("topic:*");
        subscribers.push_back(std::move(sub));
    }

    size_t memAfterSubscribers = getMemoryUsageMB();

    // Report
    reporter.printResult("Memory before", static_cast<double>(memBefore), "MB");
    reporter.printResult("Memory after topics", static_cast<double>(memAfterTopics), "MB");
    reporter.printResult("Memory after subscribers", static_cast<double>(memAfterSubscribers), "MB");

    if (memBefore > 0) {
        double memPerTopic = ((memAfterTopics - memBefore) * 1024.0) / numTopics; // KB
        double memPerSubscriber = ((memAfterSubscribers - memAfterTopics) * 1024.0) / numSubscribers; // KB

        reporter.printResult("Memory per topic", memPerTopic, "KB");
        reporter.printResult("Memory per subscriber", memPerSubscriber, "KB");
    } else {
        reporter.printMessage("(Memory measurement not available on this platform)");
    }

    reporter.printSubseparator();
    reporter.printSummary("Memory footprint measured");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "          END-TO-END REAL WORLD BENCHMARKS\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";

    benchmarkM_game_loop();
    benchmarkN_hotreload_under_load();
    benchmarkO_memory_footprint();

    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "✅ ALL BENCHMARKS COMPLETE\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << std::endl;

    return 0;
}
