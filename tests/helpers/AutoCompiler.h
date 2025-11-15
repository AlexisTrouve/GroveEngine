#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <functional>

namespace TestHelpers {

/**
 * @brief Helper class to automatically compile a module repeatedly
 *
 * Designed to test race conditions during hot-reload by:
 * - Modifying source files to bump version numbers
 * - Triggering CMake builds repeatedly
 * - Running on a separate thread with configurable interval
 * - Tracking compilation success/failure rates
 */
class AutoCompiler {
public:
    /**
     * @param moduleName Name of the module to compile (e.g., "TestModule")
     * @param buildDir Path to build directory (e.g., "build")
     * @param sourcePath Path to the source file to modify (e.g., "tests/modules/TestModule.cpp")
     */
    AutoCompiler(const std::string& moduleName,
                 const std::string& buildDir,
                 const std::string& sourcePath);

    ~AutoCompiler();

    /**
     * @brief Start auto-compilation thread
     * @param iterations Total number of compilations to perform
     * @param intervalMs Milliseconds between each compilation
     */
    void start(int iterations, int intervalMs);

    /**
     * @brief Stop the compilation thread gracefully
     */
    void stop();

    /**
     * @brief Get current iteration number
     */
    int getCurrentIteration() const { return currentIteration_.load(); }

    /**
     * @brief Get number of successful compilations
     */
    int getSuccessCount() const { return successCount_.load(); }

    /**
     * @brief Get number of failed compilations
     */
    int getFailureCount() const { return failureCount_.load(); }

    /**
     * @brief Check if compilation thread is still running
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief Wait for all compilations to complete
     */
    void waitForCompletion();

private:
    /**
     * @brief Modify source file to change version number
     */
    void modifySourceVersion(int iteration);

    /**
     * @brief Compile the module using CMake
     * @return true if compilation succeeded
     */
    bool compile(int iteration);

    /**
     * @brief Main compilation loop (runs in separate thread)
     */
    void compilationLoop(int iterations, int intervalMs);

    std::string moduleName_;
    std::string buildDir_;
    std::string sourcePath_;

    std::atomic<int> currentIteration_{0};
    std::atomic<int> successCount_{0};
    std::atomic<int> failureCount_{0};
    std::atomic<bool> running_{false};

    std::thread compilationThread_;
};

} // namespace TestHelpers
