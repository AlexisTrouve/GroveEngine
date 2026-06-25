// ============================================================================
// E2E: the WHOLE thing runs THREADED on the engine — a cross-module IIO chain
//      hosted via registerStaticModule(THREADED), driven by engine.step() only.
//
// WHAT: three modules (Producer -> Relay -> Sink) hosted by DebugEngine with the
//       THREADED strategy. Producer publishes "chain:a" every frame; Relay
//       subscribes "chain:a" and republishes "chain:b"; Sink subscribes "chain:b"
//       and counts. The whole pub/sub chain must flow across threads, delivered by
//       the engine's own loop (engine.step()), exactly as a static-linked game
//       would consume it with the threaded strategy.
//
// WHY:  the headless static-hosting + UI-on-engine locks (StaticEngineHosting,
//       IT_053) only exercise the SEQUENTIAL strategy. Nothing combined
//       registerStaticModule(THREADED) with a real cross-module IIO chain under
//       engine.step(). Per the doctrine: until a test actually drives it, threaded
//       hosting is "unverified", not "works". This is that lock.
//
//       Each module deliberately mutates its OWN integer state in BOTH its
//       subscribe handler AND its process() body. If the hosting model ever lets a
//       module's handler run on one thread while its process() runs on another
//       (the latent race the threading maps flagged), ThreadSanitizer flags it.
//       Run this binary under TSan (WSL: g++ -fsanitize=thread, launched via
//       `setarch $(uname -m) -R`) to make that proof real, not asserted.
//
// RUN:  ./test_threaded_hosting_e2e   (no GPU, no SDL, no .so — pure orchestration)
//       exit 0 = chain flowed; non-zero = a hop never delivered.
// ============================================================================

#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/IIO.h>
#include <grove/JsonDataNode.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

using namespace grove;

namespace {

// Shared IModule boilerplate (mirrors test_engine_hosting_demo's DemoModule).
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
        if (!io_) return;
        ++produced_;  // module state mutated on the worker thread
        io_->publish(outTopic_, std::make_unique<JsonDataNode>("m", nlohmann::json{{"n", produced_}}));
    }
    std::string getType() const override { return "ProducerModule"; }
    int produced() const { return produced_; }
private:
    std::string outTopic_;
    int produced_ = 0;
};

// Relay — middle of the chain. Subscribes "chain:a" (handler bumps a counter),
// and in process() republishes its running count on "chain:b".
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
    void process(const IDataNode&) override {}
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

    std::printf("================================================================\n");
    std::printf("  Threaded hosting E2E — Producer -> Relay -> Sink, THREADED\n");
    std::printf("================================================================\n");

    DebugEngine engine;
    engine.initialize();

    auto producer = std::make_unique<ProducerModule>();
    auto relay    = std::make_unique<RelayModule>();
    auto sink     = std::make_unique<SinkModule>();
    auto* P = producer.get();
    auto* R = relay.get();
    auto* S = sink.get();

    // Host all three with the THREADED strategy — each gets its own worker thread.
    engine.registerStaticModule("producer", std::move(producer), ModuleSystemType::THREADED,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"out", "chain:a"}}));
    engine.registerStaticModule("relay", std::move(relay), ModuleSystemType::THREADED,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"in", "chain:a"}, {"out", "chain:b"}}));
    engine.registerStaticModule("sink", std::move(sink), ModuleSystemType::THREADED,
        std::make_unique<JsonDataNode>("config", nlohmann::json{{"in", "chain:b"}}));

    // The ONLY per-frame call. process() (worker threads) + IIO delivery all happen
    // inside step(). A few hundred frames so the chain has time to flow end to end.
    const int frames = 300;
    for (int f = 0; f < frames; ++f) engine.step(1.0f / 60.0f);

    engine.shutdown();

    std::printf("  producer published : %d (frames=%d)\n", P->produced(), frames);
    std::printf("  relay received     : %d\n", R->relayed());
    std::printf("  sink received      : %d (last relayed=%d)\n", S->received(), S->lastRelayed());

    // The chain must have flowed end to end across the three worker threads.
    const bool ok = (P->produced() >= frames - 1)
                 && (R->relayed()  > 0)
                 && (S->received() > 0)
                 && (S->lastRelayed() > 0);

    std::printf(ok ? "  OK — threaded pub/sub chain flowed through the engine.\n"
                   : "  FAIL — a hop never delivered.\n");
    std::printf("================================================================\n");
    return ok ? 0 : 1;
}
