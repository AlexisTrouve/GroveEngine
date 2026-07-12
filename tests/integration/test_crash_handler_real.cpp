// ============================================================================
// CrashHandlerRealE2E — process-isolated proof the REAL crash handler fires (B1b).
//
// QUOI  : spawn the doomed `crash_child` process (which installs the real handler then null-derefs)
//         and assert it produced (1) a non-empty native minidump and (2) the callback's CrashContext
//         JSON marker naming the exception — i.e. the handler ran on a real fault.
//
// POURQUOI: a Mock test proves the wiring; only a real fault proves the OS hook + MiniDumpWriteDump
//           actually work. That fault must happen in a disposable child (it terminates the process),
//           so the check is inherently process-isolated (the LimitsTest lesson).
//
// COMMENT: Windows-only for now (the real backend is Windows; POSIX is a follow-on). The child is a
//         bare-main exe (no Catch2) so nothing intercepts its crash. Paths pass via env vars, which
//         the child inherits; CreateProcess (not system()) avoids cmd.exe quoting pitfalls.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX          // MinGW predefines NOMINMAX — guard to avoid a redefinition warning
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {
// Directory of THIS test exe — the child exe is built alongside it.
std::string exeDir() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf, n);
    auto slash = p.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string(".") : p.substr(0, slash);
}
} // namespace

TEST_CASE("real crash handler writes a minidump + fires the callback", "[crash][real]") {
    const std::string dir    = exeDir();
    const std::string dump   = dir + "\\grove_crashtest.dmp";
    const std::string marker = dir + "\\grove_crashtest.marker.json";
    const std::string child  = dir + "\\crash_child.exe";

    // Clean any stale artifacts so we only ever see THIS run's output.
    std::remove(dump.c_str());
    std::remove(marker.c_str());
    REQUIRE(std::ifstream(child).good());   // the child exe must be built alongside

    // The child reads its output paths from the environment (inherited across CreateProcess).
    _putenv_s("GROVE_CRASH_DUMP", dump.c_str());
    _putenv_s("GROVE_CRASH_MARKER", marker.c_str());

    STARTUPINFOA si;        ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::string cmd = "\"" + child + "\"";     // CreateProcessA may write to the cmd buffer
    BOOL ok = CreateProcessA(child.c_str(), cmd.data(), nullptr, nullptr,
                             TRUE /*inherit env*/, 0, nullptr, nullptr, &si, &pi);
    REQUIRE(ok);
    WaitForSingleObject(pi.hProcess, 30000);   // 30s guard against a hang
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    INFO("child exit code: 0x" << std::hex << code);   // expected: a crash code, not 0

    // 1. The callback ran in the crash context and wrote our CrashContext JSON marker.
    std::ifstream mk(marker);
    REQUIRE(mk.good());
    const std::string body((std::istreambuf_iterator<char>(mk)), std::istreambuf_iterator<char>());
    REQUIRE(body.find("grove_crash") != std::string::npos);
    REQUIRE(body.find("EXCEPTION_ACCESS_VIOLATION") != std::string::npos);

    // 2. The native minidump was written and is non-empty.
    std::ifstream df(dump, std::ios::binary | std::ios::ate);
    REQUIRE(df.good());
    REQUIRE(static_cast<long long>(df.tellg()) > 0);
}

#else

TEST_CASE("real crash handler E2E is Windows-only (POSIX backend is a follow-on)", "[crash][real][.]") {
    SUCCEED("skipped on non-Windows");
}

#endif // _WIN32
