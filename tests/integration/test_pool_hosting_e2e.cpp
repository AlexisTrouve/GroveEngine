// ============================================================================
// E2E: the WHOLE thing runs in the THREAD_POOL on the engine — a cross-module
//      IIO chain hosted via registerStaticModule(THREAD_POOL), driven by
//      engine.step() only. This is the missing twin of test_threaded_hosting_e2e:
//      same synthetic Producer -> Relay -> Sink chain, but routed into the SHARED
//      ThreadPoolModuleSystem instead of one-thread-per-module.
//
// WHAT: three modules (Producer -> Relay -> Sink) hosted by DebugEngine with the
//       THREAD_POOL strategy. Producer publishes "chain:a" every frame; Relay
//       subscribes "chain:a" and republishes "chain:b"; Sink subscribes "chain:b"
//       and counts. registerStaticModule(THREAD_POOL) drops all three into ONE
//       work-stealing pool (M ~= cores-1 workers shared across the 3 module-tasks),
//       and the whole pub/sub chain must flow across worker threads, delivered by
//       the engine's own loop (engine.step()) — exactly as a static-linked game
//       would consume it with the pool strategy.
//
// WHY:  slice 1 TSan'd the pool STANDALONE (test_threadpool_lifecycle); the threaded
//       system has its hosted-via-engine TSan twin (test_threaded_hosting_e2e). The
//       pool had NO such twin — the engine->pool path (the DebugEngine THREAD_POOL
//       branch + processModuleSystems driving poolSystem_ + cross-thread IIO routing
//       + archi-A drain, all recombined) was never exercised under ThreadSanitizer.
//       This binary closes that gap. Per the doctrine: until a test actually drives
//       it, pool hosting via the engine is "unverified", not "works".
//
//       Each module deliberately mutates its OWN integer state in BOTH its subscribe
//       handler AND its process() body. In the pool a module can be picked up by a
//       DIFFERENT worker on different frames, so this exercises the pool's cross-frame
//       happens-before (the workersDone barrier): if that ordering were wrong, TSan
//       would flag the plain ints `produced_` / `relayed_` / `received_`. Run this
//       binary under TSan (WSL: g++ -fsanitize=thread, launched via
//       `setarch $(uname -m) -R`) to make that proof real, not asserted.
//
// RUN:  ./test_pool_hosting_e2e   (no GPU, no SDL, no .so — pure orchestration)
//       exit 0 = chain flowed (and, where the host has >=2 pool workers, in parallel);
//       non-zero = a hop never delivered, or modules ran serially on a multi-core host.
// ============================================================================

#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/IIO.h>
#include <grove/JsonDataNode.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

using namespace grove;

namespace {

// --- Concurrency probe: do the modules' process() bodies overlap in TIME? ---------
// A shared in-flight counter. ProcGuard bumps it on entry (recording the running
// maximum) and drops it on exit; the short sleep widens the overlap window so the
// probe is reliable. g_maxConcurrent >= 2 proves >=2 pool workers were inside
// process() SIMULTANEOUSLY — true parallelism. (Only meaningful when the pool has
// >=2 workers, i.e. cores >= 3; the engine auto-sizes the pool to cores-1 and
// thread_count is not yet plumbed through registerStaticModule — see the handoff's
// open task #2. On a 1-worker pool this stays 1 and we don't assert on it.)
std::atomic<int> g_inProcess{0};
std::atomic<int> g_maxConcurrent{0};
struct ProcGuard {
    ProcGuard() {
        int n = g_inProcess.fetch_add(1) + 1;
        int prev = g_maxConcurrent.load();
        while (n > prev && !g_maxConcurrent.compare_exchange_weak(prev, n)) {}
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    ~ProcGuard() { g_inProcess.fetch_sub(1); }
};

// Shared IModule boilerplate (mirrors test_threaded_hosting_e2e's ChainModule).
class ChainModule : public IModule {
public:
    const IDataNode& getConfiguration() override {
        if (!cfg_) cfg_ = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
        return *cfg_;
    }
    std::unique_ptr<IDataNode> getHealthStatus() override {
        return std::make_unique<JsonDataNode>("health", nlohmann::json{{"status", "healthy"}});
    }
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", nlohmann::json::object());
    }
    void setState(const IDataNode&) override {}
    bool isIdle() const override { return true; }
protected:
    IIO* io_ = nullptr;
    std::unique_ptr<IDataNode> cfg_;
};

// Producer — the source. Publishes "chain:a" with an incrementing counter each frame.
class ProducerModule : public ChainModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler*) override {
        io_ = io; outTopic_ = c.getString("out", "chain:a");
    }
    void process(const IDataNode&) override {
        ProcGuard _pg;  // record overlap with other modules' process()
        if (!io_) return;
        ++produced_;  // plain int — touched by whichever pool worker picked this task up
        io_->publish(outTopic_, std::make_unique<JsonDataNode>("m", nlohmann::json{{"n", produced_}}));
    }
    std::string getType() const override { return "ProducerModule"; }
    int produced() const { return produced_; }
private:
    std::string outTopic_;
    int produced_ = 0;
};

