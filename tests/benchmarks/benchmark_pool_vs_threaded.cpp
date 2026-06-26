/**
 * Phase 3 slice 3 — ThreadPoolModuleSystem vs ThreadedModuleSystem (vs Sequential baseline).
 *
 * WHAT: real CPU-bound modules (sqrt/sin/cos, NOT sleep), the SAME per-module workload run on
 *   all three systems, swept over a growing module count N. Each measurement is the MIN of
 *   several repeats (min rejects scheduling noise). Prints time per frame + the pool-vs-threaded
 *   ratio so we can see WHERE the pool wins.
 *
 * WHY: Phase 2 (ThreadedModuleSystem) spawns one OS thread per module — fine up to ~cores, but
 *   at N >> cores it oversubscribes (context-switch thrash). Phase 3 (ThreadPoolModuleSystem)
 *   caps threads at M ~= cores and distributes N module-tasks over them with work-stealing.
 *   This benchmark is the evidence for the roadmap claim "pool for high module counts" — and
 *   tells a host WHEN to pick the pool over per-module threading. It is a measurement (not a
 *   ctest assertion: wall-clock ratios are machine/scheduler dependent).
 *
 * RUN: ./benchmark_pool_vs_threaded   (headless, no GPU/SDL)
 */

#include "grove/ThreadPoolModuleSystem.h"
#include "grove/ThreadedModuleSystem.h"
#include "grove/SequentialModuleSystem.h"
#include "grove/JsonDataNode.h"

#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace grove;
using namespace std::chrono;

// CPU-intensive workload module (same shape as benchmark_threaded_vs_sequential_cpu).
class CPUWorkloadModule : public IModule {
public:
    explicit CPUWorkloadModule(int iterations) : iters_(iterations) {}
    void process(const IDataNode&) override {
        // Real CPU work — defeats the optimizer via volatile accumulation.
        volatile double r = 0.0;
        for (int i = 0; i < iters_; ++i) {
            r += std::sqrt(i * 3.14159265359);
            r += std::sin(i * 0.001);
            r += std::cos(i * 0.001);
            r *= 1.000001;
        }
    }
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler*) override { io_ = io; }
    const IDataNode& getConfiguration() override {
        static JsonDataNode empty("config", nlohmann::json{});
        return empty;
    }
    std::unique_ptr<IDataNode> getHealthStatus() override { return std::make_unique<JsonDataNode>("h", nlohmann::json{{"status","healthy"}}); }
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override { return std::make_unique<JsonDataNode>("s", nlohmann::json::object()); }
    void setState(const IDataNode&) override {}
    std::string getType() const override { return "CPUWorkloadModule"; }
    bool isIdle() const override { return true; }
private:
    IIO* io_ = nullptr;
    int iters_;
};

// One timed run: register N modules, warm up, time `frames` of processModules(). Returns ms.
// Sequential keeps only 1 module (it replaces on each register), so it gets N× the workload to
// match the same TOTAL work as the parallel systems (fair total-work comparison).
template <typename SystemType>
double runOnce(int numModules, int frames, int workload) {
    auto system = std::make_unique<SystemType>();
    constexpr bool isSeq = std::is_same_v<SystemType, SequentialModuleSystem>;
    const int perModule = isSeq ? workload * numModules : workload;

    for (int i = 0; i < numModules; ++i) {
        system->registerModule("M" + std::to_string(i),
                               std::make_unique<CPUWorkloadModule>(perModule));
    }
    for (int i = 0; i < 5; ++i) system->processModules(1.0f / 60.0f);  // warmup

    auto t0 = high_resolution_clock::now();
    for (int f = 0; f < frames; ++f) system->processModules(1.0f / 60.0f);
    auto t1 = high_resolution_clock::now();
    return duration_cast<microseconds>(t1 - t0).count() / 1000.0;
}

template <typename SystemType>
double bestOf(int repeats, int numModules, int frames, int workload) {
    double best = 1e18;
    for (int r = 0; r < repeats; ++r) {
        double ms = runOnce<SystemType>(numModules, frames, workload);
        if (ms < best) best = ms;
    }
    return best;
}

// Sweep one workload regime over a list of N and print the comparison table.
static void printRegime(const char* label, int workload, int frames, int repeats,
                        const std::vector<int>& Ns) {
    std::printf("\n  === %s (workload = %d iters/module/frame, %d frames, best-of-%d) ===\n",
                label, workload, frames, repeats);
    std::printf("  %-6s %-12s %-12s %-12s %-14s %s\n",
                "N", "Seq(ms)", "Threaded(ms)", "Pool(ms)", "Pool/Threaded", "winner");
    std::printf("  ---------------------------------------------------------------------------\n");
    for (int N : Ns) {
        const double seq  = bestOf<SequentialModuleSystem>(repeats, N, frames, workload);
        const double thr  = bestOf<ThreadedModuleSystem>(repeats, N, frames, workload);
        const double pool = bestOf<ThreadPoolModuleSystem>(repeats, N, frames, workload);
        const double ratio = pool / thr;   // <1 => pool faster than threaded
        const char* winner = (ratio < 0.95) ? "POOL" : (ratio > 1.05) ? "threaded" : "~tie";
        std::printf("  %-6d %-12.1f %-12.1f %-12.1f %-14.2f %s\n", N, seq, thr, pool, ratio, winner);
    }
}

int main() {
    spdlog::set_level(spdlog::level::off);

    const unsigned cores = std::thread::hardware_concurrency();
    const unsigned poolM = (cores > 1) ? cores - 1 : 1;  // ThreadPoolModuleSystem auto-sizes to this

    std::printf("================================================================================\n");
    std::printf("  Phase 3 slice 3 — ThreadPool vs Threaded vs Sequential (CPU-bound)\n");
    std::printf("  Hardware: %u logical cores | pool workers (auto) = %u\n", cores, poolM);
    std::printf("  Pool/Threaded < 1 => pool faster. Per-module work is fixed; total work grows with N.\n");
    std::printf("================================================================================\n");

    // HEAVY: each module does a lot of CPU work → the calculation dominates and saturates the
    // cores, so the scheduling/dispatch overhead is in the noise. Expectation: ~tie.
    printRegime("HEAVY per-module work", 80000, 60, 5, {2, 4, 8, 16, 32, 64});

    // LIGHT: each module does little work, so the cost is dominated by THREADING OVERHEAD —
    // the threaded system pays for N OS threads (creation + scheduling of 128-256 threads),
    // the pool only ever has M workers + cheap tasks. This is where the pool should pull ahead.
    printRegime("LIGHT per-module work", 1500, 50, 3, {8, 16, 32, 64, 128, 256});

    std::printf("\n  Takeaway: with heavy CPU work the two are a wash (cores are the bottleneck, not\n");
    std::printf("  dispatch). The pool's edge shows when there are MANY modules doing LITTLE each —\n");
    std::printf("  the threaded system oversubscribes with N OS threads; the pool stays at %u workers.\n", poolM);
    std::printf("================================================================================\n");
    return 0;
}
