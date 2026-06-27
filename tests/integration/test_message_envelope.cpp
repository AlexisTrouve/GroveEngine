// ============================================================================
// test_message_envelope.cpp — E2E for the message envelope (IO contract §5, part 2b/2c).
//
// WHAT: locks that the IIO transport STAMPS an envelope (source / seq / lamport / tick / simTime)
//   on every delivered control-plane message, and that it carries the documented semantics:
//     - source  = the publisher's instance id
//     - seq     = monotonic per-source sequence (gap detection / dedup)
//     - lamport = a per-node logical clock obeying the receive rule (causal order across a chain)
//     - tick/simTime = the engine-pushed clock snapshot (so a module — or a remote one — reads
//       the tick's time off the message without holding the EngineClock)
//
// WHY: the envelope is the foundation of replay/debug ordering under a non-deterministic engine.
//   "No E2E = it doesn't exist": these cases prove the metadata is actually stamped through the
//   real publish -> route -> deliver path (transport cases) AND through the engine's step() wire
//   (the engine case), not just that the structs exist.
//
// HOW: transport cases drive raw IntraIO instances through the IntraIOManager singleton (publish,
//   pull, inspect Message::env). The engine case hosts a publisher + a receiver module and drives
//   engine.step(), asserting the received envelope's tick equals the engine's clock tick.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/DebugEngine.h>
#include <grove/IModule.h>
#include <grove/IModuleSystem.h>
#include <grove/JsonDataNode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace grove;
using Catch::Matchers::WithinAbs;

namespace {
std::unique_ptr<IDataNode> emptyNode() {
    return std::make_unique<JsonDataNode>("d", nlohmann::json::object());
}
} // namespace

// ============================================================================
// Transport: source / seq stamping
// ============================================================================

TEST_CASE("Envelope - transport stamps source + monotonic per-source seq", "[envelope][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    auto a = mgr.createInstance("ModA");
    auto b = mgr.createInstance("ModB");

    std::vector<Envelope> received;
    b->subscribe("env:topic", [&](const Message& m) { received.push_back(m.env); });

    for (int i = 0; i < 3; ++i) a->publish("env:topic", emptyNode());
    while (b->hasMessages() > 0) b->pullAndDispatch();

    REQUIRE(received.size() == 3);
    REQUIRE(received[0].source == "ModA");           // source = publisher id
    REQUIRE(received[1].source == "ModA");
    REQUIRE(received[2].source == "ModA");
    REQUIRE(received[0].seq == 1u);                  // per-source seq starts at 1, monotonic
    REQUIRE(received[1].seq == 2u);
    REQUIRE(received[2].seq == 3u);
    REQUIRE(received[0].lamport < received[1].lamport);   // lamport strictly increases on sends
    REQUIRE(received[1].lamport < received[2].lamport);

    mgr.removeInstance("ModA");
    mgr.removeInstance("ModB");
}

// ============================================================================
// Transport: Lamport causality across a publish chain
// ============================================================================

TEST_CASE("Envelope - Lamport obeys the receive rule across an A->B->A chain", "[envelope][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    auto a = mgr.createInstance("CA");
    auto b = mgr.createInstance("CB");

    uint64_t lamportAtoB = 0;
    uint64_t lamportBtoA = 0;

    // B reacts to A's message by publishing back to A — the causal chain A -> B -> A.
    b->subscribe("chain:a2b", [&](const Message& m) {
        lamportAtoB = m.env.lamport;
        b->publish("chain:b2a", emptyNode());
    });
    a->subscribe("chain:b2a", [&](const Message& m) { lamportBtoA = m.env.lamport; });

    a->publish("chain:a2b", emptyNode());
    while (b->hasMessages() > 0) b->pullAndDispatch();   // B receives (updates its clock) + replies
    while (a->hasMessages() > 0) a->pullAndDispatch();   // A receives the reply

    REQUIRE(lamportAtoB > 0u);
    // B folded A's stamp into its clock on receive, so B's reply is stamped strictly AFTER the
    // message that caused it — the load-bearing causality guarantee.
    REQUIRE(lamportBtoA > lamportAtoB);

    mgr.removeInstance("CA");
    mgr.removeInstance("CB");
}

