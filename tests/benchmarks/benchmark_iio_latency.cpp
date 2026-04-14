/**
 * IIO Message Latency Benchmark
 *
 * Measures the latency of inter-module communication via IIO topics
 * in both Sequential and Threaded module systems.
 */

#include "grove/ThreadedModuleSystem.h"
#include "grove/SequentialModuleSystem.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <iomanip>

using namespace grove;
using namespace std::chrono;

// Sender module: publishes timestamped messages
class SenderModule : public IModule {
private:
    std::string name;
    IIO* io = nullptr;
    std::atomic<int> messagesSent{0};

public:
    SenderModule(std::string n) : name(std::move(n)) {}

    void process(const IDataNode& input) override {
        if (!io) return;

        // Add some CPU work to avoid trivial workload timing issues
        // Simulate realistic module processing (e.g., game logic checks)
        volatile double dummy = 0.0;
        for (int i = 0; i < 1000; i++) {
            dummy += std::sqrt(i * 1.5);
        }

        // Send timestamped message
        auto now = high_resolution_clock::now();
        auto timestamp = duration_cast<nanoseconds>(now.time_since_epoch()).count();

        nlohmann::json msgData = {
            {"timestamp", timestamp},
            {"senderId", name},
            {"messageId", messagesSent.load()}
        };

        auto msg = std::make_unique<JsonDataNode>("message", msgData);
        io->publish("test/latency", std::move(msg));

        messagesSent++;
    }

    void setConfiguration(const IDataNode& configNode, IIO* ioLayer, ITaskScheduler* scheduler) override {
        io = ioLayer;
    }

    const IDataNode& getConfiguration() override {
        static JsonDataNode emptyConfig("config", nlohmann::json{});
        return emptyConfig;
    }

    std::unique_ptr<IDataNode> getHealthStatus() override {
        return std::make_unique<JsonDataNode>("health", nlohmann::json{{"status", "healthy"}});
    }

    void shutdown() override {}

    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", nlohmann::json{});
    }

    void setState(const IDataNode& state) override {}

    std::string getType() const override { return "SenderModule"; }
    bool isIdle() const override { return true; }

    int getMessagesSent() const { return messagesSent.load(); }
};

// Receiver module: measures latency of received messages
class ReceiverModule : public IModule {
private:
    std::string name;
    IIO* io = nullptr;
    std::vector<int64_t> latencies; // nanoseconds
    std::atomic<int> messagesReceived{0};
    mutable std::mutex latenciesMutex;

public:
    ReceiverModule(std::string n) : name(std::move(n)) {
        latencies.reserve(10000);
    }

    void process(const IDataNode& input) override {
        // Messages are received via callback in setConfiguration
    }

    void setConfiguration(const IDataNode& configNode, IIO* ioLayer, ITaskScheduler* scheduler) override {
        io = ioLayer;

        // Subscribe to latency test topic with callback
        io->subscribe("test/latency", [this](const Message& msg) {
            auto receiveTime = high_resolution_clock::now();
            auto receiveTimestamp = duration_cast<nanoseconds>(receiveTime.time_since_epoch()).count();

            try {
                auto* jsonMsg = dynamic_cast<const JsonDataNode*>(msg.data.get());
                if (!jsonMsg) return;

                const auto& data = jsonMsg->getJsonData();
                int64_t sendTimestamp = data["timestamp"];

                int64_t latencyNs = receiveTimestamp - sendTimestamp;

                {
                    std::lock_guard<std::mutex> lock(latenciesMutex);
                    latencies.push_back(latencyNs);
                }

                messagesReceived++;
            } catch (...) {
                // Ignore malformed messages
            }
        });
    }

    const IDataNode& getConfiguration() override {
        static JsonDataNode emptyConfig("config", nlohmann::json{});
        return emptyConfig;
    }

    std::unique_ptr<IDataNode> getHealthStatus() override {
        return std::make_unique<JsonDataNode>("health", nlohmann::json{{"status", "healthy"}});
    }

    void shutdown() override {}

    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", nlohmann::json{});
    }

    void setState(const IDataNode& state) override {}

    std::string getType() const override { return "ReceiverModule"; }
    bool isIdle() const override { return true; }

    int getMessagesReceived() const { return messagesReceived.load(); }

    std::vector<int64_t> getLatencies() const {
        std::lock_guard<std::mutex> lock(latenciesMutex);
        return latencies;
    }
};

struct LatencyStats {
    double minUs;
    double maxUs;
    double avgUs;
    double medianUs;
    double p95Us;
    double p99Us;
    int totalMessages;
};

LatencyStats calculateStats(const std::vector<int64_t>& latencies) {
    if (latencies.empty()) {
        return {0, 0, 0, 0, 0, 0, 0};
    }

    std::vector<int64_t> sorted = latencies;
    std::sort(sorted.begin(), sorted.end());

    int64_t sum = 0;
    for (auto lat : sorted) {
        sum += lat;
    }

    LatencyStats stats;
    stats.totalMessages = sorted.size();
    stats.minUs = sorted.front() / 1000.0;
    stats.maxUs = sorted.back() / 1000.0;
    stats.avgUs = (sum / sorted.size()) / 1000.0;

    size_t medianIdx = sorted.size() / 2;
    stats.medianUs = sorted[medianIdx] / 1000.0;

    size_t p95Idx = (sorted.size() * 95) / 100;
    stats.p95Us = sorted[p95Idx] / 1000.0;

    size_t p99Idx = (sorted.size() * 99) / 100;
    stats.p99Us = sorted[p99Idx] / 1000.0;

    return stats;
}

