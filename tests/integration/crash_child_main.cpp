// ============================================================================
// crash_child_main.cpp — the DOOMED child process for the real-crash E2E (B1b).
//
// QUOI  : a bare main() (NOT a Catch2 binary — so nothing intercepts the fault) that installs the
//         real crash handler, then deliberately dereferences null. On the fault the handler writes
//         a minidump to $GROVE_CRASH_DUMP and the callback writes a CrashContext JSON marker to
//         $GROVE_CRASH_MARKER. The parent test spawns this, waits, and asserts both files exist.
//
// POURQUOI: the ONLY honest proof a crash handler works is to actually crash and see it fire —
//         which must happen in a separate, disposable process (the LimitsTest isolation lesson).
// ============================================================================

#include <grove/crash/ICrashHandler.h>
#include <grove/crash/CrashContext.h>

#include <cstdlib>
#include <fstream>
#include <string>

int main() {
    const char* dump   = std::getenv("GROVE_CRASH_DUMP");
    const char* marker = std::getenv("GROVE_CRASH_MARKER");
    if (!dump || !marker) return 2;   // misconfigured — parent must set both

    auto handler = grove::crash::makeCrashHandler();
    handler->setDumpPath(dump);

    const std::string markerPath = marker;
    handler->install([markerPath](const std::string& reason) {
        // Runs IN the crash context: build the same CrashContext the engine would and write its
        // JSON. Proves both that the callback fired AND that the B1a report path works end-to-end.
        grove::crash::CrashContext ctx;
        ctx.reason = reason;
        ctx.frameCount = 7;
        ctx.moduleNames = {"child-mod"};
        std::ofstream(markerPath) << grove::crash::toJson(ctx).dump();
    });

    // Deliberate null dereference → hardware access violation → the installed filter fires.
    // volatile so the store can't be optimized away.
    volatile int* p = nullptr;
    *p = 42;

    return 0;   // unreachable
}
