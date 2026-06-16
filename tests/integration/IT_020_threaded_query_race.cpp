/**
 * Integration Test IT_020: ThreadedModuleSystem queryModule data race (audit #10).
 *
 * BUG D: queryModule() calls module->process() directly from the CALLER's thread,
 * while the module's worker thread independently calls process() on the SAME instance
 * whenever processModules() dispatches a frame. Two threads in the same module's
 * process() at once = data race on the module's internal state. The old code only
 * LOGGED a warning ("potential data race").
 *
 * This test makes the race OBSERVABLE: a probe module flags any concurrent entry into
 * its process() (a re-entrancy detector). One thread spins processModules() (driving
 * the worker's process()) while the main thread hammers queryModule(). Without mutual
 * exclusion the detector trips; the per-worker processMutex fix serializes the two
 * process() calls so it never does.
 *
 * Note: a passing run is not a proof of race-freedom (only TSAN is), but the fix is
 * mutual exclusion by construction; this test is the observable regression guard and
 * reliably trips on the unfixed code thanks to the widened process() window.
 */

#include <catch2/catch_test_macros.hpp>
#include <grove/ThreadedModuleSystem.h>
#include <grove/IModule.h>
#include <grove/JsonDataNode.h>

#include <atomic>
#include <thread>
#include <chrono>

using namespace grove;

namespace {
// Probe module: detects two threads being inside process() simultaneously.
class RaceProbeModule : public IModule {
public:
    std::atomic<int>  inProcess{0};
    std::atomic<bool> raceDetected{false};
    std::atomic<long> processCount{0};

    void process(const IDataNode&) override {
        // If the counter was already non-zero, another thread is in process() too.
        if (inProcess.fetch_add(1, std::memory_order_acq_rel) != 0) {
            raceDetected.store(true, std::memory_order_release);
        }
        // Widen the window so a real overlap is observable (and the unfixed code trips).
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        processCount.fetch_add(1, std::memory_order_relaxed);
        inProcess.fetch_sub(1, std::memory_order_acq_rel);
    }

    void setConfiguration(const IDataNode&, IIO*, ITaskScheduler*) override {}
    const IDataNode& getConfiguration() override { return m_cfg; }
    std::unique_ptr<IDataNode> getHealthStatus() override { return std::make_unique<JsonDataNode>("h"); }
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override { return std::make_unique<JsonDataNode>("s"); }
    void setState(const IDataNode&) override {}
    std::string getType() const override { return "RaceProbe"; }
    bool isIdle() const override { return true; }

private:
    JsonDataNode m_cfg{"cfg"};
};
} // namespace

TEST_CASE("IT_020: queryModule does not race the worker thread's process() (#10)", "[integration][threaded][race]") {
    ThreadedModuleSystem sys;
    auto probe = std::make_unique<RaceProbeModule>();
    RaceProbeModule* p = probe.get();
    sys.registerModule("probe", std::move(probe));

    // Background thread: keep dispatching frames (each one drives the worker's process()).
    std::atomic<bool> stop{false};
    std::thread frames([&] {
        while (!stop.load(std::memory_order_acquire)) {
            sys.processModules(0.016f);
        }
    });

    // Main thread: hammer queryModule() — each call enters process() on the caller thread.
    JsonDataNode input("q");
    for (int i = 0; i < 3000 && !p->raceDetected.load(std::memory_order_acquire); ++i) {
        try { sys.queryModule("probe", input); } catch (...) {}
    }

    stop.store(true, std::memory_order_release);
    frames.join();

    INFO("processCount=" << p->processCount.load() << " raceDetected=" << p->raceDetected.load());
    REQUIRE(p->processCount.load() > 0);                            // both paths actually executed
    REQUIRE(p->raceDetected.load(std::memory_order_acquire) == false);  // never two process() at once
}
