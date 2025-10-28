#include <grove/SequentialModuleSystem.h>
#include <stdexcept>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace grove {

SequentialModuleSystem::SequentialModuleSystem() {
    // Create logger with file and console output
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/sequential_system.log", true);

    console_sink->set_level(spdlog::level::debug);
    file_sink->set_level(spdlog::level::trace);

    logger = std::make_shared<spdlog::logger>("SequentialModuleSystem",
        spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::debug);

    spdlog::register_logger(logger);

    logSystemStart();
    lastProcessTime = std::chrono::high_resolution_clock::now();
}

SequentialModuleSystem::~SequentialModuleSystem() {
    logger->info("🔧 SequentialModuleSystem destructor called");

    if (module) {
        logger->info("📊 Final performance metrics:");
        logger->info("   Total process calls: {}", processCallCount);
        logger->info("   Total process time: {:.2f}ms", totalProcessTime);
        logger->info("   Average process time: {:.3f}ms", getAverageProcessTime());
        logger->info("   Total task executions: {}", taskExecutionCount);
    }

    logger->trace("🏗️ SequentialModuleSystem destroyed");
}

void SequentialModuleSystem::setModule(std::unique_ptr<IModule> newModule) {
    logger->info("🔧 Setting module in SequentialModuleSystem");

    if (module) {
        logger->warn("⚠️ Replacing existing module '{}' with new module", moduleName);
        try {
            module->shutdown();
            logger->debug("✅ Previous module shut down successfully");
        } catch (const std::exception& e) {
            logger->error("❌ Error shutting down previous module: {}", e.what());
        }
    }

    if (!newModule) {
        logger->error("❌ Cannot set null module");
        throw std::invalid_argument("Cannot set null module");
    }

    module = std::move(newModule);

    // Get module type for better logging
    try {
        moduleName = module->getType();
        logger->info("✅ Module set successfully: type '{}'", moduleName);
    } catch (const std::exception& e) {
        logger->warn("⚠️ Could not get module type: {} - using 'unknown'", e.what());
        moduleName = "unknown";
    }

    // Reset performance metrics for new module
    resetPerformanceMetrics();
    logger->debug("📊 Performance metrics reset for new module");
}

IModule* SequentialModuleSystem::getModule() const {
    logger->trace("🔍 Module pointer requested");
    return module.get();
}

int SequentialModuleSystem::processModule(float deltaTime) {
    logProcessStart(deltaTime);

    auto processStartTime = std::chrono::high_resolution_clock::now();

    try {
        validateModule();

        // Create input JSON for module
        json moduleInput = {
            {"deltaTime", deltaTime},
            {"frameCount", processCallCount},
            {"system", "sequential"},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                processStartTime.time_since_epoch()).count()}
        };

        logger->trace("📥 Calling module process() with input: {}", moduleInput.dump());

        // Process the module
        module->process(moduleInput);

        processCallCount++;

        auto processEndTime = std::chrono::high_resolution_clock::now();
        lastProcessDuration = std::chrono::duration<float, std::milli>(processEndTime - processStartTime).count();
        totalProcessTime += lastProcessDuration;

        logProcessEnd(lastProcessDuration);

        // Check for performance warnings
        if (lastProcessDuration > 16.67f) { // More than 60fps budget
            logger->warn("🐌 Slow module processing: {:.2f}ms (target: <16.67ms for 60fps)", lastProcessDuration);
        }

        logger->trace("✅ Module processing completed successfully");
        return 0; // Success

    } catch (const std::exception& e) {
        logger->error("❌ Error processing module '{}': {}", moduleName, e.what());
        logger->error("🔍 Error occurred at frame {}, deltaTime: {:.3f}ms", processCallCount, deltaTime * 1000);

        auto processEndTime = std::chrono::high_resolution_clock::now();
        lastProcessDuration = std::chrono::duration<float, std::milli>(processEndTime - processStartTime).count();

        logProcessEnd(lastProcessDuration);

        return 1; // Error
    }
}

ModuleSystemType SequentialModuleSystem::getType() const {
    logger->trace("🏷️ ModuleSystem type requested: SEQUENTIAL");
    return ModuleSystemType::SEQUENTIAL;
}

void SequentialModuleSystem::scheduleTask(const std::string& taskType, const json& taskData) {
    logger->debug("⚙️ Task scheduled for immediate execution: '{}'", taskType);
    logTaskExecution(taskType, taskData);

    try {
        // In sequential system, tasks execute immediately
        // This is just a placeholder - real task execution would happen here
        logger->trace("🔧 Executing task '{}' immediately", taskType);

        // TODO: Implement actual task execution
        // For now, we just log and count
        taskExecutionCount++;

        logger->debug("✅ Task '{}' completed immediately", taskType);

    } catch (const std::exception& e) {
        logger->error("❌ Error executing task '{}': {}", taskType, e.what());
        throw;
    }
}

