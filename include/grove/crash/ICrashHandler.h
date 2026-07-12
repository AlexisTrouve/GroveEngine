#pragma once

// ============================================================================
// ICrashHandler.h — the process-wide, last-resort crash hook (grove::crash).
//
// QUOI  : an interface for "on an unhandled crash, run a callback (write the diagnostic) + write a
//         native minidump, then let the process die". Behind the interface so the real OS backend
//         (Windows SEH + MiniDumpWriteDump) is swappable for a headless Mock in tests — the same
//         ISoundBackend / IVideoBackend pattern used across the engine.
//
// POURQUOI: you cannot attach a debugger to a player's crashed retail build; the only diagnostic
//         is what the process writes ON THE WAY DOWN. This is the hook that fires there. The
//         callback writes the engine-level context (grove::crash::CrashContext — the IIO trail is
//         the killer artifact); the backend writes the native minidump (stack/registers).
//
// COMMENT: the callback runs in a HOSTILE context (the process is already faulting) — keep it
//         minimal and best-effort; it is a diagnostic aid, not a guarantee. simulateCrash() is a
//         TEST HOOK that fires the callback WITHOUT faulting, so the install->callback contract is
//         unit-testable headlessly; a separate process-isolated test exercises the REAL fault path.
// ============================================================================

#include <functional>
#include <memory>
#include <string>

namespace grove {
namespace crash {

// Invoked on an unhandled crash with a human-readable reason (e.g. "EXCEPTION_ACCESS_VIOLATION").
// The callback writes the diagnostic (typically CrashContext::toJson to a file next to the dump).
// WARNING: runs in a crash context — do the minimum, catch nothing you can't afford to.
using CrashCallback = std::function<void(const std::string& reason)>;

class ICrashHandler {
public:
    virtual ~ICrashHandler() = default;

    // Where the real backend writes the native minidump (".dmp"). Set before install().
    virtual void setDumpPath(const std::string& path) = 0;

    // Install this as the process-wide unhandled-crash hook. On a crash it invokes onCrash(reason),
    // then writes the minidump, then lets the process terminate. Replaces any prior hook (restored
    // by uninstall()).
    virtual void install(CrashCallback onCrash) = 0;

    // Restore the previously-installed hook. Safe to call when not installed.
    virtual void uninstall() = 0;

    // TEST HOOK — fire the installed callback with `reason` WITHOUT actually faulting (no minidump).
    // Lets a headless test verify the install->callback path; real backends implement it too.
    virtual void simulateCrash(const std::string& reason) = 0;
};

// Factory: the platform's real handler — Windows today (SEH + MiniDumpWriteDump); other platforms
// get a no-op backend that still routes simulateCrash() (POSIX signal support is a follow-on).
std::unique_ptr<ICrashHandler> makeCrashHandler();

} // namespace crash
} // namespace grove
