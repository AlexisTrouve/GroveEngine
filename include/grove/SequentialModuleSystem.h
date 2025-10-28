#pragma once

#include <memory>
#include <string>
#include <queue>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "IModuleSystem.h"
#include "IModule.h"

using json = nlohmann::json;

namespace grove {

/**
 * @brief Sequential module system implementation for debug and testing
 *
 * SequentialModuleSystem processes modules one at a time in a simple, predictable manner.
 * Perfect for development, debugging, and testing scenarios where deterministic execution
 * is more important than performance.
 *
 * Features:
 * - Single-threaded execution (thread-safe by design)
 * - Immediate task execution (no actual scheduling)
 * - Comprehensive logging of all operations
 * - Simple state management
 * - Perfect for step-by-step debugging
 *
 * Task scheduling behavior:
 * - scheduleTask() executes immediately (no queue)
 * - hasCompletedTasks() always returns 0 (tasks complete immediately)
 * - getCompletedTask() throws (no queued results)
 */
class SequentialModuleSystem : public IModuleSystem {
private:
    std::shared_ptr<spdlog::logger> logger;
    std::unique_ptr<IModule> module;
    std::string moduleName = "unknown";

    // Performance tracking
    std::chrono::high_resolution_clock::time_point lastProcessTime;
    size_t processCallCount = 0;
    float totalProcessTime = 0.0f;
    float lastProcessDuration = 0.0f;

    // Task execution tracking (for logging purposes)
    size_t taskExecutionCount = 0;

    // Helper methods
    void logSystemStart();
    void logProcessStart(float deltaTime);
    void logProcessEnd(float processTime);
    void logTaskExecution(const std::string& taskType, const json& taskData);
    void validateModule() const;

public:
    SequentialModuleSystem();
    virtual ~SequentialModuleSystem();

    // IModuleSystem implementation
    void setModule(std::unique_ptr<IModule> module) override;
    IModule* getModule() const override;
    int processModule(float deltaTime) override;
    ModuleSystemType getType() const override;

    // Hot-reload support
    std::unique_ptr<IModule> extractModule();

    // ITaskScheduler implementation (inherited)
    void scheduleTask(const std::string& taskType, const json& taskData) override;
    int hasCompletedTasks() const override;
    json getCompletedTask() override;

    // Debug and monitoring methods
    json getPerformanceMetrics() const;
    void resetPerformanceMetrics();
    float getAverageProcessTime() const;
    size_t getProcessCallCount() const;
    size_t getTaskExecutionCount() const;

    // Configuration
    void setLogLevel(spdlog::level::level_enum level);
};

} // namespace grove