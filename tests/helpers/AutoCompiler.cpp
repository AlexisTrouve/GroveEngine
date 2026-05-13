#include "AutoCompiler.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <vector>   // required for std::vector<char> mutable cmdLine buffer (CreateProcess)
#ifndef _WIN32
#include <sys/wait.h>
#endif

// WHY: On Windows, we use CreateProcess + WaitForSingleObject to run the build
// command with a hard timeout. std::system() spawns cmd.exe which can hang
// indefinitely (e.g. if ninja waits for stdin, or if PATH resolution fails
// silently). Using the Win32 API gives us control over the child process lifetime.
#ifdef _WIN32
#include <windows.h>
#endif

namespace TestHelpers {

AutoCompiler::AutoCompiler(const std::string& moduleName,
                           const std::string& buildDir,
                           const std::string& sourcePath)
    : moduleName_(moduleName)
    , buildDir_(buildDir)
    , sourcePath_(sourcePath)
{
}

AutoCompiler::~AutoCompiler() {
    stop();
}

void AutoCompiler::start(int iterations, int intervalMs) {
    if (running_.load()) {
        return; // Already running
    }

    running_ = true;
    compilationThread_ = std::thread(&AutoCompiler::compilationLoop, this, iterations, intervalMs);
}

void AutoCompiler::stop() {
    running_ = false;
    if (compilationThread_.joinable()) {
        compilationThread_.join();
    }
}

void AutoCompiler::waitForCompletion() {
    if (compilationThread_.joinable()) {
        compilationThread_.join();
    }
}

void AutoCompiler::modifySourceVersion(int iteration) {
    // Read entire file
    std::ifstream inFile(sourcePath_);
    if (!inFile.is_open()) {
        std::cerr << "[AutoCompiler] Failed to open source file: " << sourcePath_ << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << inFile.rdbuf();
    inFile.close();

    std::string content = buffer.str();

    // Replace version string: moduleVersion = "vX" → moduleVersion = "vITERATION"
    std::regex versionRegex(R"(std::string\s+moduleVersion\s*=\s*"v\d+")");
    std::string newVersion = "std::string moduleVersion = \"v" + std::to_string(iteration) + "\"";
    content = std::regex_replace(content, versionRegex, newVersion);

    // Write back to file
    std::ofstream outFile(sourcePath_);
    if (!outFile.is_open()) {
        std::cerr << "[AutoCompiler] Failed to write source file: " << sourcePath_ << std::endl;
        return;
    }

    outFile << content;
    outFile.close();
}

// Maximum time in milliseconds we allow a single ninja/make invocation to run.
// WHY: Without a timeout, a hung build tool will block the compilation thread
// forever, causing the whole test to freeze. 60 seconds is generous for a
// single-module incremental build.
static constexpr int COMPILE_TIMEOUT_MS = 60000;

bool AutoCompiler::compile(int iteration) {
    // Modify source version before compiling
    modifySourceVersion(iteration);

    // Small delay to ensure file is written
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // -------------------------------------------------------------------------
    // Determine the build tool command.
    //
    // WHY priority order:
    //   1. GROVE_MAKE_COMMAND — injected by CTest with the FULL PATH to the
    //      build tool (e.g. C:/msys64/usr/bin/ninja.exe). Using the full path
    //      bypasses any PATH lookup issues inside cmd.exe on Windows.
    //   2. "ninja" fallback — for manual runs where ninja is on PATH.
    //   3. "make"  fallback — for Linux manual runs.
    //
    // WHY NOT just "ninja" on Windows: cmd.exe spawned by std::system() uses
    // a different PATH than the shell that launched the test. The ninja binary
    // found during CMake configuration may not be on cmd.exe's PATH at all,
    // causing silent failure or indefinite hang waiting for a prompt.
    // -------------------------------------------------------------------------
#ifdef _WIN32
    const char* envMake = std::getenv("GROVE_MAKE_COMMAND");
    // Use the full path from CTest if available; fall back to bare "ninja" for
    // manual invocations where ninja is expected to be on PATH.
    std::string makeCmd = envMake ? envMake : "ninja";
    std::string nullDev = "NUL";
#else
    const char* envMake = std::getenv("GROVE_MAKE_COMMAND");
    std::string makeCmd = envMake ? envMake : "make";
    std::string nullDev = "/dev/null";
#endif

    // Note: Tests run from build/tests/, so we use -C .. to build from build/
    std::string command;
    if (buildDir_ == "build") {
        command = "\"" + makeCmd + "\" -C .. " + moduleName_;
    } else {
        command = "\"" + makeCmd + "\" -C " + buildDir_ + " " + moduleName_;
    }

#ifdef _WIN32
    // -------------------------------------------------------------------------
    // WHY CreateProcess instead of cmd.exe /C "..." :
    //   The original code built:
    //     cmd.exe /C "\"C:/path/ninja.exe\" -C .. Target > NUL 2>&1"
    //   cmd.exe sees the backslash-escaped inner quotes as literal backslashes,
    //   causing the error: '\' n'est pas reconnu...
    //
    //   The fix: skip cmd.exe entirely. Run ninja directly via CreateProcess
    //   with lpCommandLine starting with the quoted executable path. This is
    //   the standard Win32 way to launch a subprocess with space-containing paths.
    //
    //   Stdout/stderr redirection to NUL is done through STARTUPINFO handles
    //   rather than shell redirection operators — no shell = no quoting nightmare.
    //
    // WHY bInheritHandles = TRUE:
    //   We open a HANDLE to NUL and pass it via STARTUPINFO. For the child to
    //   receive those handles, bInheritHandles must be TRUE. We use
    //   CREATE_NO_WINDOW to keep the console hidden.
    // -------------------------------------------------------------------------

    // Determine the build directory relative to where the test is running.
    // Tests run from build/tests/, so buildDir_=="build" means one level up.
    std::string relBuildDir = (buildDir_ == "build") ? ".." : buildDir_;

    // Build the command line string that CreateProcess will parse.
    // Format: "C:/path/to/ninja.exe" -C ".." TargetName
    // Wrapping the executable path in quotes handles spaces (e.g. Program Files).
    // Wrapping relBuildDir in quotes handles paths with spaces as well.
    std::string cmdLine = "\"" + makeCmd + "\" -C \"" + relBuildDir + "\" " + moduleName_;

    // Open a handle to NUL so we can silence stdout and stderr.
    // WHY CreateFileA instead of a pre-opened stream: STARTUPINFO needs a HANDLE.
    // WHY SECURITY_ATTRIBUTES with bInheritHandle=TRUE: the handle must be inheritable
    // so that the child process (ninja) can actually write to it. Without this,
    // the child receives an invalid handle and its output goes nowhere (or errors).
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength              = sizeof(sa);
    sa.bInheritHandle       = TRUE;   // CRITICAL: child must inherit this handle
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hNull = CreateFileA(
        "NUL",
        GENERIC_WRITE,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        &sa,               // inheritable security attributes — CRITICAL for child to use it
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb       = sizeof(si);
    si.dwFlags  = STARTF_USESTDHANDLES;   // tell CreateProcess to honour our handle fields
    // Redirect stdout and stderr to NUL; if NUL open failed fall back to parent handles.
    si.hStdOutput = (hNull != INVALID_HANDLE_VALUE) ? hNull : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = (hNull != INVALID_HANDLE_VALUE) ? hNull : GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    // CREATE_NO_WINDOW: prevents any console window from flashing on the desktop
    // during the build, and avoids interactive prompts blocking on CI.
    DWORD creationFlags = CREATE_NO_WINDOW;

    // CreateProcess requires a mutable (non-const) buffer for lpCommandLine.
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(
        nullptr,       // lpApplicationName — null: let lpCommandLine supply the exe path
        cmdBuf.data(), // lpCommandLine — must be mutable; first token is the executable
        nullptr,       // process security attributes
        nullptr,       // thread security attributes
        TRUE,          // bInheritHandles — TRUE so the NUL HANDLE flows to the child
        creationFlags,
        nullptr,       // environment — inherit parent's
        nullptr,       // working directory — inherit parent's (build/tests/)
        &si,
        &pi
    );

    // Close our copy of the NUL handle immediately after CreateProcess.
    // WHY: The child already received an inherited copy. If we keep ours open
    // until after WaitForSingleObject we delay final close by the timeout duration.
    if (hNull != INVALID_HANDLE_VALUE) {
        CloseHandle(hNull);
    }

    if (!created) {
        // Hard failure — log and return false so compilationLoop counts it.
        DWORD err = GetLastError();
        std::cerr << "[AutoCompiler] CreateProcess failed (error " << err
                  << ") for: " << cmdLine << "\n";
        return false;
    }

    // Block until the build finishes or the timeout expires.
    DWORD waitResult = WaitForSingleObject(pi.hProcess, COMPILE_TIMEOUT_MS);
    bool success = false;

    if (waitResult == WAIT_TIMEOUT) {
        // Build tool is hung — kill it so the compilation thread can continue.
        // WHY: A frozen ninja (e.g. waiting on a .ninja_deps lock) would otherwise
        // hold this thread for the entire CTest timeout (often 300s).
        std::cerr << "[AutoCompiler] Build timed out after "
                  << (COMPILE_TIMEOUT_MS / 1000) << "s — killing process\n";
        TerminateProcess(pi.hProcess, 1);
        success = false;
    } else if (waitResult == WAIT_OBJECT_0) {
        // Process exited normally — inspect its exit code.
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        success = (exitCode == 0);
    } else {
        // WAIT_FAILED or other unexpected result — treat as failure.
        std::cerr << "[AutoCompiler] WaitForSingleObject returned "
                  << waitResult << " (error " << GetLastError() << ")\n";
        success = false;
    }

    // Always release handles — failure to do so leaks kernel objects.
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return success;

#else
    // -------------------------------------------------------------------------
    // POSIX path: std::system() is sufficient because:
    //   - SIGALRM or a wrapper script can be used if needed, but in practice
    //     Linux build environments don't hang the same way cmd.exe does.
    //   - The CTest timeout provides the last-resort safety net.
    // If Linux hangs become a concern, replace with posix_spawn + waitpid
    // with SIGKILL on timeout (same pattern as Windows above).
    // -------------------------------------------------------------------------
    std::string fullCmd = command + " > " + nullDev + " 2>&1";
    int result = std::system(fullCmd.c_str());

    // WEXITSTATUS extracts the actual exit code from the wait-status value
    // returned by std::system() on POSIX systems. On Windows this is not
    // needed — the value IS the exit code.
    return (WEXITSTATUS(result) == 0);
#endif
}

void AutoCompiler::compilationLoop(int iterations, int intervalMs) {
    for (int i = 1; i <= iterations && running_.load(); ++i) {
        currentIteration_ = i;

        // Compile
        bool success = compile(i);
        if (success) {
            successCount_++;
        } else {
            failureCount_++;
        }

        // Wait for next iteration
        if (i < iterations) {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }

    running_ = false;
}

} // namespace TestHelpers
