// ============================================================================
// Static engine hosting — the acceptance test for the Drifterra grievance.
//
// WHAT: 3 already-instantiated modules (Cadence -> Colony -> Logistics) hosted
//       BY the engine via IEngine::registerStaticModule(), then driven with
//       engine.step(dt) only. They talk to each other through the engine's IIO.
//
// WHY:  The generic IEngine/IModuleSystem layer used to have NO entry point for
//       a static-linked module (only .so/.dll file loading, and loadModules()
//       itself was a stub), never called setConfiguration(), and never drained
//       the module IIO queues — so a static host had to bypass the engine
//       entirely (instantiate by hand, drive process() by hand, route via
//       IntraIOManager by hand). This locks the fix: the host does ZERO of that.
//
// HOW:  Each module publishes/subscribes on CONFIG-driven topics (the engine
//       forwards config to setConfiguration). Colony and Logistics deliberately
//       DO NOT drain their own IIO in process() — they rely on the engine to
//       pump their inbox each step(). If the engine doesn't, their handlers
//       never fire and the cross-module assertions fail (the reported bug).
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/IIO.h>
#include <grove/JsonDataNode.h>

#include <memory>
#include <string>

using namespace grove;

namespace {

// Minimal IModule base: implements the boilerplate pure-virtuals these hosting
// tests don't exercise, so each concrete module only defines process(),
// setConfiguration() and getType(). A real game module looks like this minus the
// trivial bodies plus its actual subsystem logic.
class HostTestModule : public IModule {
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
    ITaskScheduler* sched_ = nullptr;
    std::unique_ptr<IDataNode> cfg_;
};

// Cadence — publishes a beat on its tick topic every process(). The topic comes
// from CONFIG (not from process() input), so it works under the engine's fixed
// per-frame input. Proves the engine actually runs a static module's process().
class CadenceModule : public HostTestModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler* s) override {
        io_ = io; sched_ = s;
        tickTopic_ = c.getString("tickTopic", "cadence:tick");
    }
    void process(const IDataNode&) override {
        if (!io_) return;
        ++beats_;
        io_->publish(tickTopic_, std::make_unique<JsonDataNode>("tick", nlohmann::json{{"n", beats_}}));
    }
    std::string getType() const override { return "CadenceModule"; }
    int beats() const { return beats_; }
private:
    std::string tickTopic_;
    int beats_ = 0;
};

// Colony — subscribes to Cadence's ticks, counts them, and re-publishes a report.
// Does NOT self-drain: relies on the engine pumping its inbox each step().
class ColonyModule : public HostTestModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler* s) override {
        io_ = io; sched_ = s;
        reportTopic_ = c.getString("reportTopic", "colony:report");
        io_->subscribe(c.getString("tickTopic", "cadence:tick"),
                       [this](const Message&) { ++ticksSeen_; });
    }
    void process(const IDataNode&) override {
        if (!io_) return;
        io_->publish(reportTopic_, std::make_unique<JsonDataNode>("report", nlohmann::json{{"ticks", ticksSeen_}}));
    }
    std::string getType() const override { return "ColonyModule"; }
    int ticksSeen() const { return ticksSeen_; }
private:
    std::string reportTopic_;
    int ticksSeen_ = 0;
};

// Logistics — subscribes to Colony's reports, records how many it saw and the
// latest tick-count carried. Second hop of the chain (A -> B -> C). No self-drain.
class LogisticsModule : public HostTestModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler* s) override {
        io_ = io; sched_ = s;
        io_->subscribe(c.getString("reportTopic", "colony:report"),
                       [this](const Message& m) {
                           ++reportsSeen_;
                           if (m.data) lastTicks_ = m.data->getInt("ticks", -1);
                       });
    }
    void process(const IDataNode&) override {}
    std::string getType() const override { return "LogisticsModule"; }
    int reportsSeen() const { return reportsSeen_; }
    int lastTicks() const { return lastTicks_; }
private:
    int reportsSeen_ = 0;
    int lastTicks_ = -1;
};

} // namespace

TEST_CASE("3 static modules run through the engine and talk — zero hand-wiring", "[engine][static]") {
    DebugEngine engine;
    engine.initialize();

    // Topics wired via config — the engine forwards this to setConfiguration().
    auto cadenceCfg = std::make_unique<JsonDataNode>("config",
        nlohmann::json{{"tickTopic", "cadence:tick"}});
    auto colonyCfg = std::make_unique<JsonDataNode>("config",
        nlohmann::json{{"tickTopic", "cadence:tick"}, {"reportTopic", "colony:report"}});
    auto logiCfg = std::make_unique<JsonDataNode>("config",
        nlohmann::json{{"reportTopic", "colony:report"}});

    auto cadence = std::make_unique<CadenceModule>();
    auto colony  = std::make_unique<ColonyModule>();
    auto logi    = std::make_unique<LogisticsModule>();
    // Borrow raw pointers to assert internal counters after stepping (the engine
    // owns the modules once registered).
    auto* cadencePtr = cadence.get();
    auto* colonyPtr  = colony.get();
    auto* logiPtr    = logi.get();

    // THE FIX: hand already-instantiated modules to the engine — no .so/.dll, no
    // manual setConfiguration, no manual IIO wiring.
    engine.registerStaticModule("Cadence",   std::move(cadence), ModuleSystemType::SEQUENTIAL, std::move(cadenceCfg));
    engine.registerStaticModule("Colony",    std::move(colony),  ModuleSystemType::SEQUENTIAL, std::move(colonyCfg));
    engine.registerStaticModule("Logistics", std::move(logi),    ModuleSystemType::SEQUENTIAL, std::move(logiCfg));

    // Drive the engine — the ONLY per-frame call. No manual process(), no manual
    // pump, no IntraIOManager wiring.
    for (int i = 0; i < 20; ++i) {
        engine.step(1.0f / 60.0f);
    }

    // (a) The engine actually processed the static modules.
    REQUIRE(cadencePtr->beats() >= 20);

    // (b) Cross-module routing THROUGH the engine: Colony received Cadence's
    //     ticks, Logistics received Colony's reports. This is the acceptance
    //     criterion — it fails if the engine doesn't pump the module IIO queues.
    REQUIRE(colonyPtr->ticksSeen() > 0);
    REQUIRE(logiPtr->reportsSeen() > 0);
    REQUIRE(logiPtr->lastTicks() > 0);

    engine.shutdown();
}
