// ============================================================================
// CrashHandler.cpp — the real (platform) ICrashHandler backends + factory (grove::crash).
//
// QUOI  : WindowsCrashHandler (SetUnhandledExceptionFilter + MiniDumpWriteDump) for the real
//         crash hook; NoopCrashHandler for platforms without a backend yet; makeCrashHandler()
//         picks the right one. The handler classes are file-private — only ICrashHandler +
//         makeCrashHandler() are public.
//
// POURQUOI: this is the OS-specific half of the crash reporter. It's isolated in one leaf .cpp so
//         windows.h / dbghelp.h never leak into the rest of the engine, and so a non-Windows build
//         simply compiles the Noop path (no #ifdef soup elsewhere).
//
// COMMENT: on Windows the unhandled-exception filter writes the callback's diagnostic FIRST (the
//         engine context) then the minidump, then returns EXECUTE_HANDLER so the process
//         terminates quietly (no WER dialog) — appropriate for an unattended/shipping process.
//         The filter is a free function, so the active handler is reached through a single static
//         instance pointer (one handler installed at a time, which install()/uninstall() enforce).
// ============================================================================

#include <grove/crash/ICrashHandler.h>

#include <memory>
#include <utility>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX          // MinGW predefines NOMINMAX — guard to avoid a redefinition warning
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <dbghelp.h>
#else
    #include <logger/Logger.h>   // Noop path logs a one-time warning
#endif

namespace grove {
namespace crash {

namespace {

#ifdef _WIN32

// Map the common hardware/exception codes to a stable human string for the report `reason`.
const char* reasonFromCode(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:    return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW:      return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:  return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_PRIV_INSTRUCTION:    return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:       return "EXCEPTION_IN_PAGE_ERROR";
        default:                            return "EXCEPTION_UNKNOWN";
    }
}

class WindowsCrashHandler : public ICrashHandler {
public:
    ~WindowsCrashHandler() override { uninstall(); }

    void setDumpPath(const std::string& p) override { dumpPath_ = p; }

    void install(CrashCallback onCrash) override {
        cb_ = std::move(onCrash);
        s_instance = this;                                       // route the free-function filter here
        prev_ = SetUnhandledExceptionFilter(&WindowsCrashHandler::filter);
    }

    void uninstall() override {
        if (s_instance == this) {
            SetUnhandledExceptionFilter(prev_);
            s_instance = nullptr;
            prev_ = nullptr;
        }
    }

    void simulateCrash(const std::string& reason) override { if (cb_) cb_(reason); }

private:
    // The OS calls this on an unhandled exception. Writes diagnostics, then terminates the process.
    static LONG WINAPI filter(EXCEPTION_POINTERS* ep) {
        if (s_instance) s_instance->handle(ep);
        return EXCEPTION_EXECUTE_HANDLER;   // we've captured what we need → terminate (no WER dialog)
    }

    void handle(EXCEPTION_POINTERS* ep) {
        const char* reason = reasonFromCode(
            (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0u);
        // 1. engine context first (the IIO trail etc.) — best-effort, must not throw out of here.
        if (cb_) { try { cb_(reason); } catch (...) {} }
        // 2. native minidump (stack / registers / loaded modules).
        writeDump(ep);
    }

    void writeDump(EXCEPTION_POINTERS* ep) {
        HANDLE hFile = CreateFileA(dumpPath_.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return;

        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers     = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpNormal, ep ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(hFile);
    }

    CrashCallback                   cb_;
    std::string                     dumpPath_ = "crash.dmp";
    LPTOP_LEVEL_EXCEPTION_FILTER    prev_ = nullptr;
    static WindowsCrashHandler*     s_instance;
};

WindowsCrashHandler* WindowsCrashHandler::s_instance = nullptr;

#else  // !_WIN32

// No OS backend yet (POSIX signal handler is a follow-on). The callback still routes (so the
// wiring is testable), but a real fault is NOT captured — warn once so that's never a silent gap.
class NoopCrashHandler : public ICrashHandler {
public:
    void setDumpPath(const std::string& p) override { dumpPath_ = p; }
    void install(CrashCallback onCrash) override {
        cb_ = std::move(onCrash);
        static auto lg = stillhammer::createDomainLogger("CrashHandler", "engine");
        lg->warn("⚠️ Crash handler: no OS backend on this platform (POSIX support is a follow-on) — "
                 "real faults will NOT be captured");
    }
    void uninstall() override { cb_ = nullptr; }
    void simulateCrash(const std::string& reason) override { if (cb_) cb_(reason); }
private:
    CrashCallback cb_;
    std::string   dumpPath_;
};

#endif // _WIN32

} // namespace

std::unique_ptr<ICrashHandler> makeCrashHandler() {
#ifdef _WIN32
    return std::make_unique<WindowsCrashHandler>();
#else
    return std::make_unique<NoopCrashHandler>();
#endif
}

} // namespace crash
} // namespace grove
