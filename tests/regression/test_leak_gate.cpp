/**
 * test_leak_gate — a FAST, committed anti-regression LEAK GATE (quality Phase 4).
 *
 * WHAT : hammers the engine's core allocation-heavy paths in a tight loop — DebugEngine lifecycle,
 *        static-module registration + engine.step() pumping + cross-module IIO routing, and raw
 *        IntraIOManager instance churn with shared-payload pub/sub — then exits cleanly.
 * WHY  : Phase 1 proved the core/hot-reload path is leak-free under ASan/LSan, but that was a MANUAL sweep.
 *        This makes the "0 leaks" win a permanent, automated regression gate. Under LeakSanitizer (a
 *        Linux/WSL `-DGROVE_ENABLE_ASAN=ON` build) LSan runs at process exit and returns non-zero on ANY leak
 *        -> this ctest fails. On Windows (MinGW has no sanitizer) it is a fast SMOKE test that just proves the
 *        scenario still runs end to end. It is deliberately FAST (a gate runs often) — unlike the 200-cycle
 *        RSS-based MemoryLeakHunter, which stays as a separate long stress test.
 * HOW  : plain main() + a tiny CHECK harness (exit non-zero on a FUNCTIONAL failure). The leak check itself is
 *        LSan's job — we only make sure the scenario actually EXECUTED (so real code is exercised), then exit 0.
 *
 * Coverage (honest line): core engine lifecycle + module-system routing + IntraIO alloc/pub/sub. NOT here: the
 * dlopen/dlclose hot-reload path (covered under ASan by ModuleDependencies/MultiVersion/ReloadAfterThrow — see
 * docs/design/quality-hardening-handoff.md Phase 1) and GPU/SDL (no sanitizer toolchain on those).
 *
 * Run AS THE GATE (Linux/WSL):
 *   cmake -S . -B build-asan -DCMAKE_CXX_COMPILER=g++ -DGROVE_ENABLE_ASAN=ON -DGROVE_BUILD_TESTS=ON \
 *         -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON -DCMAKE_DISABLE_FIND_PACKAGE_SDL2_mixer=ON
 *   cmake --build build-asan --target test_leak_gate
 *   ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -R LeakGate --output-on-failure
 */

#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/IIO.h>
#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>

#include <cstdio>
#include <memory>
#include <string>

using namespace grove;

// --- tiny check harness: a FUNCTIONAL failure exits non-zero (LSan handles leaks independently). ---
static int g_fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_fails; } } while (0)

namespace {

// Minimal IModule base — the boilerplate pure-virtuals these gate modules don't exercise (mirrors the
// static-hosting test). Each concrete module only defines process()/setConfiguration()/getType().
class GateModule : public IModule {
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

// A -> B -> C chain: Cadence publishes a beat, Colony counts + re-publishes, Logistics records. Two routing
// hops through the engine's IIO — allocation on publish (JSON payloads), subscription state, per-frame pumping.
class CadenceModule : public GateModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler*) override {
        io_ = io; topic_ = c.getString("tickTopic", "gate:tick");
    }
    void process(const IDataNode&) override {
        if (io_) io_->publish(topic_, std::make_unique<JsonDataNode>("tick", nlohmann::json{{"n", ++beats_}}));
    }
    std::string getType() const override { return "CadenceModule"; }
    int beats() const { return beats_; }
private:
    std::string topic_; int beats_ = 0;
};
class ColonyModule : public GateModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler*) override {
        io_ = io; report_ = c.getString("reportTopic", "gate:report");
        io_->subscribe(c.getString("tickTopic", "gate:tick"), [this](const Message&) { ++ticks_; });
    }
    void process(const IDataNode&) override {
        if (io_) io_->publish(report_, std::make_unique<JsonDataNode>("report", nlohmann::json{{"ticks", ticks_}}));
    }
    std::string getType() const override { return "ColonyModule"; }
    int ticks() const { return ticks_; }
private:
    std::string report_; int ticks_ = 0;
};
class LogisticsModule : public GateModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler*) override {
        io_ = io;
        io_->subscribe(c.getString("reportTopic", "gate:report"), [this](const Message&) { ++reports_; });
    }
    void process(const IDataNode&) override {}
    std::string getType() const override { return "LogisticsModule"; }
    int reports() const { return reports_; }
private:
    int reports_ = 0;
};

// One full engine lifecycle: build, register the 3-module chain, step (routing flows), shut down, destroy.
// Repeated so LSan sees any PER-CYCLE leak (a growing allocation across identical cycles).
void engineLifecycleCycle() {
    DebugEngine engine;
    engine.initialize();

    engine.registerStaticModule("Cadence", std::make_unique<CadenceModule>(), ModuleSystemType::SEQUENTIAL,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"tickTopic", "gate:tick"}}));
    auto colony = std::make_unique<ColonyModule>();
    auto logi   = std::make_unique<LogisticsModule>();
    auto* colonyPtr = colony.get();
    auto* logiPtr   = logi.get();
    engine.registerStaticModule("Colony", std::move(colony), ModuleSystemType::SEQUENTIAL,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"tickTopic", "gate:tick"}, {"reportTopic", "gate:report"}}));
    engine.registerStaticModule("Logistics", std::move(logi), ModuleSystemType::SEQUENTIAL,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"reportTopic", "gate:report"}}));

    for (int i = 0; i < 30; ++i) engine.step(1.0f / 60.0f);

    // Prove the scenario ACTUALLY RAN (else it exercises nothing and the leak gate is vacuous).
    CHECK(colonyPtr->ticks() > 0,   "engine cycle: Colony received no ticks (routing didn't run)");
    CHECK(logiPtr->reports() > 0,   "engine cycle: Logistics received no reports (2nd hop didn't run)");

    engine.shutdown();
}

// Raw IntraIOManager churn: create instances, subscribe, publish shared payloads, dispatch, remove. Exercises
// the manager's instance alloc/dealloc + the zero-copy shared_ptr<const> payload path directly (no modules).
void iioChurnCycle(int cycle) {
    auto& mgr = IntraIOManager::getInstance();
    const std::string base = "leakgate_" + std::to_string(cycle) + "_";
    auto pub = mgr.createInstance(base + "pub");
    auto sub = mgr.createInstance(base + "sub");

    int seen = 0;
    sub->subscribe("gate:churn", [&seen](const Message&) { ++seen; });
    for (int i = 0; i < 50; ++i)
        pub->publish("gate:churn", std::make_unique<JsonDataNode>("m", nlohmann::json{{"i", i}, {"payload", "some string data"}}));
    while (sub->hasMessages() > 0) sub->pullAndDispatch();

    CHECK(seen == 50, "iio churn: not all published messages were dispatched");

    // Tear the instances back down — the alloc/dealloc balance is what LSan verifies across cycles.
    mgr.removeInstance(base + "pub");
    mgr.removeInstance(base + "sub");
}

} // namespace

int main() {
    // A handful of identical cycles — enough for LSan to flag a per-cycle leak, fast enough for a gate (<<1s).
    constexpr int CYCLES = 8;
    for (int c = 0; c < CYCLES; ++c) {
        engineLifecycleCycle();
        iioChurnCycle(c);
    }

    if (g_fails == 0)
        std::fprintf(stdout, "LeakGate: %d cycles ran clean (LSan enforces the leak check at exit)\n", CYCLES);
    return g_fails ? 1 : 0;
}
