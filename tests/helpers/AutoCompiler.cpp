#include "AutoCompiler.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <cstdlib>

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

    // Build the module using CMake
    std::string command = "cmake --build " + buildDir_ + " --target " + moduleName_ + " 2>&1 > /dev/null";
    int result = std::system(command.c_str());

    return (result == 0);
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
