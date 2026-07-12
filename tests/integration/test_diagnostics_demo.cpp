// ============================================================================
// DiagnosticsDemo — a runnable reference for the engine's diagnostics layer.
//
// QUOI  : walks a game dev through the four diagnostics tools end to end and PRINTS what each one
//         produces — a memory leak report, a per-frame profile, and a crash-context report (with the
//         IIO message trail). Doubles as a guard: it asserts the reports carry real content, so the
//         DEVELOPER_GUIDE "Diagnostics" snippets can't rot.
//
// POURQUOI: we built the crash reporter / grove::mem / grove::profile / the IIO guard this cycle — a
//         tool nobody knows how to turn on is half-shipped. Run this (`./test_diagnostics_demo`) to
//         SEE the output format the docs describe. Referenced from DEVELOPER_GUIDE > Diagnostics.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/mem/Tracker.h>
#include <grove/profile/ProfileZone.h>
#include <grove/crash/CrashContext.h>
#include <grove/DebugEngine.h>
#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>

#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>

using namespace grove;

TEST_CASE("DiagnosticsDemo: memory leak report by tag", "[diagnostics][demo]") {
    std::cout << "\n--- grove::mem leak report ---\n";
    mem::Tracker t;
    int a, b, c;                              // three fake allocations
    t.onAlloc(&a, 64,  "iio:jsonnode");
    t.onAlloc(&b, 128, "iio:jsonnode");
    t.onAlloc(&c, 4096, "sprite:batch");
    t.onFree(&b);                             // b freed; a + c "leak"

    std::cout << t.report().dump(2) << "\n";  // what a leak hunt prints
    const auto j = t.report();
    REQUIRE(j["grove_mem"]["liveCount"] == 2);
    REQUIRE(j["grove_mem"]["byTag"]["sprite:batch"]["bytes"] == 4096);   // the biggest leaker
}

TEST_CASE("DiagnosticsDemo: per-frame profile", "[diagnostics][demo]") {
    std::cout << "\n--- grove::profile per-frame breakdown ---\n";
    profile::profiler().reset();
    {
        GROVE_PROFILE_ZONE("demo:work");
        volatile double acc = 0.0;
        for (int i = 0; i < 500000; ++i) acc += i * 0.5;
        (void)acc;
    }
    std::cout << profile::profiler().report().dump(2) << "\n";
#if GROVE_DEBUG
    REQUIRE(profile::profiler().zone("demo:work").count == 1);
#endif
    profile::profiler().reset();
}

TEST_CASE("DiagnosticsDemo: crash context with the IIO trail", "[diagnostics][demo]") {
    std::cout << "\n--- grove::crash CrashContext (non-fatal snapshot) ---\n";
    spdlog::drop("DebugEngine");
    auto& mgr = IntraIOManager::getInstance();
    mgr.enableReplaySink(200);                // capture the last-200 IIO messages for the crash trail

    // A bit of real IIO traffic so the trail isn't empty.
    auto src = mgr.createInstance("demo_src");
    auto snk = mgr.createInstance("demo_snk");
    snk->subscribe("demo:evt", [](const Message&) {});
    src->publish("demo:evt", std::make_unique<JsonDataNode>("m", nlohmann::json::object()));
    src->publish("demo:evt", std::make_unique<JsonDataNode>("m", nlohmann::json::object()));

    DebugEngine engine;
    engine.initialize();
    engine.step(0.016f);
    engine.step(0.016f);                      // frameCount -> 2

    const crash::CrashContext ctx = engine.snapshotCrashContext("manual");
    std::cout << "summary: " << crash::summary(ctx) << "\n";
    std::cout << crash::toJson(ctx).dump(2) << "\n";

    REQUIRE(ctx.frameCount == 2);
    REQUIRE_FALSE(ctx.recentMessages.empty());   // the IIO trail was captured
    bool sawEvt = false;
    for (const auto& m : ctx.recentMessages) if (m.topic == "demo:evt") sawEvt = true;
    REQUIRE(sawEvt);

    mgr.disableReplaySink();
    engine.shutdown();
}
