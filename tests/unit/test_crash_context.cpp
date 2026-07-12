// ============================================================================
// CrashContextUnit — locks the pure crash-report payload (B1a).
//
// QUOI  : build a CrashContext (as the engine would fill it), then assert its JSON report + the
//         one-line summary carry the right fields — and crucially that the IIO message trail
//         preserves order (oldest -> newest), the diagnostic that makes this worth capturing.
//
// POURQUOI: this is the machine artifact written next to a minidump; a silent field drift (wrong
//           key, reordered message trail, missing module list) would waste a real field-crash
//           post-mortem. Pure data → fully testable without any crash machinery.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/crash/CrashContext.h>

using grove::crash::CrashContext;
using grove::crash::MessageTrace;

// Fill a context the way the engine would at crash time.
static CrashContext makeCtx() {
    CrashContext c;
    c.reason = "SIGSEGV";
    c.tick = 5678;
    c.simTime = 94.63;
    c.timeScale = 1.0;
    c.paused = false;
    c.frameCount = 1234;
    c.moduleNames = {"renderer", "physics"};
    // Three messages in causal order — the trail the crash handler would snapshot.
    c.recentMessages = {
        MessageTrace{"input:mouse", "input", 5676, 40, 900},
        MessageTrace{"physics:step", "physics", 5677, 12, 901},
        MessageTrace{"render:sprite", "renderer", 5678, 88, 902},
    };
    return c;
}

TEST_CASE("CrashContext: toJson carries the engine snapshot", "[crash][context]") {
    const auto j = grove::crash::toJson(makeCtx());
    REQUIRE(j.contains("grove_crash"));            // self-identifying root
    const auto& g = j["grove_crash"];

    REQUIRE(g["reason"] == "SIGSEGV");
    REQUIRE(g["frameCount"] == 1234);
    REQUIRE(g["clock"]["tick"] == 5678);
    REQUIRE(g["clock"]["paused"] == false);
    REQUIRE(g["modules"].size() == 2);
    REQUIRE(g["modules"][0] == "renderer");
    // Build stamp reflects the config this test was compiled under.
    REQUIRE(g["build"] == (grove::kDebugBuild ? "debug" : "shipping"));
}

TEST_CASE("CrashContext: the IIO message trail is ordered oldest->newest", "[crash][context]") {
    const auto j = grove::crash::toJson(makeCtx());
    const auto& msgs = j["grove_crash"]["recentMessages"];

    REQUIRE(msgs.size() == 3);
    // Order preserved: first captured is oldest, last is the most-implicated event.
    REQUIRE(msgs.front()["topic"] == "input:mouse");
    REQUIRE(msgs.back()["topic"] == "render:sprite");
    // Each carries its identity/ordering metadata (no payload).
    REQUIRE(msgs.back()["source"] == "renderer");
    REQUIRE(msgs.back()["seq"] == 88);
    REQUIRE(msgs.back()["lamport"] == 902);
}

TEST_CASE("CrashContext: summary is a one-line triage string", "[crash][context]") {
    const std::string s = grove::crash::summary(makeCtx());
    // Names the reason, frame, tick, module count, and the last (most-implicated) message.
    REQUIRE(s.find("reason='SIGSEGV'") != std::string::npos);
    REQUIRE(s.find("frame=1234") != std::string::npos);
    REQUIRE(s.find("tick=5678") != std::string::npos);
    REQUIRE(s.find("modules=2") != std::string::npos);
    REQUIRE(s.find("render:sprite@renderer") != std::string::npos);
}

TEST_CASE("CrashContext: empty message trail is safe", "[crash][context]") {
    CrashContext c;             // defaults: no messages, no modules
    c.reason = "EXCEPTION_ACCESS_VIOLATION";
    const auto j = grove::crash::toJson(c);
    REQUIRE(j["grove_crash"]["recentMessages"].empty());
    REQUIRE(grove::crash::summary(c).find("lastMsg=(none)") != std::string::npos);
}
