#include <grove/SequentialModuleSystem.h>
#include <grove/JsonDataNode.h>
#include <stdexcept>
#include <logger/Logger.h>

namespace grove {

SequentialModuleSystem::SequentialModuleSystem() {
    logger = stillhammer::createDomainLogger("SequentialModuleSystem", "engine");

    logSystemStart();
    lastProcessTime = std::chrono::high_resolution_clock::now();
}

SequentialModuleSystem::~SequentialModuleSystem() {
    // IMPORTANT: During static destruction order on Windows (especially MinGW GCC 15),
    // spdlog's registry may be destroyed BEFORE this destructor runs.
    // Using a destroyed logger causes STATUS_STACK_BUFFER_OVERRUN (0xc0000409).
    // We must check if our logger is still valid before using it.
    bool loggerValid = false;
    try {
        // Check if spdlog registry still exists and our logger is registered
        loggerValid = logger && spdlog::get(logger->name()) != nullptr;
    } catch (...) {
        // spdlog registry may throw during destruction
        loggerValid = false;
    }

    if (loggerValid) {
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

    // Explicitly reset module before logger destruction to ensure proper cleanup order
    module.reset();
}

// IModuleSystem implementation
void SequentialModuleSystem::registerModule(const std::string& name, std::unique_ptr<IModule> newModule) {
    logger->info("🔧 Registering module '{}' in SequentialModuleSystem", name);

    if (module) {
        logger->warn("⚠️ Replacing existing module '{}' with '{}'", moduleName, name);
        try {
            module->shutdown();
            logger->debug("✅ Previous module shut down successfully");
        } catch (const std::exception& e) {
            logger->error("❌ Error shutting down previous module: {}", e.what());
        }
    }

    if (!newModule) {
        logger->error("❌ Cannot register null module");
        throw std::invalid_argument("Cannot register null module");
    }

    module = std::move(newModule);
    moduleName = name;

    logger->info("✅ Module '{}' registered successfully", moduleName);

    // Reset performance metrics for new module
    resetPerformanceMetrics();
    logger->debug("📊 Performance metrics reset for new module");
}

void SequentialModuleSystem::processModules(float deltaTime) {
    logProcessStart(deltaTime);

    auto processStartTime = std::chrono::high_resolution_clock::now();

    try {
        validateModule();

        // Create input IDataNode for module
        nlohmann::json inputJson = {
            {"deltaTime", deltaTime},
            {"frameCount", processCallCount},
            {"system", "sequential"},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                processStartTime.time_since_epoch()).count()}
        };

        auto moduleInput = std::make_unique<JsonDataNode>("input", inputJson);

        logger->trace("📥 Calling module process() with deltaTime: {:.3f}ms", deltaTime * 1000);

        // Process the module
        module->process(*moduleInput);

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

    } catch (const std::exception& e) {
        logger->error("❌ Error processing module '{}': {}", moduleName, e.what());
        logger->error("🔍 Error occurred at frame {}, deltaTime: {:.3f}ms", processCallCount, deltaTime * 1000);

        auto processEndTime = std::chrono::high_resolution_clock::now();
        lastProcessDuration = std::chrono::duration<float, std::milli>(processEndTime - processStartTime).count();

        logProcessEnd(lastProcessDuration);
        throw;
    }
}

void SequentialModuleSystem::setIOLayer(std::unique_ptr<IIO> io) {
    logger->info("🌐 Setting IO layer for SequentialModuleSystem");
    ioLayer = std::move(io);
    logger->debug("✅ IO layer set successfully");
}

std::unique_ptr<IDataNode> SequentialModuleSystem::queryModule(const std::string& name, const IDataNode& input) {
    logger->debug("🔍 Querying module '{}' directly", name);

    if (name != moduleName) {
        logger->warn("⚠️ Query for module '{}' but loaded module is '{}'", name, moduleName);
    }

    validateModule();

    try {
        // Clone input for processing
        // Note: We need to pass the input directly since IDataNode doesn't have clone yet
        logger->trace("📥 Querying module with input");

        // Process and return result
        // Since process() is void, we get state as result
        module->process(input);
        auto result = module->getState();

        logger->debug("✅ Module query completed");
        return result;

    } catch (const std::exception& e) {
        logger->error("❌ Error querying module '{}': {}", name, e.what());
        throw;
    }
}

ModuleSystemType SequentialModuleSystem::getType() const {
    logger->trace("🏷️ ModuleSystem type requested: SEQUENTIAL");
    return ModuleSystemType::SEQUENTIAL;
}

int SequentialModuleSystem::getPendingTaskCount(const std::string& moduleName) const {
    // SequentialModuleSystem executes tasks immediately, so never has pending tasks
    logger->trace("🔍 Pending task count for '{}': 0 (sequential execution)", moduleName);
    return 0;
}

// ITaskScheduler implementation
void SequentialModuleSystem::scheduleTask(const std::string& taskType, std::unique_ptr<IDataNode> taskData) {
    logger->debug("⚙️ Task scheduled for immediate execution: '{}'", taskType);
    logTaskExecution(taskType, *taskData);

    try {
        // In sequential system, tasks execute immediately
        logger->trace("🔧 Executing task '{}' immediately", taskType);

        // TODO: Implement actual task execution logic
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

std::unique_ptr<IDataNode> SequentialModuleSystem::getCompletedTask() {
    logger->warn("⚠️ getCompletedTask() called on sequential system - no queued tasks");
    throw std::runtime_error("SequentialModuleSystem executes tasks immediately - no completed tasks queue");
}

// Debug and monitoring methods
nlohmann::json SequentialModuleSystem::getPerformanceMetrics() const {
    logger->debug("📊 Performance metrics requested");

    nlohmann::json metrics = {
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

// Hot-reload support
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

// Private helper methods
void SequentialModuleSystem::logSystemStart() {
    logger->info("================================================================");
    logger->info("⚙️ SEQUENTIAL MODULE SYSTEM INITIALIZED");
    logger->info("================================================================");
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

void SequentialModuleSystem::logTaskExecution(const std::string& taskType, const IDataNode& taskData) {
    logger->trace("⚙️ Task execution {} - type: '{}'",
                 taskExecutionCount + 1, taskType);

    // Log data if available
    if (taskData.hasData()) {
        logger->trace("📄 Task data: {}", taskData.getData()->toString());
    }
}

void SequentialModuleSystem::validateModule() const {
    if (!module) {
        logger->error("❌ No module set - cannot process");
        throw std::runtime_error("No module set in SequentialModuleSystem");
    }
}

} // namespace grove
