#pragma once
/**
 * CrashBacktrace — an in-process SEH backtrace on an unhandled crash (Windows diagnostic aid).
 *
 * WHY: gdb changes the process (exception hooks / heap layout) enough to make some load-dependent crashes
 *      vanish under it (a Heisenbug). An IN-PROCESS handler doesn't perturb the process, so it catches the real
 *      fault. We walk the stack with StackWalk64 (uses the PE unwind tables, present even in Release — no
 *      symbols needed) and print MODULE + OFFSET per frame. Symbolize offline with binutils addr2line (which
 *      reads MinGW's DWARF, unlike dbghelp):  addr2line -f -C -e <module> 0x<offset>
 *
 * Use: call installCrashHandler() at the top of main(). Link dbghelp. Build the target (and the .dll it loads)
 *      with -g so addr2line has line info. No-op on non-Windows.
 */
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>

inline LONG WINAPI groveCrashHandler(EXCEPTION_POINTERS* ep) {
    std::fprintf(stderr, "\n===== GROVE CRASH (code 0x%08lx, addr %p) — backtrace (module+offset) =====\n",
                 ep->ExceptionRecord->ExceptionCode,
                 (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, NULL, TRUE);   // for StackWalk64's table-access callbacks (not for names)

    CONTEXT ctx = *ep->ContextRecord;  // copy — StackWalk64 mutates it
    STACKFRAME64 f = {};
    f.AddrPC.Offset    = ctx.Rip; f.AddrPC.Mode    = AddrModeFlat;
    f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
    f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 48; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &f, &ctx,
                         NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
        const DWORD64 pc = f.AddrPC.Offset;
        if (pc == 0) break;

        HMODULE hmod = NULL;
        char modPath[MAX_PATH] = "?";
        const char* modName = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)pc, &hmod) && hmod) {
            GetModuleFileNameA(hmod, modPath, MAX_PATH);
            modName = std::strrchr(modPath, '\\'); modName = modName ? modName + 1 : modPath;
            std::fprintf(stderr, "  #%02d  %-28s +0x%llx\n", i, modName,
                         (unsigned long long)(pc - (DWORD64)hmod));
        } else {
            std::fprintf(stderr, "  #%02d  <no module>                 0x%llx\n", i, (unsigned long long)pc);
        }
    }
    std::fprintf(stderr, "===== end backtrace (symbolize: addr2line -f -C -e <module> 0x<offset>) =====\n");
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;   // still die
}

inline void installCrashHandler() { SetUnhandledExceptionFilter(groveCrashHandler); }
#else
inline void installCrashHandler() {}
#endif
