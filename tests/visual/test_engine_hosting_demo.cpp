// ============================================================================
// Engine hosting demo — "le moteur qui tourne avec le système".
//
// WHAT: a runnable, console demo of the static-hosting fix. A DebugEngine hosts
//       three already-instantiated modules (Cadence -> Colony -> Logistics) and
//       drives them with engine.step(dt) ONLY. You watch the data flow through
//       the engine, frame by frame.
//
// WHY:  the headless regression lock is tests/integration/test_static_engine_hosting.cpp
//       (asserts the behaviour). THIS is the human-readable companion: it narrates
//       what the engine does so the fix is visible, not just asserted. No GPU, no
//       SDL, no .so — pure engine orchestration.
//
// RUN:  ./build/tests/test_engine_hosting_demo
// ============================================================================

#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/IIO.h>
#include <grove/JsonDataNode.h>

#include <iostream>
#include <iomanip>
#include <memory>
#include <string>

using namespace grove;

namespace {

// Trivial IModule boilerplate shared by the demo modules (see the regression test
// for the same pattern). Keeps each concrete module to its actual behaviour.
class DemoModule : public IModule {
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

// Cadence — the beat. Publishes "cadence:tick" every frame.
class CadenceModule : public DemoModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler* s) override {
        io_ = io; sched_ = s; topic_ = c.getString("tickTopic", "cadence:tick");
    }
    void process(const IDataNode&) override {
        if (!io_) return;
        ++beats_;
        io_->publish(topic_, std::make_unique<JsonDataNode>("tick", nlohmann::json{{"n", beats_}}));
    }
    std::string getType() const override { return "CadenceModule"; }
    int beats() const { return beats_; }
private:
    std::string topic_;
    int beats_ = 0;
};

// Colony — consumes ticks (via the engine pump), reports its tick count.
class ColonyModule : public DemoModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler* s) override {
        io_ = io; sched_ = s;
        reportTopic_ = c.getString("reportTopic", "colony:report");
        io_->subscribe(c.getString("tickTopic", "cadence:tick"), [this](const Message&) { ++ticksSeen_; });
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

// Logistics — consumes Colony's reports (via the engine pump). End of the chain.
class LogisticsModule : public DemoModule {
public:
    void setConfiguration(const IDataNode& c, IIO* io, ITaskScheduler* s) override {
        io_ = io; sched_ = s;
        io_->subscribe(c.getString("reportTopic", "colony:report"),
                       [this](const Message& m) { ++reportsSeen_; if (m.data) lastTicks_ = m.data->getInt("ticks", -1); });
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

int main() {
    // Quiet the engine's verbose logging so the demo narration stands alone.
    spdlog::set_level(spdlog::level::off);

    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  GroveEngine — static module hosting demo\n";
    std::cout << "  3 modules hosted BY the engine, driven by engine.step() ONLY.\n";
    std::cout << "  No .so/.dll, no manual process(), no manual IIO routing.\n";
    std::cout << "================================================================\n\n";

    DebugEngine engine;
    engine.initialize();

    // Topic wiring via config — the engine forwards it to setConfiguration().
    auto cadenceCfg = std::make_unique<JsonDataNode>("config", nlohmann::json{{"tickTopic", "cadence:tick"}});
    auto colonyCfg  = std::make_unique<JsonDataNode>("config", nlohmann::json{{"tickTopic", "cadence:tick"}, {"reportTopic", "colony:report"}});
    auto logiCfg    = std::make_unique<JsonDataNode>("config", nlohmann::json{{"reportTopic", "colony:report"}});

    auto cadence = std::make_unique<CadenceModule>();
    auto colony  = std::make_unique<ColonyModule>();
    auto logi    = std::make_unique<LogisticsModule>();
    auto* C = cadence.get();
    auto* O = colony.get();
    auto* L = logi.get();

    std::cout << "  registerStaticModule(\"Cadence\")    -> hosted + wired\n";
    engine.registerStaticModule("Cadence",   std::move(cadence), ModuleSystemType::SEQUENTIAL, std::move(cadenceCfg));
    std::cout << "  registerStaticModule(\"Colony\")     -> hosted + wired\n";
    engine.registerStaticModule("Colony",    std::move(colony),  ModuleSystemType::SEQUENTIAL, std::move(colonyCfg));
    std::cout << "  registerStaticModule(\"Logistics\")  -> hosted + wired\n\n";
    engine.registerStaticModule("Logistics", std::move(logi),    ModuleSystemType::SEQUENTIAL, std::move(logiCfg));

    std::cout << "  frame | Cadence beats | Colony ticksSeen | Logistics reports (last tickCount)\n";
    std::cout << "  ------+---------------+------------------+-----------------------------------\n";

    // The ONLY per-frame call. Everything — process(), inbound message delivery,
    // cross-module routing — happens inside step().
    for (int f = 1; f <= 12; ++f) {
        engine.step(1.0f / 60.0f);
        std::cout << "   " << std::setw(4) << f
                  << "  |   " << std::setw(11) << C->beats()
                  << " |   " << std::setw(14) << O->ticksSeen()
                  << " |   " << std::setw(4) << L->reportsSeen()
                  << "  (tickCount=" << L->lastTicks() << ")\n";
    }

    std::cout << "\n";
    const bool ok = (C->beats() >= 12) && (O->ticksSeen() > 0) && (L->reportsSeen() > 0) && (L->lastTicks() > 0);
    if (ok) {
        std::cout << "  ✅ Cadence -> Colony -> Logistics flowed THROUGH the engine.\n";
        std::cout << "     The host called engine.step() and nothing else.\n";
    } else {
        std::cout << "  ❌ Cross-module flow did not complete — the engine is not hosting correctly.\n";
    }
    std::cout << "================================================================\n\n";

    engine.shutdown();
    return ok ? 0 : 1;
}
