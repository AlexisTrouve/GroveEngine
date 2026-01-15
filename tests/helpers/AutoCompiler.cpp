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

bool AutoCompiler::compile(int iteration) {
    // Modify source version before compiling
    modifySourceVersion(iteration);

    // Small delay to ensure file is written
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Build the module using make
#ifdef _WIN32
    std::string makeCmd = "mingw32-make";
    std::string nullDev = "NUL";
#else
    std::string makeCmd = "make";
    std::string nullDev = "/dev/null";
#endif
    // Note: Tests run from build/tests/, so we use make -C .. to build from build directory
    std::string command;
    if (buildDir_ == "build") {
        command = makeCmd + " -C .. " + moduleName_ + " > " + nullDev + " 2>&1";
    } else {
        command = makeCmd + " -C " + buildDir_ + " " + moduleName_ + " > " + nullDev + " 2>&1";
    }
    int result = std::system(command.c_str());

    // std::system returns exit status in platform-specific format
    // WEXITSTATUS is the correct way to extract it on POSIX systems
    #ifdef _WIN32
        return (result == 0);
    #else
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