void printStats(const std::string& systemName, const LatencyStats& stats) {
    std::cout << "\n=== " << systemName << " ===\n";
    std::cout << "  Messages:    " << stats.totalMessages << "\n";
    std::cout << "  Min:         " << std::fixed << std::setprecision(2) << stats.minUs << " μs\n";
    std::cout << "  Avg:         " << std::fixed << std::setprecision(2) << stats.avgUs << " μs\n";
    std::cout << "  Median:      " << std::fixed << std::setprecision(2) << stats.medianUs << " μs\n";
    std::cout << "  P95:         " << std::fixed << std::setprecision(2) << stats.p95Us << " μs\n";
    std::cout << "  P99:         " << std::fixed << std::setprecision(2) << stats.p99Us << " μs\n";
    std::cout << "  Max:         " << std::fixed << std::setprecision(2) << stats.maxUs << " μs\n";
}

template<typename SystemType>
LatencyStats runLatencyBenchmark(const std::string& systemName, int numFrames) {
    auto system = std::make_unique<SystemType>();

    // Create sender and receiver modules
    auto sender = std::make_unique<SenderModule>("Sender");
    auto receiver = std::make_unique<ReceiverModule>("Receiver");

    auto* receiverPtr = receiver.get();

    system->registerModule("Sender", std::move(sender));
    system->registerModule("Receiver", std::move(receiver));

    // Warmup (10 frames)
    for (int i = 0; i < 10; i++) {
        system->processModules(1.0f / 60.0f);
    }

    // Benchmark
    for (int frame = 0; frame < numFrames; frame++) {
        system->processModules(1.0f / 60.0f);
    }

    // Get results
    auto latencies = receiverPtr->getLatencies();
    return calculateStats(latencies);
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "IIO MESSAGE LATENCY BENCHMARK\n";
    std::cout << "================================================================================\n";
    std::cout << "Measuring inter-module communication latency via IIO topics\n\n";

    // Disable verbose logging
    spdlog::set_level(spdlog::level::off);

    const int NUM_FRAMES = 1000;

    std::cout << "Test configuration:\n";
    std::cout << "  Frames:      " << NUM_FRAMES << "\n";
    std::cout << "  Modules:     2 (Sender → Receiver)\n";
    std::cout << "  Topic:       test/latency\n\n";

    // Test 1: Sequential (baseline)
    std::cout << "Running SequentialModuleSystem...\n";
    auto seqStats = runLatencyBenchmark<SequentialModuleSystem>("Sequential", NUM_FRAMES);
    printStats("SequentialModuleSystem", seqStats);

    // Test 2: Threaded (parallel)
    std::cout << "\nRunning ThreadedModuleSystem...\n";
    auto threadedStats = runLatencyBenchmark<ThreadedModuleSystem>("Threaded", NUM_FRAMES);
    printStats("ThreadedModuleSystem", threadedStats);

    // Comparison
    std::cout << "\n================================================================================\n";
    std::cout << "COMPARISON\n";
    std::cout << "================================================================================\n";

    double avgOverhead = threadedStats.avgUs - seqStats.avgUs;
    double medianOverhead = threadedStats.medianUs - seqStats.medianUs;
    double p95Overhead = threadedStats.p95Us - seqStats.p95Us;

    std::cout << "  Latency Overhead (Threaded vs Sequential):\n";
    std::cout << "    Avg:       " << std::showpos << std::fixed << std::setprecision(2)
              << avgOverhead << " μs\n";
    std::cout << "    Median:    " << std::showpos << std::fixed << std::setprecision(2)
              << medianOverhead << " μs\n";
    std::cout << "    P95:       " << std::showpos << std::fixed << std::setprecision(2)
              << p95Overhead << " μs\n";

    std::cout << "\n  Interpretation:\n";
    if (avgOverhead < 10.0) {
        std::cout << "    ✅ EXCELLENT: Very low latency overhead (<10μs average)\n";
        std::cout << "    → Threaded system adds minimal communication delay\n";
    } else if (avgOverhead < 50.0) {
        std::cout << "    ✅ GOOD: Acceptable latency overhead (<50μs average)\n";
        std::cout << "    → Suitable for real-time game engines (60 FPS = 16.67ms budget)\n";
    } else if (avgOverhead < 100.0) {
        std::cout << "    ⚠️  MODERATE: Noticeable latency overhead (<100μs average)\n";
        std::cout << "    → May impact highly latency-sensitive systems\n";
    } else {
        std::cout << "    ⚠️  HIGH: Significant latency overhead (>100μs average)\n";
        std::cout << "    → Consider batching or optimizing IIO for threaded workloads\n";
    }

    std::cout << "\n  Context:\n";
    std::cout << "    - 60 FPS frame budget:  16,667 μs (16.67 ms)\n";
    std::cout << "    - 144 FPS frame budget:  6,944 μs (6.94 ms)\n";
    std::cout << "    - Typical overhead:      " << std::noshowpos << std::fixed << std::setprecision(1)
              << (avgOverhead / 16667.0) * 100.0 << "% of 60 FPS budget\n";

    std::cout << "================================================================================\n";
    std::cout << "🎉 BENCHMARK COMPLETE\n";
    std::cout << "================================================================================\n";

    return 0;
}
