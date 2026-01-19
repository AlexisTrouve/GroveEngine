/**
 * REAL CPU-Bound Performance Comparison: ThreadedModuleSystem vs SequentialModuleSystem
 *
 * Uses actual CPU-intensive work (NOT sleep) to measure true parallelization gains.
 */

#include "grove/ThreadedModuleSystem.h"
#include "grove/SequentialModuleSystem.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <chrono>
#include <atomic>
#include <thread>
#include <cmath>
#include <iomanip>
#include <type_traits>

using namespace grove;
using namespace std::chrono;

// CPU-intensive workload module
class CPUWorkloadModule : public IModule {
private:
    std::string name;
    IIO* io = nullptr;
    std::atomic<int> processCount{0};
    int workloadIterations; // Amount of CPU work (iterations)

public:
    CPUWorkloadModule(std::string n, int iterations)
        : name(std::move(n)), workloadIterations(iterations) {
    }

    void process(const IDataNode& input) override {
        processCount++;

        // REAL CPU-INTENSIVE WORK (NOT sleep!)
        // Simulate game logic: pathfinding, physics calculations, AI decision trees
        volatile double result = 0.0;
        for (int i = 0; i < workloadIterations; i++) {
            result += std::sqrt(i * 3.14159265359);
            result += std::sin(i * 0.001);
            result += std::cos(i * 0.001);
            result *= 1.000001; // Prevent compiler optimization
        }
    }

    void setConfiguration(const IDataNode& configNode, IIO* ioLayer, ITaskScheduler* scheduler) override {
        io = ioLayer;
    }

    const IDataNode& getConfiguration() override {
        static JsonDataNode emptyConfig("config", nlohmann::json{});
        return emptyConfig;
    }

    std::unique_ptr<IDataNode> getHealthStatus() override {
        nlohmann::json health = {
            {"status", "healthy"},
            {"processCount", processCount.load()}
        };
        return std::make_unique<JsonDataNode>("health", health);
    }

    void shutdown() override {}

    std::unique_ptr<IDataNode> getState() override {
        nlohmann::json state = {
            {"processCount", processCount.load()}
        };
        return std::make_unique<JsonDataNode>("state", state);
    }

    void setState(const IDataNode& state) override {
        processCount = state.getInt("processCount", 0);
    }

    std::string getType() const override { return "CPUWorkloadModule"; }
    bool isIdle() const override { return true; }

    int getProcessCount() const { return processCount.load(); }
};

struct BenchmarkResult {
    std::string systemName;
    int numModules;
    int numFrames;
    double totalTimeMs;
    double framesPerSecond;
    double avgFrameTimeMs;
    int totalProcessed;
};

template<typename SystemType>
BenchmarkResult runBenchmark(const std::string& systemName, int numModules, int numFrames, int workloadIterations) {
    auto system = std::make_unique<SystemType>();

    // FAIR COMPARISON FIX:
    // SequentialModuleSystem only keeps 1 module (replaces on each register)
    // ThreadedModuleSystem keeps all N modules
    // To compare fairly: Sequential gets N× workload, Threaded gets 1× workload per module
    constexpr bool isSequential = std::is_same_v<SystemType, SequentialModuleSystem>;
    int adjustedWorkload = isSequential ? (workloadIterations * numModules) : workloadIterations;

    // Create modules
    std::vector<CPUWorkloadModule*> moduleRefs;

    for (int i = 0; i < numModules; i++) {
        std::string moduleName = "Module" + std::to_string(i);

        auto module = std::make_unique<CPUWorkloadModule>(moduleName, adjustedWorkload);
        auto* modulePtr = module.get();
        moduleRefs.push_back(modulePtr);

        system->registerModule(moduleName, std::move(module));
    }

    // Warmup (5 frames)
    for (int i = 0; i < 5; i++) {
        system->processModules(1.0f / 60.0f);
    }

    // Benchmark
    auto startTime = high_resolution_clock::now();

    for (int frame = 0; frame < numFrames; frame++) {
        system->processModules(1.0f / 60.0f);
    }

    auto endTime = high_resolution_clock::now();
    double totalTimeMs = duration_cast<microseconds>(endTime - startTime).count() / 1000.0;

    // Collect metrics
    int totalProcessed = 0;
    for (auto* mod : moduleRefs) {
        totalProcessed += mod->getProcessCount();
    }

    BenchmarkResult result;
    result.systemName = systemName;
    result.numModules = numModules;
    result.numFrames = numFrames;
    result.totalTimeMs = totalTimeMs;
    result.framesPerSecond = (numFrames * 1000.0) / totalTimeMs;
    result.avgFrameTimeMs = totalTimeMs / numFrames;
    result.totalProcessed = totalProcessed;

    return result;
}

