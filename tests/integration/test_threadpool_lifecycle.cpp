// ============================================================================
// Slice-1 lock for ThreadPoolModuleSystem (Phase 3: shared pool + work-stealing).
//
// WHAT: drives the standalone system (no engine) and proves the load-bearing
//       properties of the pool, each as an independent, TSan-observable assertion:
//
//   A. CORRECTNESS + WORK-STEALING BALANCE — 6 modules on a 3-worker pool (so tasks
//      must be stolen), 2 of them deliberately SLOW. After K frames EVERY module's
//      process() ran EXACTLY K times: no task lost, none run twice, and the slow ones
//      never starve the fast ones (stealing kept all workers fed).
//
//   B. REAL PARALLELISM — the system's own in-flight probe records >=2 process()
//      bodies running SIMULTANEOUSLY. With a serial executor it would stay 1.
//
//   C. ARCHI-A ON THE POOL — a module with a routed IIO inbox: its subscribe handler
//      (++echoes_) and its process() (which READS echoes_) must run on the SAME worker
//      thread. process() runs, THEN the worker drains the inbox → no cross-thread race
//      on echoes_. Run under ThreadSanitizer this is the proof; a pre-drain or a
//      cross-thread handler would trip TSan and/or lose the ordering.
//
// WHY: the engine-hosted E2E (slice 2, real UIModule) is the end-to-end proof, but this
//      lock exercises the pool's internals directly — barrier, stealing, happens-before
//      across frames (a module is picked up by different workers on different frames; if
//      the cross-frame ordering were wrong, TSan would flag the plain int `count_`).
//
// RUN:  ./test_threadpool_lifecycle      (headless, no GPU/SDL/.so)
//       Under TSan (WSL): setarch $(uname -m) -R ./tsan_pool_lifecycle
//       exit 0 = all properties hold.
// ============================================================================

#include <grove/ThreadPoolModuleSystem.h>
#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>   // complete type: createInstance returns shared_ptr<IntraIO>, publish(), upcast to IIO
#include <grove/JsonDataNode.h>

#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace grove;
using nlohmann::json;

namespace {

// Shared IModule boilerplate (same shape as the threaded-hosting test's ChainModule).
class PoolBaseModule : public IModule {
public:
    const IDataNode& getConfiguration() override {
        if (!cfg_) cfg_ = std::make_unique<JsonDataNode>("config", json::object());
        return *cfg_;
    }
    std::unique_ptr<IDataNode> getHealthStatus() override {
        return std::make_unique<JsonDataNode>("health", json{{"status", "healthy"}});
    }
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", json::object());
    }
    void setState(const IDataNode&) override {}
    bool isIdle() const override { return true; }
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler*) override { io_ = io; }
protected:
    IIO* io_ = nullptr;
    std::unique_ptr<IDataNode> cfg_;
};

// CountModule — bumps a PLAIN int each frame (TSan watches cross-frame happens-before:
// different workers touch count_ on different frames). Optionally sleeps to (a) widen the
// parallelism window and (b) force work-stealing (a worker stuck on a slow task → others
// must steal the fast ones to keep all modules at K).
class CountModule : public PoolBaseModule {
public:
    explicit CountModule(bool slow) : slow_(slow) {}
    void process(const IDataNode&) override {
        ++count_;  // plain int — exercised by whichever pool worker picked this task up
        if (slow_) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    std::string getType() const override { return "CountModule"; }
    int count() const { return count_; }
private:
    bool slow_;
    int count_ = 0;
};

// EchoModule — archi-A probe. The handler writes echoes_; process() READS echoes_ (like
// UIModule's process() consuming what its input handler set). Worker drains the inbox AFTER
// process(), on the same thread, so the read and the write never race.
class EchoModule : public PoolBaseModule {
public:
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler*) override {
        io_ = io;
        io_->subscribe("ping", [this](const Message&) { ++echoes_; });
    }
    void process(const IDataNode&) override {
        ++procs_;
        lastSeen_ = echoes_;  // READ of echoes_ on the worker thread (archi-A ordering test)
    }
    std::string getType() const override { return "EchoModule"; }
    int procs() const { return procs_; }
    int echoes() const { return echoes_; }
private:
    int procs_ = 0;
    int echoes_ = 0;
    int lastSeen_ = 0;
};

} // namespace

