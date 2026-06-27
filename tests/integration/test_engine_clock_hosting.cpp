// ============================================================================
// test_engine_clock_hosting.cpp — E2E: the EngineClock reaches a hosted module
// and drives pause / slow-motion THROUGH the engine (IO contract part 1b + 1c).
//
// WHAT: a module hosted via IEngine::registerStaticModule() reads the engine's clock
//   inside process(); the host drives engine.step(dt) and controls time via engine.clock().
//   Asserts: (1b) the engine advances its clock each step, (1c) the SAME clock is injected
//   into the module (setClock), and pause / slow-mo applied by the host are seen by the module.
//
// WHY: part 1a (grove::EngineClock) is locked in isolation by test_engine_clock.cpp, but a
//   clock nobody advances or reads is inert. This is the proof it is actually WIRED: the
//   doctrine's "no test E2E = it doesn't exist" applied to engine time. Without this, "the
//   engine has a clock modules can read, with pause/slowmo" is an affirmation, not a fact.
//
// HOW: ClockReaderModule overrides setClock() (the 1c injection) + records what tick/simTime
//   it sees in process(). Driven SEQUENTIAL (engine thread, deterministic, one process()/step).
//   dt = 1/60 == the clock's fixed dt, so one normal step == one tick. Three phases: normal,
//   pause (sim frozen while step() keeps being called), slow-mo (fewer ticks than real frames).
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/DebugEngine.h>
#include <grove/EngineClock.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/IIO.h>
#include <grove/JsonDataNode.h>

#include <cstdint>
#include <memory>
#include <string>

using namespace grove;
using Catch::Matchers::WithinAbs;

namespace {

// Minimal IModule that records the engine clock it is handed. Boilerplate pure-virtuals
// are trivially implemented; only setClock() (1c injection) and process() carry meaning.
class ClockReaderModule : public IModule {
public:
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler* s) override {
        io_ = io; sched_ = s;
    }

    // 1c: the engine injects its authoritative clock here, once, at registration.
    void setClock(const EngineClock* clock) override { clock_ = clock; }

    // Read the injected clock every frame — this is what a time-aware module does.
    void process(const IDataNode&) override {
        ++processCalls_;
        if (!clock_) return;
        sawClock_   = true;
        lastTick_   = clock_->tick();
        lastSimTime_ = clock_->simTime();
    }

    std::string getType() const override { return "ClockReaderModule"; }

    // Observation accessors for the assertions.
    bool     sawClock()     const { return sawClock_; }
    uint64_t lastTick()     const { return lastTick_; }
    double   lastSimTime()  const { return lastSimTime_; }
    int      processCalls() const { return processCalls_; }

    // --- trivial lifecycle boilerplate (unused by this test) ---
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

private:
    const EngineClock* clock_ = nullptr;
    IIO* io_ = nullptr;
    ITaskScheduler* sched_ = nullptr;
    std::unique_ptr<IDataNode> cfg_;

    bool     sawClock_    = false;
    uint64_t lastTick_    = 0;
    double   lastSimTime_ = 0.0;
    int      processCalls_ = 0;
};

} // namespace

TEST_CASE("EngineClock reaches a hosted module and drives pause / slow-motion", "[engine][clock][static]") {
    DebugEngine engine;
    engine.initialize();

    auto reader = std::make_unique<ClockReaderModule>();
    auto* mod = reader.get();   // borrow to assert after the engine takes ownership
    engine.registerStaticModule("ClockReader", std::move(reader), ModuleSystemType::SEQUENTIAL, nullptr);

    constexpr float  DT     = 1.0f / 60.0f;
    constexpr double DT_SIM = 1.0 / 60.0;   // the clock's default fixed dt (double)

    // --- Phase A: normal time. Each step advances the clock one fixed dt; the module reads
    //     that same clock inside process(). ---
    for (int i = 0; i < 20; ++i) engine.step(DT);

    REQUIRE(mod->sawClock());                              // (1c) the engine injected the clock
    REQUIRE(mod->processCalls() >= 20);                    // process() ran every step
    REQUIRE(engine.clock().tick() >= 20u);                 // (1b) the engine advanced its clock
    REQUIRE(mod->lastTick() == engine.clock().tick());     // module saw THE engine clock, not a copy
    REQUIRE_THAT(mod->lastSimTime(),
                 WithinAbs(static_cast<double>(mod->lastTick()) * DT_SIM, 1e-9));  // simTime = tick*dt

    // --- Phase B: pause. The host freezes sim time through the engine; the module's view of
    //     time must stop even though step() keeps being called. ---
    const uint64_t tickAtPause = engine.clock().tick();
    engine.clock().pause();
    for (int i = 0; i < 10; ++i) engine.step(DT);

    REQUIRE(engine.clock().tick() == tickAtPause);         // sim frozen
    REQUIRE(mod->lastTick() == tickAtPause);               // module saw the freeze
    REQUIRE(engine.clock().realTime() > 0.0);              // but wall time kept flowing

    // --- Phase C: slow motion (half speed). 20 real frames must yield FEWER than 20 new ticks
    //     — the proof that a host-applied time scale reaches the hosted module. ---
    engine.clock().setTimeScale(0.5);
    const uint64_t tickAtSlowmo = engine.clock().tick();
    for (int i = 0; i < 20; ++i) engine.step(DT);
    const uint64_t slowmoTicks = engine.clock().tick() - tickAtSlowmo;

    REQUIRE(slowmoTicks > 0u);                             // time advanced...
    REQUIRE(slowmoTicks < 20u);                            // ...but slower than real frames
    REQUIRE(mod->lastTick() == engine.clock().tick());     // module still tracking the same clock

    engine.shutdown();
}
