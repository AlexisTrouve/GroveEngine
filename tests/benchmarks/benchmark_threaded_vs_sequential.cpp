#include "grove/ThreadedModuleSystem.h"
#include "grove/SequentialModuleSystem.h"
#include "grove/JsonDataNode.h"
#include "../helpers/TestAssertions.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <vector>

using namespace grove;

// ============================================================================
// Benchmark Module: Simulates realistic game module workload
// ============================================================================

class BenchmarkModule : public IModule {
private:
    int counter = 0;
    std::string name;
    IIO* io = nullptr;
    std::shared_ptr<spdlog::logger> logger;
    int workDelayMs = 0;

public:
    BenchmarkModule(std::string moduleName, int workMs = 5)
        : name(std::move(moduleName)), workDelayMs(workMs) {
        logger = spdlog::get("BenchmarkModule_" + name);
        if (!logger) {
            logger = spdlog::stdout_color_mt("BenchmarkModule_" + name);
            logger->set_level(spdlog::level::off);  // Disable logging for benchmarks
        }
    }

    void process(const IDataNode& input) override {
        counter++;

        // Simulate realistic work (e.g., physics, AI, rendering preparation)
        if (workDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(workDelayMs));
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
        return std::make_unique<JsonDataNode>("health", nlohmann::json{{"status", "healthy"}});
    }

    void shutdown() override {}

    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", nlohmann::json{{"counter", counter}});
    }

    void setState(const IDataNode& state) override {
        counter = state.getInt("counter", 0);
    }

    std::string getType() const override {
        return "BenchmarkModule";
    }

    bool isIdle() const override {
        return true;
    }

    int getCounter() const { return counter; }
};

// ============================================================================
// Benchmark Runner
// ============================================================================

struct BenchmarkResult {
    int numModules;
    int workMs;
    int numFrames;
    float sequentialTime;
    float threadedTime;
    float speedup;
    float sequentialAvgFrame;
    float threadedAvgFrame;
};

BenchmarkResult runBenchmark(int numModules, int workMs, int numFrames) {
    BenchmarkResult result;
    result.numModules = numModules;
    result.workMs = workMs;
    result.numFrames = numFrames;

    // --- Sequential System ---
    {
        auto system = std::make_unique<SequentialModuleSystem>();

        for (int i = 0; i < numModules; i++) {
            auto module = std::make_unique<BenchmarkModule>("SeqModule_" + std::to_string(i), workMs);
            system->registerModule("SeqModule_" + std::to_string(i), std::move(module));
        }

        auto start = std::chrono::high_resolution_clock::now();
        for (int frame = 0; frame < numFrames; frame++) {
            system->processModules(1.0f / 60.0f);
        }
        auto end = std::chrono::high_resolution_clock::now();

        result.sequentialTime = std::chrono::duration<float, std::milli>(end - start).count();
        result.sequentialAvgFrame = result.sequentialTime / numFrames;
    }

    // --- Threaded System ---
    {
        auto system = std::make_unique<ThreadedModuleSystem>();

        for (int i = 0; i < numModules; i++) {
            auto module = std::make_unique<BenchmarkModule>("ThreadedModule_" + std::to_string(i), workMs);
            system->registerModule("ThreadedModule_" + std::to_string(i), std::move(module));
        }

        auto start = std::chrono::high_resolution_clock::now();
        for (int frame = 0; frame < numFrames; frame++) {
            system->processModules(1.0f / 60.0f);
        }
        auto end = std::chrono::high_resolution_clock::now();

        result.threadedTime = std::chrono::duration<float, std::milli>(end - start).count();
        result.threadedAvgFrame = result.threadedTime / numFrames;
    }

    // Calculate speedup
    result.speedup = result.sequentialTime / result.threadedTime;

    return result;
}

// ============================================================================
// Print Results
// ============================================================================

void printResultsTable(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n";
    std::cout << "┌─────────────────────────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│                     Sequential vs Threaded Performance Comparison                       │\n";
    std::cout << "├────────┬────────┬─────────┬──────────────┬──────────────┬──────────┬──────────┬─────────┤\n";
    std::cout << "│ Modules│ Work   │ Frames  │ Sequential   │ Threaded     │ Seq/Frame│ Thr/Frame│ Speedup │\n";
    std::cout << "│        │ (ms)   │         │ Total (ms)   │ Total (ms)   │ (ms)     │ (ms)     │         │\n";
    std::cout << "├────────┼────────┼─────────┼──────────────┼──────────────┼──────────┼──────────┼─────────┤\n";

    for (const auto& r : results) {
        std::cout << "│ " << std::setw(6) << r.numModules << " │ ";
        std::cout << std::setw(6) << r.workMs << " │ ";
        std::cout << std::setw(7) << r.numFrames << " │ ";
        std::cout << std::setw(12) << std::fixed << std::setprecision(2) << r.sequentialTime << " │ ";
        std::cout << std::setw(12) << std::fixed << std::setprecision(2) << r.threadedTime << " │ ";
        std::cout << std::setw(8) << std::fixed << std::setprecision(3) << r.sequentialAvgFrame << " │ ";
        std::cout << std::setw(8) << std::fixed << std::setprecision(3) << r.threadedAvgFrame << " │ ";
        std::cout << std::setw(7) << std::fixed << std::setprecision(2) << r.speedup << "x │\n";
    }

    std::cout << "└────────┴────────┴─────────┴──────────────┴──────────────┴──────────┴──────────┴─────────┘\n";
}

