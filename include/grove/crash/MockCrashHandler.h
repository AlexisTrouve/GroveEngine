#pragma once

// ============================================================================
// MockCrashHandler.h — headless test double for ICrashHandler (grove::crash).
//
// QUOI  : an ICrashHandler that installs NO OS hook and never faults — it just records the
//         install/uninstall/dump-path state and, on simulateCrash(), fires the callback exactly
//         as a real crash would.
//
// POURQUOI: lets the install->callback contract (and the engine wiring in B1c) be unit-tested with
//         zero platform machinery and zero risk of actually crashing the test process. The REAL
//         fault path is proven separately by a process-isolated test.
// ============================================================================

#include <string>
#include <utility>

#include <grove/crash/ICrashHandler.h>

namespace grove {
namespace crash {

class MockCrashHandler : public ICrashHandler {
public:
    // Observable state for assertions.
    std::string   dumpPath;
    bool          installed = false;
    int           simulateCount = 0;
    CrashCallback cb;

    void setDumpPath(const std::string& p) override { dumpPath = p; }
    void install(CrashCallback onCrash) override { cb = std::move(onCrash); installed = true; }
    void uninstall() override { installed = false; cb = nullptr; }

    // Fire the callback the same way a real crash would (no minidump, no fault).
    void simulateCrash(const std::string& reason) override {
        ++simulateCount;
        if (cb) cb(reason);
    }
};

} // namespace crash
} // namespace grove