void printResult(const BenchmarkResult& result) {
    std::cout << "\n=== " << result.systemName << " ===\n";
    std::cout << "  Modules:         " << result.numModules << "\n";
    std::cout << "  Frames:          " << result.numFrames << "\n";
    std::cout << "  Total Time:      " << std::fixed << std::setprecision(2) << result.totalTimeMs << " ms\n";
    std::cout << "  FPS:             " << std::fixed << std::setprecision(2) << result.framesPerSecond << "\n";
    std::cout << "  Avg Frame Time:  " << std::fixed << std::setprecision(3) << result.avgFrameTimeMs << " ms\n";
    std::cout << "  Total Processed: " << result.totalProcessed << "\n";
}

void printComparison(const BenchmarkResult& sequential, const BenchmarkResult& threaded) {
    double speedup = sequential.totalTimeMs / threaded.totalTimeMs;
    double fpsGain = ((threaded.framesPerSecond - sequential.framesPerSecond) / sequential.framesPerSecond) * 100.0;
    double efficiency = (speedup / threaded.numModules) * 100.0;

    std::cout << "\n================================================================================\n";
    std::cout << "PERFORMANCE COMPARISON\n";
    std::cout << "================================================================================\n";
    std::cout << "  Speedup:         " << std::fixed << std::setprecision(2) << speedup << "x\n";
    std::cout << "  FPS Gain:        " << (fpsGain > 0 ? "+" : "") << fpsGain << "%\n";
    std::cout << "  Efficiency:      " << efficiency << "% (ideal: 100%)\n";
    std::cout << "\n  Interpretation:\n";

    if (speedup >= threaded.numModules * 0.8) {
        std::cout << "    ✅ EXCELLENT: Near-linear scaling (" << speedup << "x with " << threaded.numModules << " modules)\n";
    } else if (speedup >= threaded.numModules * 0.5) {
        std::cout << "    ✅ GOOD: Decent parallelization (" << speedup << "x with " << threaded.numModules << " modules)\n";
    } else if (speedup >= 1.5) {
        std::cout << "    ⚠️  MODERATE: Some parallelization benefit (" << speedup << "x with " << threaded.numModules << " modules)\n";
    } else if (speedup >= 1.0) {
        std::cout << "    ⚠️  LIMITED: Minimal benefit from threading (" << speedup << "x with " << threaded.numModules << " modules)\n";
    } else {
        std::cout << "    ❌ SLOWER: Threading overhead exceeds benefits (" << speedup << "x with " << threaded.numModules << " modules)\n";
    }

    std::cout << "\n  Why:\n";
    if (speedup < 1.5) {
        std::cout << "    - ThreadedModuleSystem uses barrier synchronization (all threads wait)\n";
        std::cout << "    - CPU-bound workload may be memory-limited\n";
        std::cout << "    - Context switching overhead with many threads\n";
    } else {
        std::cout << "    - Effective thread parallelization\n";
        std::cout << "    - CPU-bound workload benefits from multi-core\n";
    }
    std::cout << "================================================================================\n";
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "THREADED vs SEQUENTIAL - REAL CPU-BOUND BENCHMARK\n";
    std::cout << "================================================================================\n";
    std::cout << "Hardware: " << std::thread::hardware_concurrency() << " logical cores\n";
    std::cout << "Workload: REAL CPU calculations (sqrt, sin, cos)\n\n";

    // Disable verbose logging
    spdlog::set_level(spdlog::level::off);

    // Test configurations: (numModules, numFrames, workloadIterations)
    struct TestConfig {
        int numModules;
        int numFrames;
        int workloadIterations;
        std::string description;
    };

    // IMPORTANT: Sequential processes 1 module, Threaded processes N modules
    // To make comparison fair, Sequential gets N× workload to match total work
    std::vector<TestConfig> configs = {
        {2, 100, 100000, "Light workload, 2 modules (total: 200k iterations)"},
        {4, 100, 100000, "Light workload, 4 modules (total: 400k iterations)"},
        {8, 100, 100000, "Light workload, 8 modules (total: 800k iterations)"},
        {4, 50, 500000, "Heavy workload, 4 modules (total: 2M iterations)"},
        {8, 50, 500000, "Heavy workload, 8 modules (total: 4M iterations)"},
    };

    for (size_t i = 0; i < configs.size(); i++) {
        auto& config = configs[i];

        std::cout << "\n--- Test " << (i + 1) << "/" << configs.size() << ": " << config.description << " ---\n";

        // Run sequential
        std::cout << "Running SequentialModuleSystem...\n";
        auto seqResult = runBenchmark<SequentialModuleSystem>(
            "Sequential_" + std::to_string(i), config.numModules, config.numFrames, config.workloadIterations
        );
        printResult(seqResult);

        // Run threaded
        std::cout << "\nRunning ThreadedModuleSystem...\n";
        auto threadedResult = runBenchmark<ThreadedModuleSystem>(
            "Threaded_" + std::to_string(i), config.numModules, config.numFrames, config.workloadIterations
        );
        printResult(threadedResult);

        // Compare
        printComparison(seqResult, threadedResult);
    }

    std::cout << "\n🎉 BENCHMARK COMPLETE\n";
    std::cout << "================================================================================\n";

    return 0;
}
