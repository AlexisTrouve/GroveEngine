// ============================================================================
// DebugGateE2E — proves DebugEngine::step()'s per-frame logging is GROVE_DEBUG-gated (A1).
//
// QUOI  : run the REAL engine's step() and count how many log records it emits, via a spy sink
//         attached to the engine's own "DebugEngine" logger. Debug build → step() emits its
//         per-frame frame-start / health / frame-end logs (count > 0); shipping build
//         (GROVE_DEBUG=OFF) → those call sites are compiled out, so step() emits nothing (== 0).
//
// POURQUOI: A0's BuildConfigUnit proves the GROVE_DEBUG_ONLY mechanism in isolation; this proves
//         step() actually APPLIES it on the real engine. It bites a regression where someone
//         removes the #if guards (shipping count jumps > 0) or over-gates the prod path (debug
//         count drops to 0). Same two-config discipline as BuildConfigUnit: compiled under both,
//         the assertion is #if-selected so the file passes in whichever config runs it.
//
// COMMENT: createDomainLogger registers the logger under the name "DebugEngine" (Logger.cpp), so
//         spdlog::get("DebugEngine") reaches the exact instance step() logs through. We REPLACE
//         its sinks with a single counting sink at trace level (and lower the logger level to
//         trace) so the count is clean + noise-free. The engine is constructed with no sockets /
//         no modules, so in a debug build the ONLY things step() can emit are the gated per-frame
//         logs — making count a direct measure of the gate.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/DebugEngine.h>
#include <grove/BuildConfig.h>
#include <grove/JsonDataNode.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <atomic>
#include <mutex>

namespace {

// Minimal spy sink: counts every log record routed to it (thread-safe via base_sink's mutex).
class CountingSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    std::atomic<int> count{0};
protected:
    void sink_it_(const spdlog::details::log_msg&) override { count.fetch_add(1); }
    void flush_() override {}
};

} // namespace

TEST_CASE("DebugEngine::step() per-frame logging is GROVE_DEBUG-gated", "[debuggate][config]") {
    // Fresh registry slot: a prior construction in this process would make the engine's
    // register_logger throw on a duplicate name. Dropping first makes the test order-independent.
    spdlog::drop("DebugEngine");

    grove::DebugEngine engine;  // constructor registers the "DebugEngine" logger

    // Attach the spy: replace the engine logger's sinks with our counter, open the level to trace
    // so nothing is filtered before it reaches us. This is the exact instance step() logs through.
    auto spy = std::make_shared<CountingSink>();
    spy->set_level(spdlog::level::trace);
    auto lg = spdlog::get("DebugEngine");
    REQUIRE(lg != nullptr);            // the engine must have registered it
    lg->sinks().clear();
    lg->sinks().push_back(spy);
    lg->set_level(spdlog::level::trace);

    // Drive two frames of the real engine (no sockets, no modules → the only possible emissions
    // are the per-frame debug logs). Frame 0 also crosses the every-30-frames health check.
    engine.step(0.016f);
    engine.step(0.016f);

    const int emitted = spy->count.load();

#if GROVE_DEBUG
    // Debug build: the frame-start / health / frame-end logging ran.
    REQUIRE(emitted > 0);
#else
    // Shipping build: every per-frame log call site was compiled out — step() is silent.
    REQUIRE(emitted == 0);
#endif
}

// A2: getDetailedStatus() is introspection — a rich internal snapshot in debug, stripped to a
// minimal marker node in a shipping build. Proves the method-body gate (not just logging) bites.
TEST_CASE("DebugEngine::getDetailedStatus is GROVE_DEBUG-gated", "[debuggate][config]") {
    spdlog::drop("DebugEngine");  // order-independent (see the note in the first case)
    grove::DebugEngine engine;

    auto node = engine.getDetailedStatus();
    REQUIRE(node != nullptr);
    auto* jn = dynamic_cast<grove::JsonDataNode*>(node.get());
    REQUIRE(jn != nullptr);
    const auto& j = jn->getJsonData();

#if GROVE_DEBUG
    // Debug: the full internal snapshot (frame count, module count, sockets, ...).
    REQUIRE(j.contains("frame_count"));
    REQUIRE(j.contains("modules_loaded"));
#else
    // Shipping: the body is compiled out — only the minimal "stripped" marker remains.
    REQUIRE(j.contains("introspection"));
    REQUIRE(j["introspection"] == "stripped");
    REQUIRE_FALSE(j.contains("frame_count"));
#endif
}