void printCSV(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n=== CSV Output ===\n";
    std::cout << "Modules,WorkMs,Frames,SequentialTotal,ThreadedTotal,SequentialAvg,ThreadedAvg,Speedup\n";

    for (const auto& r : results) {
        std::cout << r.numModules << ","
                  << r.workMs << ","
                  << r.numFrames << ","
                  << std::fixed << std::setprecision(2) << r.sequentialTime << ","
                  << r.threadedTime << ","
                  << std::setprecision(3) << r.sequentialAvgFrame << ","
                  << r.threadedAvgFrame << ","
                  << std::setprecision(2) << r.speedup << "\n";
    }
}

void printAnalysis(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n=== Performance Analysis ===\n\n";

    // Find best speedup
    auto bestSpeedup = *std::max_element(results.begin(), results.end(),
        [](const BenchmarkResult& a, const BenchmarkResult& b) {
            return a.speedup < b.speedup;
        });

    std::cout << "Best Speedup: " << std::fixed << std::setprecision(2)
              << bestSpeedup.speedup << "x with "
              << bestSpeedup.numModules << " modules ("
              << bestSpeedup.workMs << "ms work)\n";

    // Overhead analysis for 1 module
    auto singleModule = std::find_if(results.begin(), results.end(),
        [](const BenchmarkResult& r) { return r.numModules == 1; });

    if (singleModule != results.end()) {
        float overhead = singleModule->threadedTime - singleModule->sequentialTime;
        float overheadPercent = (overhead / singleModule->sequentialTime) * 100.0f;

        std::cout << "\nSingle Module Overhead:\n";
        std::cout << "  Sequential: " << std::fixed << std::setprecision(2)
                  << singleModule->sequentialTime << "ms\n";
        std::cout << "  Threaded:   " << singleModule->threadedTime << "ms\n";
        std::cout << "  Overhead:   " << overhead << "ms ("
                  << std::setprecision(1) << overheadPercent << "%)\n";
    }

    // Parallel efficiency analysis
    std::cout << "\nParallel Efficiency:\n";
    for (const auto& r : results) {
        if (r.numModules > 1) {
            float efficiency = (r.speedup / r.numModules) * 100.0f;
            std::cout << "  " << r.numModules << " modules: "
                      << std::fixed << std::setprecision(1) << efficiency << "% efficient\n";
        }
    }

    // Recommendations
    std::cout << "\nRecommendations:\n";
    if (bestSpeedup.speedup >= 2.0f) {
        std::cout << "  ✓ ThreadedModuleSystem shows excellent parallel performance\n";
    } else if (bestSpeedup.speedup >= 1.5f) {
        std::cout << "  ✓ ThreadedModuleSystem shows good parallel performance\n";
    } else {
        std::cout << "  ⚠️  Parallel overhead may be high - investigate synchronization costs\n";
    }

    if (singleModule != results.end() && singleModule->speedup < 0.8f) {
        std::cout << "  ⚠️  Significant overhead for single module - use SequentialModuleSystem for 1 module\n";
    }

    std::cout << "  ℹ️  ThreadedModuleSystem is best for 2-8 modules with moderate workloads\n";
}

// ============================================================================
// Main Benchmark Runner
// ============================================================================

int main() {
    std::cout << "================================================================================\n";
    std::cout << "ThreadedModuleSystem vs SequentialModuleSystem - Performance Benchmark\n";
    std::cout << "================================================================================\n";

    // Disable verbose logging for benchmarks
    spdlog::set_level(spdlog::level::warn);

    std::vector<BenchmarkResult> results;

    // Benchmark configurations: (modules, work_ms, frames)
    std::vector<std::tuple<int, int, int>> configs = {
        {1, 5, 50},    // 1 module,  5ms work, 50 frames
        {2, 5, 50},    // 2 modules, 5ms work, 50 frames
        {4, 5, 50},    // 4 modules, 5ms work, 50 frames
        {8, 5, 50},    // 8 modules, 5ms work, 50 frames
        {4, 10, 20},   // 4 modules, 10ms work, 20 frames (heavier load)
        {8, 10, 20},   // 8 modules, 10ms work, 20 frames
    };

    std::cout << "\nRunning benchmarks...\n";

    int totalBenchmarks = configs.size();
    int currentBenchmark = 0;

    for (const auto& config : configs) {
        int modules = std::get<0>(config);
        int workMs = std::get<1>(config);
        int frames = std::get<2>(config);

        currentBenchmark++;
        std::cout << "  [" << currentBenchmark << "/" << totalBenchmarks << "] "
                  << modules << " modules, " << workMs << "ms work, "
                  << frames << " frames... ";
        std::cout.flush();

        auto result = runBenchmark(modules, workMs, frames);
        results.push_back(result);

        std::cout << "Speedup: " << std::fixed << std::setprecision(2)
                  << result.speedup << "x\n";
    }

    // Print results
    printResultsTable(results);
    printAnalysis(results);
    printCSV(results);

    std::cout << "\n================================================================================\n";
    std::cout << "Benchmark Complete!\n";
    std::cout << "================================================================================\n";

    return 0;
}
