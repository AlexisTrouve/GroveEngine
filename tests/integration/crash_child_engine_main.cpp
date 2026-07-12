// ============================================================================
// crash_child_engine_main.cpp — doomed child for the ENGINE crash-reporter E2E (B1c).
//
// QUOI  : a bare main() that builds a real DebugEngine, points its crash output at $GROVE_CRASH_BASE,
//         initialize()s it (which installs the crash reporter), steps a few frames, then null-derefs.
//         On the fault the engine's installed handler writes "<base>.json" (the CrashContext, with
//         live frameCount) + "<base>.dmp" (the minidump). The parent test asserts both.
//
// POURQUOI: proves the FULL wiring — initialize()->install->real crash->writeCrashReport->file — on
//         an actual fault, not a simulated one. Bare main (no Catch2) so nothing intercepts the crash.
// ============================================================================

#include <grove/DebugEngine.h>

#include <cstdlib>
#include <string>

int main() {
    const char* base = std::getenv("GROVE_CRASH_BASE");
    if (!base) return 2;   // parent must set the output base

    grove::DebugEngine engine;
    engine.setCrashOutputBase(base);   // BEFORE initialize() — that's when the handler is installed
    engine.initialize();

    engine.step(0.016f);
    engine.step(0.016f);
    engine.step(0.016f);               // frameCount -> 3, so the report proves LIVE state at crash

    // Real null dereference → the engine's crash handler fires → writes <base>.json + <base>.dmp.
    volatile int* p = nullptr;
    *p = 42;

    return 0;   // unreachable
}