// Relay — middle of the chain. Subscribes "chain:a" (handler bumps a counter),
// and in process() republishes its running count on "chain:b". The handler runs
// during the worker's post-process() inbox drain (archi-A) — same worker thread.
class RelayModule : public ChainModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler*) override {
        io_ = io;
        outTopic_ = c.getString("out", "chain:b");
        io_->subscribe(c.getString("in", "chain:a"), [this](const Message&) {
            ++relayed_;  // module state mutated wherever this handler is dispatched
        });
    }
    void process(const IDataNode&) override {
        ProcGuard _pg;  // record overlap with other modules' process()
        if (!io_) return;
        // Read relayed_ (written by the handler) AND publish — same state, this thread.
        io_->publish(outTopic_, std::make_unique<JsonDataNode>("m", nlohmann::json{{"relayed", relayed_}}));
    }
    std::string getType() const override { return "RelayModule"; }
    int relayed() const { return relayed_; }
private:
    std::string outTopic_;
    int relayed_ = 0;
};

// Sink — end of the chain. Subscribes "chain:b" and counts deliveries.
class SinkModule : public ChainModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler*) override {
        io_ = io;
        io_->subscribe(c.getString("in", "chain:b"), [this](const Message& m) {
            ++received_;
            if (m.data) lastRelayed_ = m.data->getInt("relayed", -1);
        });
    }
    void process(const IDataNode&) override { ProcGuard _pg; }  // overlap probe
    std::string getType() const override { return "SinkModule"; }
    int received() const { return received_; }
    int lastRelayed() const { return lastRelayed_; }
private:
    int received_ = 0;
    int lastRelayed_ = -1;
};

} // namespace

int main() {
    spdlog::set_level(spdlog::level::off);  // quiet — the verdict is the exit code

    // The engine auto-sizes the shared pool to max(1, hardware_concurrency()-1).
    // Parallelism (overlapping process()) is only possible with >=2 workers, i.e.
    // cores >= 3. On a smaller host the chain still flows correctly (and TSan still
    // validates the engine->pool path) but process() bodies can't overlap, so we
    // only assert parallelism where the host can actually deliver it.
    const unsigned cores = std::thread::hardware_concurrency();
    const bool expectParallel = (cores >= 3);

    std::printf("================================================================\n");
    std::printf("  Pool hosting E2E — Producer -> Relay -> Sink, THREAD_POOL\n");
    std::printf("  host cores=%u  pool workers(auto)=%u  expect-parallel=%s\n",
                cores, (cores > 1 ? cores - 1 : 1), expectParallel ? "yes" : "no");
    std::printf("================================================================\n");

    DebugEngine engine;
    engine.initialize();

    auto producer = std::make_unique<ProducerModule>();
    auto relay    = std::make_unique<RelayModule>();
    auto sink     = std::make_unique<SinkModule>();
    auto* P = producer.get();
    auto* R = relay.get();
    auto* S = sink.get();

    // Host all three with the THREAD_POOL strategy — registerStaticModule routes every
    // one into the SAME shared ThreadPoolModuleSystem (work-stealing across M workers).
    engine.registerStaticModule("producer", std::move(producer), ModuleSystemType::THREAD_POOL,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"out", "chain:a"}}));
    engine.registerStaticModule("relay", std::move(relay), ModuleSystemType::THREAD_POOL,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"in", "chain:a"}, {"out", "chain:b"}}));
    engine.registerStaticModule("sink", std::move(sink), ModuleSystemType::THREAD_POOL,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"in", "chain:b"}}));

    // The ONLY per-frame call. The pool's process() tasks (worker threads) + the
    // post-process() inbox drain + IIO delivery all happen inside step(). A few
    // hundred frames so the chain has time to flow end to end.
    const int frames = 300;
    for (int f = 0; f < frames; ++f) engine.step(1.0f / 60.0f);

    // Snapshot module state BEFORE shutdown(): shutdown() destroys the hosted modules
    // (it resets poolSystem_), after which P/R/S dangle. Reading them post-shutdown is
    // a use-after-free (TSan/ASan catch it).
    const int producedV    = P->produced();
    const int relayedV     = R->relayed();
    const int receivedV    = S->received();
    const int lastRelayedV = S->lastRelayed();
    const int maxConc      = g_maxConcurrent.load();

    engine.shutdown();

    std::printf("  producer published : %d (frames=%d)\n", producedV, frames);
    std::printf("  relay received     : %d\n", relayedV);
    std::printf("  sink received      : %d (last relayed=%d)\n", receivedV, lastRelayedV);
    std::printf("  max concurrent process() : %d  (>=2 == real parallelism)\n", maxConc);

    // The chain must have flowed end to end across the pool's worker threads...
    const bool delivered = (producedV    >= frames - 1)
                        && (relayedV     > 0)
                        && (receivedV    > 0)
                        && (lastRelayedV > 0);
    // ...AND, where the host has >=2 pool workers, the modules must have actually run
    // in parallel (overlapping process()). On a 1-worker pool we can't require it.
    const bool parallel = !expectParallel || (maxConc >= 2);
    const bool ok = delivered && parallel;

    if (!delivered) std::printf("  FAIL — a hop never delivered.\n");
    if (!parallel)  std::printf("  FAIL — modules ran SERIALLY (max concurrency 1) on a >=2-worker host.\n");
    if (ok && !expectParallel)
        std::printf("  OK — pool pub/sub chain flowed (host has 1 pool worker; parallelism not asserted).\n");
    else if (ok)
        std::printf("  OK — pool pub/sub chain flowed AND modules ran in parallel.\n");
    std::printf("================================================================\n");
    return ok ? 0 : 1;
}