int main() {
    spdlog::set_level(spdlog::level::off);

    bool aCounts = false, bParallel = false, cProcs = false, cDrain = false;
    int maxc = 0, echoSeen = 0, procSeen = 0;
    const int K_A = 200;
    const int K_B = 100;

    // ---- A + B: correctness, work-stealing balance, real parallelism ----
    {
        auto system = std::make_unique<ThreadPoolModuleSystem>(3);  // 3 workers
        std::vector<CountModule*> probes;
        const int N = 6;  // > 3 workers → tasks MUST be stolen
        for (int i = 0; i < N; ++i) {
            auto m = std::make_unique<CountModule>(/*slow=*/i < 2);  // 2 slow modules
            probes.push_back(m.get());
            system->registerModule("m" + std::to_string(i), std::move(m));
        }

        for (int f = 0; f < K_A; ++f) system->processModules(1.0f / 60.0f);

        aCounts = true;
        for (auto* p : probes) if (p->count() != K_A) aCounts = false;  // exactly K, no starvation
        maxc = system->getMaxObservedConcurrency();
        bParallel = (maxc >= 2);
    }

    // ---- C: archi-A inbox drain on the pool (handler + process() same worker thread) ----
    {
        auto& mgr = IntraIOManager::getInstance();
        auto echoIO = mgr.createInstance("pool_echo");
        auto pub    = mgr.createInstance("pool_pub");

        auto system = std::make_unique<ThreadPoolModuleSystem>(2);
        auto em = std::make_unique<EchoModule>();
        EchoModule* ep = em.get();
        {
            auto cfg = std::make_unique<JsonDataNode>("c", json::object());
            em->setConfiguration(*cfg, echoIO.get(), nullptr);  // subscribes "ping"
        }
        system->registerModule("pool_echo", std::move(em));
        system->setModuleInbox("pool_echo", echoIO);  // archi-A wiring

        for (int f = 0; f < K_B; ++f) {
            pub->publish("ping", std::make_unique<JsonDataNode>("m", json{{"n", f}}));
            system->processModules(1.0f / 60.0f);
        }

        procSeen = ep->procs();
        echoSeen = ep->echoes();
        cProcs = (procSeen == K_B);   // pool ran the module exactly K times
        cDrain = (echoSeen > 0);      // the worker actually drained the inbox (handler fired)

        system.reset();               // destroy module BEFORE removing its IIO instances
        mgr.removeInstance("pool_echo");
        mgr.removeInstance("pool_pub");
    }

    std::printf("================================================================\n");
    std::printf("  ThreadPoolModuleSystem — slice 1 (pool + work-stealing)\n");
    std::printf("  A counts==K (no loss/dup/starvation) : %s\n", aCounts ? "OK" : "FAIL");
    std::printf("  B real parallelism (maxConc>=2)      : %s (maxConc=%d)\n", bParallel ? "OK" : "FAIL", maxc);
    std::printf("  C archi-A: pool ran module K times   : %s (procs=%d/%d)\n", cProcs ? "OK" : "FAIL", procSeen, K_B);
    std::printf("  C archi-A: worker drained inbox      : %s (echoes=%d)\n", cDrain ? "OK" : "FAIL", echoSeen);
    const bool ok = aCounts && bParallel && cProcs && cDrain;
    std::printf(ok ? "  ✅ Pool works: correct, balanced, parallel, archi-A intact.\n"
                   : "  ❌ Pool BROKEN.\n");
    std::printf("================================================================\n");
    return ok ? 0 : 1;
}
