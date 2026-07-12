/**
 * SaveEngineE2E — whole-engine save/load through DebugEngine::saveState/loadState.
 *
 * Registers stateful modules on a real engine, mutates their state, saves to disk, then loads that file into a
 * SECOND engine with FRESH modules and asserts each module's state was restored (distinct per-module values, so
 * it's a real per-module round-trip, not a shared blob). Also checks fail-soft: a module absent from the save
 * keeps its state; loading a missing file returns false.
 */

#include <catch2/catch_test_macros.hpp>

#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/JsonDataNode.h>

#include <filesystem>
#include <memory>
#include <string>

using namespace grove;

namespace {

// A stateful module: an int `count` round-tripped through getState/setState.
class CounterModule : public IModule {
public:
    int count = 0;
    void setConfiguration(const IDataNode&, IIO*, ITaskScheduler*) override {}
    void process(const IDataNode&) override {}
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", nlohmann::json{{"count", count}});
    }
    void setState(const IDataNode& s) override {
        if (const auto* jn = dynamic_cast<const JsonDataNode*>(&s)) count = jn->getJsonData().value("count", 0);
    }
    const IDataNode& getConfiguration() override { static JsonDataNode c("c"); return c; }
    std::unique_ptr<IDataNode> getHealthStatus() override { return std::make_unique<JsonDataNode>("h"); }
    std::string getType() const override { return "Counter"; }
    bool isIdle() const override { return true; }
};

std::string tempPath(const std::string& tag) {
    return (std::filesystem::temp_directory_path() / ("grove_save_engine_" + tag + ".json")).string();
}

CounterModule* reg(DebugEngine& e, const std::string& name,
                   ModuleSystemType strategy = ModuleSystemType::SEQUENTIAL) {
    auto m = std::make_unique<CounterModule>();
    auto* ptr = m.get();
    e.registerStaticModule(name, std::move(m), strategy,
                           std::make_unique<JsonDataNode>("config"));
    return ptr;   // the engine owns it; the raw ptr stays valid for the test's assertions
}

} // namespace

TEST_CASE("SaveEngineE2E: saveState/loadState round-trips every module's state", "[integration][save][e2e]") {
    const auto path = tempPath("rt");
    std::filesystem::remove(path);

    // --- Engine A: two modules with DISTINCT states -> save. ---
    {
        DebugEngine engine;
        engine.initialize();
        CounterModule* alpha = reg(engine, "alpha");
        CounterModule* beta  = reg(engine, "beta");
        alpha->count = 7;
        beta->count  = 42;
        REQUIRE(engine.saveState(path));
        engine.shutdown();
    }
    REQUIRE(std::filesystem::exists(path));

    // --- Engine B: FRESH modules (count 0) + one extra not in the save -> load. ---
    {
        DebugEngine engine;
        engine.initialize();
        CounterModule* alpha = reg(engine, "alpha");
        CounterModule* beta  = reg(engine, "beta");
        CounterModule* gamma = reg(engine, "gamma");   // absent from the save
        gamma->count = 99;

        REQUIRE(alpha->count == 0);                    // fresh, before load
        REQUIRE(engine.loadState(path));

        REQUIRE(alpha->count == 7);                    // restored, distinct per module
        REQUIRE(beta->count == 42);
        REQUIRE(gamma->count == 99);                   // absent from save -> untouched (fail-soft)
        engine.shutdown();
    }

    std::filesystem::remove(path);
}

TEST_CASE("SaveEngineE2E: round-trips THREADED and THREAD_POOL module state", "[integration][save][e2e]") {
    // Prove-it-bites: before the threaded/pool support, saveState SKIPPED worker-hosted modules, so
    // the file wouldn't carry their state and the counts below would stay 0. The engine is never
    // step()ped, so the worker threads stay idle and the raw-ptr count access is race-free here (the
    // save/load path still snapshots under the module's processMutex).
    const auto path = tempPath("threaded");
    std::filesystem::remove(path);

    {
        DebugEngine engine;
        engine.initialize();
        CounterModule* t = reg(engine, "t", ModuleSystemType::THREADED);
        CounterModule* p = reg(engine, "p", ModuleSystemType::THREAD_POOL);
        t->count = 55;
        p->count = 77;
        REQUIRE(engine.saveState(path));
        engine.shutdown();
    }
    REQUIRE(std::filesystem::exists(path));

    {
        DebugEngine engine;
        engine.initialize();
        CounterModule* t = reg(engine, "t", ModuleSystemType::THREADED);
        CounterModule* p = reg(engine, "p", ModuleSystemType::THREAD_POOL);
        REQUIRE(t->count == 0);
        REQUIRE(p->count == 0);
        REQUIRE(engine.loadState(path));
        REQUIRE(t->count == 55);   // THREADED module state restored
        REQUIRE(p->count == 77);   // THREAD_POOL module state restored
        engine.shutdown();
    }

    std::filesystem::remove(path);
}

TEST_CASE("SaveEngineE2E: loadState fails soft on a missing file", "[integration][save][e2e]") {
    DebugEngine engine;
    engine.initialize();
    reg(engine, "alpha");
    REQUIRE_FALSE(engine.loadState(tempPath("nope_missing")));
    engine.shutdown();
}