int SequentialModuleSystem::hasCompletedTasks() const {
    // Sequential system executes tasks immediately, so no completed tasks queue
    logger->trace("🔍 Completed tasks count requested: 0 (sequential execution)");
    return 0;
}

json SequentialModuleSystem::getCompletedTask() {
    logger->warn("⚠️ getCompletedTask() called on sequential system - no queued tasks");
    throw std::runtime_error("SequentialModuleSystem executes tasks immediately - no completed tasks queue");
}

json SequentialModuleSystem::getPerformanceMetrics() const {
    logger->debug("📊 Performance metrics requested");

    json metrics = {
        {"system_type", "sequential"},
        {"module_name", moduleName},
        {"process_calls", processCallCount},
        {"total_process_time_ms", totalProcessTime},
        {"average_process_time_ms", getAverageProcessTime()},
        {"last_process_time_ms", lastProcessDuration},
        {"task_executions", taskExecutionCount}
    };

    if (processCallCount > 0) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto totalRunTime = std::chrono::duration<float>(currentTime - lastProcessTime).count();
        metrics["total_runtime_seconds"] = totalRunTime;
        metrics["average_fps"] = totalRunTime > 0 ? processCallCount / totalRunTime : 0.0f;
    }

    logger->trace("📄 Metrics JSON: {}", metrics.dump());
    return metrics;
}

void SequentialModuleSystem::resetPerformanceMetrics() {
    logger->debug("📊 Resetting performance metrics");

    processCallCount = 0;
    totalProcessTime = 0.0f;
    lastProcessDuration = 0.0f;
    taskExecutionCount = 0;
    lastProcessTime = std::chrono::high_resolution_clock::now();

    logger->trace("✅ Performance metrics reset");
}

float SequentialModuleSystem::getAverageProcessTime() const {
    if (processCallCount == 0) return 0.0f;
    return totalProcessTime / processCallCount;
}

size_t SequentialModuleSystem::getProcessCallCount() const {
    return processCallCount;
}

size_t SequentialModuleSystem::getTaskExecutionCount() const {
    return taskExecutionCount;
}

void SequentialModuleSystem::setLogLevel(spdlog::level::level_enum level) {
    logger->info("🔧 Setting log level to: {}", spdlog::level::to_string_view(level));
    logger->set_level(level);
}

// Private helper methods
void SequentialModuleSystem::logSystemStart() {
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("⚙️ SEQUENTIAL MODULE SYSTEM INITIALIZED");
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("🎯 System Type: SEQUENTIAL (Debug/Test mode)");
    logger->info("🔧 Features: Immediate execution, comprehensive logging");
    logger->info("📊 Performance: Single-threaded, deterministic");
    logger->trace("🏗️ SequentialModuleSystem object created at: {}", static_cast<void*>(this));
}

void SequentialModuleSystem::logProcessStart(float deltaTime) {
    logger->trace("🎬 Process call {} START - deltaTime: {:.3f}ms, module: '{}'",
                 processCallCount, deltaTime * 1000, moduleName);
}

void SequentialModuleSystem::logProcessEnd(float processTime) {
    logger->trace("🏁 Process call {} END - processTime: {:.3f}ms", processCallCount, processTime);

    // Log performance summary every 60 calls
    if (processCallCount > 0 && processCallCount % 60 == 0) {
        logger->debug("📊 Performance summary (frame {}): Avg: {:.3f}ms, Total: {:.1f}ms",
                     processCallCount, getAverageProcessTime(), totalProcessTime);
    }
}

void SequentialModuleSystem::logTaskExecution(const std::string& taskType, const json& taskData) {
    logger->trace("⚙️ Task execution {} - type: '{}', data size: {} bytes",
                 taskExecutionCount + 1, taskType, taskData.dump().size());
    logger->trace("📄 Task data: {}", taskData.dump());
}

std::unique_ptr<IModule> SequentialModuleSystem::extractModule() {
    logger->info("🔓 Extracting module from system");

    if (!module) {
        logger->warn("⚠️ No module to extract");
        return nullptr;
    }

    auto extractedModule = std::move(module);
    moduleName = "unknown";

    logger->info("✅ Module extracted successfully");
    return extractedModule;
}

void SequentialModuleSystem::validateModule() const {
    if (!module) {
        logger->error("❌ No module set - cannot process");
        throw std::runtime_error("No module set in SequentialModuleSystem");
    }
}

} // namespace grove