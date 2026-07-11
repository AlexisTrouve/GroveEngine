/**
 * test_doc_examples — a COMPILE-CHECK that the docs' load-bearing API examples still compile (quality Phase 4).
 *
 * WHAT : mirrors the core API-usage snippets shown in the developer docs (IIO pub/sub, engine hosting +
 *        IModule, JsonDataNode, the grove::fx authoring surface) and compiles them against the REAL headers.
 * WHY  : we shipped two stale doc examples this cycle (`pullMessage()`, `setChild(std::move(msg.data))`) — an
 *        API the docs show but the code no longer has. Prose isn't compiler-checked; this TU is. If a documented
 *        API is renamed/removed/re-signatured, THIS FILE STOPS COMPILING -> the normal build fails -> the doc is
 *        flagged before it ships. (Doctrine: doc accuracy is paramount.)
 * HOW  : each snippet is a function that USES the documented API exactly as the docs show it. main() never calls
 *        them (guarded by an always-false branch) — the VALUE is that they COMPILE, not that they run. So this is
 *        a fast, side-effect-free compile gate.
 *
 * COVERAGE (honest line): the CORE, GPU/SDL-free documented surfaces — IIO, DebugEngine hosting, IModule,
 * JsonDataNode, grove::fx (header-only authoring). NOT covered: GPU/renderer APIs (submitSpriteBatch, render:*
 * payloads — they need the BgfxRenderer link) and the module-level "fx:" / "scene:" topic wire formats (locked
 * by their own E2E tests). MAINTENANCE CONTRACT: when you add a load-bearing C++ API example to the docs, mirror
 * the call here so the compiler keeps the doc honest.
 */

#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/IIO.h>
#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>

#include "grove/fx/FxWorld.h"

#include <memory>
#include <string>

using namespace grove;

namespace doc {

// --- USER_GUIDE / DEVELOPER_GUIDE: IIO pub/sub basics ---------------------------------------------
void iio_pubsub() {
    auto& mgr = IntraIOManager::getInstance();
    auto pub = mgr.createInstance("doc_pub");
    auto sub = mgr.createInstance("doc_sub");

    sub->subscribe("some:topic", [](const Message& msg) {
        int layer = msg.data->getInt("layer", 0);
        std::string asset = msg.data->getString("asset", "");
        (void)layer; (void)asset;
    });

    pub->publish("some:topic", std::make_unique<JsonDataNode>("m",
        nlohmann::json{{"layer", 7}, {"asset", "tiles/grass"}}));

    while (sub->hasMessages() > 0) sub->pullAndDispatch();
    mgr.removeInstance("doc_pub");
    mgr.removeInstance("doc_sub");
}

// --- DEVELOPER_GUIDE: a minimal IModule (the interface a game module implements) ------------------
class DocModule : public IModule {
public:
    void setConfiguration(const IDataNode& /*c*/, IIO* io, ITaskScheduler* /*s*/) override { io_ = io; }
    void process(const IDataNode& input) override {
        double dt = input.getDouble("deltaTime", 0.016);
        (void)dt;
        if (io_) io_->publish("doc:out", std::make_unique<JsonDataNode>("m", nlohmann::json::object()));
    }
    void shutdown() override {}
    std::unique_ptr<IDataNode> getState() override {
        return std::make_unique<JsonDataNode>("state", nlohmann::json::object());
    }
    void setState(const IDataNode& /*s*/) override {}
    const IDataNode& getConfiguration() override {
        if (!cfg_) cfg_ = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
        return *cfg_;
    }
    std::unique_ptr<IDataNode> getHealthStatus() override {
        return std::make_unique<JsonDataNode>("health", nlohmann::json::object());
    }
    std::string getType() const override { return "DocModule"; }
    bool isIdle() const override { return true; }
private:
    IIO* io_ = nullptr;
    std::unique_ptr<IDataNode> cfg_;
};

// --- DEVELOPER_GUIDE: hosting a static module on the engine (registerStaticModule + step) ----------
void engine_hosting() {
    DebugEngine engine;
    engine.initialize();
    engine.registerStaticModule("doc", std::make_unique<DocModule>(), ModuleSystemType::SEQUENTIAL,
                                std::make_unique<JsonDataNode>("config", nlohmann::json::object()));
    for (int i = 0; i < 3; ++i) engine.step(1.0f / 60.0f);
    engine.shutdown();
}

// --- DEVELOPER_GUIDE: JsonDataNode construct + typed get/set --------------------------------------
void json_node() {
    JsonDataNode node("d", nlohmann::json{{"n", 3}, {"name", "hi"}, {"ratio", 0.5}});
    int n = node.getInt("n", 0);
    std::string name = node.getString("name", "");
    double ratio = node.getDouble("ratio", 0.0);
    node.setInt("n", n + 1);
    node.setString("name", name);
    node.setDouble("ratio", ratio);
}

// --- DEVELOPER_GUIDE "Effects / FX Layer": the grove::fx authoring API (header-only) --------------
void fx_authoring() {
    fx::FxWorld w;

    // Behavior factories (the fixed library).
    fx::EntityId spark = w.spawn();
    w.setSprite(spark, {true, "fx/spark", 0, 0xFFFFFFFFu, 900});
    w.setTransform(spark, {400.0f, 300.0f});              // cx,cy = CENTER
    w.addBehavior(spark, fx::move(10.0f, 0.0f));
    w.addBehavior(spark, fx::spin(45.0f));
    w.addBehavior(spark, fx::fade(0.5f));                 // fade-out
    w.addBehavior(spark, fx::velocity(100.0f, 0.0f, 2.0f));
    w.addBehavior(spark, fx::lifetime(0.8f));

    // Text component (floating damage numbers).
    fx::EntityId label = w.spawn();
    w.setText(label, {true, "-25", 0xFFFFFFFFu, 1000, 18});

    // Emitter component — burst + continuous stream factories.
    fx::EntityId boom = w.spawn();
    w.setEmitter(boom, fx::burstEmitter("spark", 24, 80.0f, 180.0f, 360.0f));
    fx::EntityId trail = w.spawn();
    w.setEmitter(trail, fx::streamEmitter("spark", 70.0f, 0.0f, 25.0f));

    // Prefabs (register once, spawn many).
    fx::Prefab explosion;
    explosion.emitter = fx::burstEmitter("spark", 24, 80.0f, 180.0f);
    w.registerPrefab("explosion", explosion);
    fx::EntityId inst = w.spawnFromPrefab("explosion");
    (void)inst;

    // Per-frame: tick + emit the retained-render diff.
    w.tick(1.0f / 60.0f);
    auto ops = w.diffRender();
    (void)ops;
}

} // namespace doc

int main(int argc, char**) {
    // The snippets must COMPILE; they need not run (this stays a fast, side-effect-free compile gate).
    if (argc < 0) {   // always false
        doc::iio_pubsub();
        doc::engine_hosting();
        doc::json_node();
        doc::fx_authoring();
    }
    return 0;
}
