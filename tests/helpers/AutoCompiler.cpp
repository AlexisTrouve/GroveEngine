#include "AutoCompiler.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <cstdlib>
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
    // WHY CreateProcess instead of std::system():
    //   std::system() wraps the command in "cmd.exe /C ..." and provides no
    //   way to enforce a timeout. If ninja hangs (e.g. waiting for a lock file,
    //   or spawning a console dialog on an error), the thread blocks forever.
    //   CreateProcess + WaitForSingleObject lets us kill the child after
    //   COMPILE_TIMEOUT_MS and return failure gracefully.
    //
    // WHY redirect stdout/stderr to NUL:
    //   Build output is noisy and not useful for test verdicts.
    //   We silence it the same way the old std::system() code did.
    // -------------------------------------------------------------------------

    // Build the full command line for CreateProcess.
    // We redirect stdout and stderr to NUL by appending to the command string
    // passed through cmd.exe. Using cmd.exe /C also handles the redirection
    // operators (>) correctly on Windows.
    std::string fullCmd = "cmd.exe /C \"" + command + " > NUL 2>&1\"";

    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    // Inherit no console window — prevents any interactive dialog from
    // blocking on a headless CI system.
    DWORD creationFlags = CREATE_NO_WINDOW;

    // CreateProcess requires a mutable buffer for lpCommandLine
    std::vector<char> cmdBuf(fullCmd.begin(), fullCmd.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(
        nullptr,          // lpApplicationName — embedded in command line
        cmdBuf.data(),    // lpCommandLine
        nullptr,          // process security attributes
        nullptr,          // thread security attributes
        FALSE,            // bInheritHandles — no need to inherit
        creationFlags,
        nullptr,          // environment — inherit parent's
        nullptr,          // current directory — inherit parent's
        &si,
        &pi
    );

    if (!created) {
        // CreateProcess failure is a hard error — report it and return false
        // so compilationLoop counts it as a failure (not a crash).
        DWORD err = GetLastError();
        std::cerr << "[AutoCompiler] CreateProcess failed (error " << err
                  << ") for command: " << fullCmd << "\n";
        return false;
    }

    // Wait up to COMPILE_TIMEOUT_MS for the build to finish.
    DWORD waitResult = WaitForSingleObject(pi.hProcess, COMPILE_TIMEOUT_MS);

    bool success = false;

    if (waitResult == WAIT_TIMEOUT) {
        // Build tool exceeded timeout — kill it to unblock the thread.
        // WHY: A frozen ninja would otherwise hold the compilationThread
        // until the OS kills the whole test process (at CTest timeout).
        std::cerr << "[AutoCompiler] Build timed out after "
                  << (COMPILE_TIMEOUT_MS / 1000) << "s — killing process\n";
        TerminateProcess(pi.hProcess, 1);
        success = false;
    } else if (waitResult == WAIT_OBJECT_0) {
        // Process exited — check its exit code.
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        success = (exitCode == 0);
    } else {
        // WAIT_FAILED or unexpected return — treat as failure.
        std::cerr << "[AutoCompiler] WaitForSingleObject returned "
                  << waitResult << " (error " << GetLastError() << ")\n";
        success = false;
    }

    // Always close handles to avoid handle leak.
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
