// ============================================================================
// CrashHandlerMockUnit — locks the ICrashHandler install->callback contract (B1b, headless).
//
// QUOI  : with the Mock backend (no OS hook, no fault), verify install() wires the callback,
//         simulateCrash() fires it with the right reason, and uninstall() tears it down. Then
//         verify makeCrashHandler() returns a usable real backend whose simulateCrash() also
//         routes the callback (no fault, no dump).
//
// POURQUOI: the real fault path can only run in a doomed child process; this proves the
//           non-faulting half of the contract cheaply, on every platform, in-process.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <grove/crash/MockCrashHandler.h>
#include <grove/crash/ICrashHandler.h>

TEST_CASE("MockCrashHandler routes install -> simulateCrash -> callback", "[crash][handler]") {
    grove::crash::MockCrashHandler h;
    std::string got;

    h.setDumpPath("ignored.dmp");
    h.install([&](const std::string& reason) { got = reason; });
    REQUIRE(h.installed);
    REQUIRE(h.dumpPath == "ignored.dmp");

    h.simulateCrash("EXCEPTION_ACCESS_VIOLATION");
    REQUIRE(h.simulateCount == 1);
    REQUIRE(got == "EXCEPTION_ACCESS_VIOLATION");   // the callback saw the exact reason

    h.uninstall();
    REQUIRE_FALSE(h.installed);
    // After uninstall, the callback is dropped — a stray simulate must not re-fire it.
    got.clear();
    h.simulateCrash("late");
    REQUIRE(got.empty());
}

TEST_CASE("makeCrashHandler returns a handler that routes the callback", "[crash][handler]") {
    auto h = grove::crash::makeCrashHandler();
    REQUIRE(h != nullptr);

    std::string got;
    h->setDumpPath("ignored.dmp");
    h->install([&](const std::string& r) { got = r; });
    h->simulateCrash("test-reason");   // no real fault, no minidump — just the callback path
    REQUIRE(got == "test-reason");
    h->uninstall();
}