// ============================================================================
// Transport: tick/simTime come from the pushed engine snapshot
// ============================================================================

TEST_CASE("Envelope - tick/simTime are stamped from the engine clock snapshot", "[envelope][iio]") {
    auto& mgr = IntraIOManager::getInstance();
    auto a = mgr.createInstance("TA");
    auto b = mgr.createInstance("TB");

    Envelope got;
    bool gotOne = false;
    b->subscribe("t:topic", [&](const Message& m) { got = m.env; gotOne = true; });

    // Stand in for the engine pushing this frame's clock snapshot.
    mgr.setSimTime(/*tick=*/4096, /*simTime=*/4096.0 / 60.0);
    a->publish("t:topic", emptyNode());
    while (b->hasMessages() > 0) b->pullAndDispatch();

    REQUIRE(gotOne);
    REQUIRE(got.tick == 4096u);
    REQUIRE_THAT(got.simTime, WithinAbs(4096.0 / 60.0, 1e-9));

    // A fresh snapshot is reflected on the next publish (stamped at route time, not subscribe time).
    mgr.setSimTime(5000, 5000.0 / 60.0);
    a->publish("t:topic", emptyNode());
    while (b->hasMessages() > 0) b->pullAndDispatch();
    REQUIRE(got.tick == 5000u);

    mgr.removeInstance("TA");
    mgr.removeInstance("TB");
}

// ============================================================================
// Engine wire: step() pushes the snapshot that the envelope is stamped from
// ============================================================================

namespace {

// Minimal IModule base — trivial lifecycle, so the two test modules below define only what matters.
class EnvBaseModule : public IModule {
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

// Publishes one message per process().
class PubModule : public EnvBaseModule {
public:
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler* s) override { io_ = io; sched_ = s; }
    void process(const IDataNode&) override {
        if (io_) io_->publish("ev:ping", std::make_unique<JsonDataNode>("d", nlohmann::json::object()));
    }
    std::string getType() const override { return "PubModule"; }
};

// Receives ev:ping (engine pumps its inbox) and records the envelope's tick.
class SubModule : public EnvBaseModule {
public:
    void setConfiguration(const IDataNode&, IIO* io, ITaskScheduler* s) override {
        io_ = io; sched_ = s;
        io_->subscribe("ev:ping", [this](const Message& m) { lastTick_ = m.env.tick; ++seen_; });
    }
    void process(const IDataNode&) override {}
    std::string getType() const override { return "SubModule"; }
    uint64_t lastTick() const { return lastTick_; }
    int seen() const { return seen_; }
private:
    uint64_t lastTick_ = 0;
    int seen_ = 0;
};

} // namespace

TEST_CASE("Envelope - engine.step() stamps the received message with the engine's tick", "[envelope][engine][static]") {
    DebugEngine engine;
    engine.initialize();

    auto pub = std::make_unique<PubModule>();
    auto sub = std::make_unique<SubModule>();
    auto* subPtr = sub.get();
    engine.registerStaticModule("Pub", std::move(pub), ModuleSystemType::SEQUENTIAL, nullptr);
    engine.registerStaticModule("Sub", std::move(sub), ModuleSystemType::SEQUENTIAL, nullptr);

    for (int i = 0; i < 10; ++i) engine.step(1.0f / 60.0f);

    REQUIRE(subPtr->seen() > 0);                                  // cross-module delivery happened
    REQUIRE(subPtr->lastTick() > 0u);                             // a real tick was stamped...
    REQUIRE(subPtr->lastTick() == engine.clock().tick());         // ...and it IS the engine's tick

    engine.shutdown();
}
