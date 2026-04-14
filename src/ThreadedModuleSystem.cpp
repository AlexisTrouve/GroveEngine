#include <grove/ThreadedModuleSystem.h>
#include <grove/JsonDataNode.h>
#include <stdexcept>
#include <algorithm>
#include <logger/Logger.h>

namespace grove {

ThreadedModuleSystem::ThreadedModuleSystem() {
    logger = stillhammer::createDomainLogger("ThreadedModuleSystem", "engine");

    logSystemStart();
    systemStartTime = std::chrono::high_resolution_clock::now();
    lastFrameTime = systemStartTime;
}

ThreadedModuleSystem::~ThreadedModuleSystem() {
    // Check if logger is still valid (Windows static destruction order issue)
    bool loggerValid = false;
    try {
        loggerValid = logger && spdlog::get(logger->name()) != nullptr;
    } catch (...) {
        loggerValid = false;
    }

    if (loggerValid) {
        logger->info("🔧 ThreadedModuleSystem destructor called ({} workers)", workers.size());

        // Log final performance metrics
        if (!workers.empty()) {
            logger->info("📊 Final performance metrics:");
            logger->info("   Total frames processed: {}", globalFrameCount.load());
            logger->info("   Worker count: {}", workers.size());
            logger->info("   Total task executions: {}", taskExecutionCount.load());
        }
    }

    // Shutdown all worker threads gracefully
    for (auto& worker : workers) {
        if (loggerValid) {
            logger->debug("🛑 Shutting down worker '{}'", worker->name);
        }

        // Signal shutdown
        {
            std::lock_guard<std::mutex> lock(worker->mutex);
            worker->shouldShutdown = true;
            worker->cv.notify_one();
        }

        // Join thread
        if (worker->thread.joinable()) {
            worker->thread.join();
            if (loggerValid) {
                logger->debug("✅ Worker thread '{}' joined", worker->name);
            }
        }

        // Shutdown module
        try {
            if (worker->module) {
                worker->module->shutdown();
            }
        } catch (const std::exception& e) {
            if (loggerValid) {
                logger->error("❌ Error shutting down module '{}': {}", worker->name, e.what());
            }
        }
    }

    // Clear workers (this destroys modules)
    workers.clear();

    if (loggerValid) {
        logger->trace("🏗️ ThreadedModuleSystem destroyed");
    }
}

// IModuleSystem implementation

void ThreadedModuleSystem::registerModule(const std::string& name, std::unique_ptr<IModule> module) {
    logger->info("🔧 Registering module '{}' in ThreadedModuleSystem", name);

    if (!module) {
        logger->error("❌ Cannot register null module");
        throw std::invalid_argument("Cannot register null module");
    }

    // Acquire exclusive lock (write operation)
    std::unique_lock<std::shared_mutex> lock(workersMutex);

    // Check if module with same name already exists
    auto existingWorker = findWorker(name);
    if (existingWorker != workers.end()) {
        logger->warn("⚠️ Module '{}' already registered - use extractModule() first for hot-reload", name);
        throw std::invalid_argument("Module '" + name + "' already registered");
    }

    // Create worker (no thread yet)
    auto worker = std::make_unique<ModuleWorker>(name, std::move(module));

    // CRITICAL: Initialize lastProcessedGeneration to current generation
    // This prevents the worker from immediately processing on startup if currentFrameGeneration > 0
    worker->lastProcessedGeneration = currentFrameGeneration.load(std::memory_order_relaxed);

    // CRITICAL: Add worker to vector BEFORE spawning thread
    // This prevents race condition where thread tries to access workers[index] before it exists
    size_t workerIndex = workers.size();
    workers.push_back(std::move(worker));

    // NOW spawn worker thread (safe - worker is in vector)
    workers[workerIndex]->thread = std::thread(&ThreadedModuleSystem::workerThreadLoop, this, workerIndex);

    // Get thread ID for logging
    auto threadId = workers[workerIndex]->thread.get_id();
    std::hash<std::thread::id> hasher;
    size_t threadIdHash = hasher(threadId);

    lock.unlock();  // Release lock before logging

    // WORKAROUND: Yield to allow worker thread to start (prevents race on trivial workloads)
    std::this_thread::yield();

    logWorkerRegistration(name, threadIdHash);
    logger->info("✅ Module '{}' registered successfully (worker count: {})", name, workers.size());
}

void ThreadedModuleSystem::processModules(float deltaTime) {
    size_t frameCount = globalFrameCount.fetch_add(1);

    auto frameStartTime = std::chrono::high_resolution_clock::now();

    // CRITICAL: Set flag to prevent extractModule() deadlock
    isProcessingFrame.store(true, std::memory_order_release);

    // Acquire shared lock (read operation - concurrent processModules allowed)
    std::shared_lock<std::shared_mutex> lock(workersMutex);

    size_t workerCount = workers.size();

    if (workerCount == 0) {
        logger->warn("⚠️ No modules registered - nothing to process");
        isProcessingFrame.store(false, std::memory_order_release);
        return;
    }

    // TRACE: First frame only (provides thread startup synchronization)
    if (frameCount == 0) {
        logger->trace("🎬 First frame: {} workers", workerCount);
    }

    // PERFORMANCE: Removed logFrameStart() from hot path (causes mutex contention)
    // logFrameStart(deltaTime, workerCount);

    // ATOMIC BARRIER OPTIMIZATION: Replace per-worker lock operations with atomic counters
    // OLD: 16 lock operations (8 workers × 2 phases)
    // NEW: 0 lock operations in hot path

    // Store shared data (no locks needed - only main thread writes)
    sharedDeltaTime = deltaTime;
    sharedFrameCount = frameCount;

    // Reset completion counter for this frame
    // Use release ordering so workers see this write when they acquire
    workersCompleted.store(0, std::memory_order_release);

    // ATOMIC: Increment frame generation (signals new frame to workers)
    // memory_order_release ensures sharedDeltaTime/sharedFrameCount are visible to workers
    size_t generation = currentFrameGeneration.fetch_add(1, std::memory_order_release) + 1;

    // Wake up all workers (broadcast) - no locks needed!
    for (auto& worker : workers) {
        worker->cv.notify_one();
    }

    // ATOMIC: Spin-wait for all workers to complete
    // memory_order_acquire ensures we see worker's memory writes after processing
    int expectedCompletions = static_cast<int>(workerCount);
    while (workersCompleted.load(std::memory_order_acquire) < expectedCompletions) {
        std::this_thread::yield();  // Give up CPU to other threads
    }

    lock.unlock();  // Release shared lock

    // Calculate total synchronization time
    auto frameEndTime = std::chrono::high_resolution_clock::now();
    float totalSyncTime = std::chrono::duration<float, std::milli>(frameEndTime - frameStartTime).count();

    // PERFORMANCE: Removed logFrameEnd() from hot path (causes mutex contention)
    // logFrameEnd(totalSyncTime);

    // PERFORMANCE: Removed logger->warn() from hot path (causes mutex contention)
    // Warn if total frame time exceeds 60fps budget
    // if (totalSyncTime > 16.67f) {
    //     logger->warn("🐌 Slow frame processing: {:.2f}ms (target: <16.67ms for 60fps)", totalSyncTime);
    // }

    lastFrameTime = frameEndTime;

    // CRITICAL: Clear flag to allow extractModule()
    isProcessingFrame.store(false, std::memory_order_release);
}

void ThreadedModuleSystem::setIOLayer(std::unique_ptr<IIO> io) {
    logger->info("🌐 Setting IO layer for ThreadedModuleSystem");
    ioLayer = std::move(io);
}

std::unique_ptr<IDataNode> ThreadedModuleSystem::queryModule(const std::string& name, const IDataNode& input) {
    logger->debug("🔍 Querying module '{}'", name);

    // Acquire shared lock (concurrent queries allowed)
    std::shared_lock<std::shared_mutex> lock(workersMutex);

    auto workerIt = findWorker(name);
    if (workerIt == workers.end()) {
        logger->error("❌ Module '{}' not found", name);
        throw std::invalid_argument("Module '" + name + "' not found");
    }

    // BYPASS thread: Call process() directly for synchronous query
    // This is a debug/testing feature, not part of normal execution
    logger->trace("📞 Calling module '{}' process() directly (bypassing thread)", name);

    // Create temporary output capture
    // Note: Module's process() typically doesn't return data, it uses IIO pub/sub
    // This is a best-effort query mechanism
    (*workerIt)->module->process(input);

    // Return empty result (modules communicate via IIO, not return values)
    return std::make_unique<JsonDataNode>("query_result", json{{"status", "processed"}});
}

ModuleSystemType ThreadedModuleSystem::getType() const {
    return ModuleSystemType::THREADED;
}

int ThreadedModuleSystem::getPendingTaskCount(const std::string& moduleName) const {
    // Acquire shared lock
    std::shared_lock<std::shared_mutex> lock(workersMutex);

    auto workerIt = findWorker(moduleName);
    if (workerIt == workers.end()) {
        logger->trace("🔍 Module '{}' not found - returning 0 pending tasks", moduleName);
        return 0;
    }

    // Check if this specific worker is currently processing
    // Compare current generation with worker's last processed generation
    size_t currentGen = currentFrameGeneration.load(std::memory_order_relaxed);
    bool isProcessing = (currentGen > (*workerIt)->lastProcessedGeneration);

    return isProcessing ? 1 : 0;
}

std::unique_ptr<IModule> ThreadedModuleSystem::extractModule(const std::string& name) {
    logger->info("🔓 Extracting module '{}' from system", name);

    // CRITICAL: Wait for processModules() to finish (prevents deadlock)
    // Spin-wait until isProcessingFrame becomes false
    while (isProcessingFrame.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    logger->debug("✅ Frame processing idle, safe to extract module '{}'", name);

    // Acquire exclusive lock (write operation)
    std::unique_lock<std::shared_mutex> lock(workersMutex);

    auto workerIt = findWorker(name);
    if (workerIt == workers.end()) {
        logger->error("❌ Module '{}' not found", name);
        throw std::invalid_argument("Module '" + name + "' not found");
    }

    // Signal shutdown to worker thread
    {
        std::lock_guard<std::mutex> workerLock((*workerIt)->mutex);
        (*workerIt)->shouldShutdown = true;
        (*workerIt)->cv.notify_one();
    }

    logger->debug("🛑 Waiting for worker thread '{}' to join", name);

    // Join thread (CRITICAL: must join before extracting module)
    if ((*workerIt)->thread.joinable()) {
        (*workerIt)->thread.join();
        logger->debug("✅ Worker thread '{}' joined", name);
    }

    // Calculate final metrics
    float avgProcessTime = (*workerIt)->processCallCount > 0
        ? (*workerIt)->totalProcessTime / (*workerIt)->processCallCount
        : 0.0f;

    logWorkerShutdown(name, avgProcessTime);

    // Extract module
    auto extractedModule = std::move((*workerIt)->module);

    // Remove worker from vector
    workers.erase(workerIt);

    logger->info("✅ Module '{}' extracted successfully (remaining workers: {})", name, workers.size());

    return extractedModule;
}

// ITaskScheduler implementation

void ThreadedModuleSystem::scheduleTask(const std::string& taskType, std::unique_ptr<IDataNode> taskData) {
    logger->debug("⚙️ Task scheduled for immediate execution: '{}'", taskType);

    try {
        // In threaded system, tasks could be delegated to modules
        // For now, execute immediately like Sequential (TODO: implement actual task queue)
        taskExecutionCount.fetch_add(1);

        logger->debug("✅ Task '{}' completed immediately", taskType);
    } catch (const std::exception& e) {
        logger->error("❌ Error executing task '{}': {}", taskType, e.what());
        throw;
    }
}

int ThreadedModuleSystem::hasCompletedTasks() const {
    return 0;  // Tasks complete immediately (no queue yet)
}

std::unique_ptr<IDataNode> ThreadedModuleSystem::getCompletedTask() {
    throw std::runtime_error("ThreadedModuleSystem executes tasks immediately - no completed tasks queue");
}

// Debug and monitoring methods

json ThreadedModuleSystem::getPerformanceMetrics() const {
    std::shared_lock<std::shared_mutex> lock(workersMutex);

    json metrics = {
        {"system_type", "threaded"},
        {"worker_count", workers.size()},
        {"global_frame_count", globalFrameCount.load()},
        {"task_executions", taskExecutionCount.load()}
    };

    // Calculate uptime
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto uptime = std::chrono::duration<float>(currentTime - systemStartTime).count();
    metrics["uptime_seconds"] = uptime;

    // Calculate average FPS
    if (globalFrameCount > 0) {
        metrics["average_fps"] = uptime > 0 ? globalFrameCount.load() / uptime : 0.0f;
    }

    // Per-worker metrics
    json workerMetrics = json::array();
    for (const auto& worker : workers) {
        float avgProcessTime = worker->processCallCount > 0
            ? worker->totalProcessTime / worker->processCallCount
            : 0.0f;

        workerMetrics.push_back({
            {"name", worker->name},
            {"process_calls", worker->processCallCount},
            {"total_process_time_ms", worker->totalProcessTime},
            {"average_process_time_ms", avgProcessTime},
            {"last_process_time_ms", worker->lastProcessDuration}
        });
    }
    metrics["workers"] = workerMetrics;

    return metrics;
}

void ThreadedModuleSystem::resetPerformanceMetrics() {
    std::unique_lock<std::shared_mutex> lock(workersMutex);

    logger->debug("📊 Resetting performance metrics");

    globalFrameCount = 0;
    taskExecutionCount = 0;
    systemStartTime = std::chrono::high_resolution_clock::now();
    lastFrameTime = systemStartTime;

    for (auto& worker : workers) {
        std::lock_guard<std::mutex> workerLock(worker->mutex);
        worker->processCallCount = 0;
        worker->totalProcessTime = 0.0f;
        worker->lastProcessDuration = 0.0f;
    }
}

size_t ThreadedModuleSystem::getGlobalFrameCount() const {
    return globalFrameCount.load();
}

size_t ThreadedModuleSystem::getWorkerCount() const {
    std::shared_lock<std::shared_mutex> lock(workersMutex);
    return workers.size();
}

size_t ThreadedModuleSystem::getTaskExecutionCount() const {
    return taskExecutionCount.load();
}

void ThreadedModuleSystem::setLogLevel(spdlog::level::level_enum level) {
    logger->set_level(level);
    logger->info("📝 Log level set to: {}", spdlog::level::to_string_view(level));
}

// Private helper methods

void ThreadedModuleSystem::workerThreadLoop(size_t workerIndex) {
    // Access worker (safe - worker added to vector before thread spawn)
    auto& worker = *workers[workerIndex];

    // TRACE: Log startup (provides synchronization for first frame)
    logger->trace("🧵 Worker '{}' started (lastProcessedGen={})", worker.name, worker.lastProcessedGeneration);

    while (true) {
        // Wait for signal (work OR shutdown)
        std::unique_lock<std::mutex> lock(worker.mutex);

        // ATOMIC BARRIER: Wait for new frame generation
        // memory_order_acquire ensures we see sharedDeltaTime/sharedFrameCount writes
        worker.cv.wait(lock, [this, &worker] {
            size_t currentGen = currentFrameGeneration.load(std::memory_order_acquire);
            return (currentGen > worker.lastProcessedGeneration) || worker.shouldShutdown;
        });

        // Read current generation BEFORE checking shutdown (needed for completion signal)
        size_t generation = currentFrameGeneration.load(std::memory_order_acquire);
        bool wasWaitingForWork = (generation > worker.lastProcessedGeneration);

        if (worker.shouldShutdown) {
            logger->debug("🛑 Worker thread '{}' received shutdown signal", worker.name);

            // CRITICAL: If we were woken up for work AND shutdown, signal completion
            // This prevents deadlock when extractModule() is called during processModules()
            if (wasWaitingForWork) {
                workersCompleted.fetch_add(1, std::memory_order_release);
                logger->debug("🔓 Worker '{}' signaled completion before shutdown (prevents deadlock)", worker.name);
            }
            break;
        }

        // Read current generation and frame data BEFORE releasing lock
        generation = currentFrameGeneration.load(std::memory_order_acquire);

        // Release lock BEFORE processing (no lock during module execution!)
        lock.unlock();

        // Check if we already processed this generation (shouldn't happen, but be safe)
        if (generation <= worker.lastProcessedGeneration) {
            // Spurious wake-up
            continue;
        }

        // Read shared frame data (safe - only main thread writes, we have memory_order_acquire)
        float dt = sharedDeltaTime;
        size_t frameCount = sharedFrameCount;

        // Mark this generation as processed
        worker.lastProcessedGeneration = generation;

        // Process module (NO LOCKS during processing!)
        auto processStartTime = std::chrono::high_resolution_clock::now();

        try {
            auto input = createInputDataNode(dt, frameCount, worker.name);
            // PERFORMANCE: Removed logger->trace() from hot path (causes mutex contention)
            // logger->trace("🎬 Worker '{}' processing frame {} (dt: {:.3f}ms)",
            //              worker.name, frameCount, dt * 1000);

            worker.module->process(*input);

        } catch (const std::exception& e) {
            logger->error("❌ Error processing module '{}': {}", worker.name, e.what());
        }

        auto processEndTime = std::chrono::high_resolution_clock::now();
        float processDuration = std::chrono::duration<float, std::milli>(
            processEndTime - processStartTime).count();

        // Update metrics (lock only for writes)
        lock.lock();

        worker.lastProcessDuration = processDuration;
        worker.totalProcessTime += processDuration;
        worker.processCallCount++;
        worker.lastProcessStart = processStartTime;

        lock.unlock();

        // PERFORMANCE: Removed logger->warn() from hot path (causes mutex contention)
        // Warn if module processing slow
        // if (processDuration > 16.67f) {
        //     logger->warn("🐌 Module '{}' processing slow: {:.2f}ms (target: <16.67ms)",
        //                 worker.name, processDuration);
        // }

        // ATOMIC: Signal completion (no lock needed!)
        // memory_order_release ensures metrics writes are visible to main thread
        workersCompleted.fetch_add(1, std::memory_order_release);
    }

    logger->debug("🏁 Worker thread '{}' exiting", worker.name);
}

std::unique_ptr<IDataNode> ThreadedModuleSystem::createInputDataNode(
    float deltaTime, size_t frameCount, const std::string& moduleName) {

    auto currentTime = std::chrono::high_resolution_clock::now();

    nlohmann::json inputJson = {
        {"deltaTime", deltaTime},
        {"frameCount", frameCount},
        {"system", "threaded"},
        {"moduleName", moduleName},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime.time_since_epoch()).count()}
    };

    return std::make_unique<JsonDataNode>("input", inputJson);
}

std::vector<std::unique_ptr<ThreadedModuleSystem::ModuleWorker>>::iterator
ThreadedModuleSystem::findWorker(const std::string& name) {
    return std::find_if(workers.begin(), workers.end(),
        [&name](const std::unique_ptr<ModuleWorker>& w) { return w->name == name; });
}

std::vector<std::unique_ptr<ThreadedModuleSystem::ModuleWorker>>::const_iterator
ThreadedModuleSystem::findWorker(const std::string& name) const {
    return std::find_if(workers.begin(), workers.end(),
        [&name](const std::unique_ptr<ModuleWorker>& w) { return w->name == name; });
}

void ThreadedModuleSystem::validateWorkerIndex(size_t index) const {
    if (index >= workers.size()) {
        throw std::out_of_range("Worker index " + std::to_string(index) + " out of range (size: " +
                               std::to_string(workers.size()) + ")");
    }
}

// Logging helper methods

void ThreadedModuleSystem::logSystemStart() {
    logger->info("🚀 ThreadedModuleSystem initialized");
    logger->debug("   Thread model: One thread per module");
    logger->debug("   Synchronization: Barrier pattern (frame-based)");
    logger->debug("   Thread safety: shared_mutex for module registry");
}

void ThreadedModuleSystem::logFrameStart(float deltaTime, size_t workerCount) {
    // Log every 60 frames to avoid spam
    if (globalFrameCount % 60 == 0) {
        logger->trace("🎬 Processing frame {} ({} workers, deltaTime: {:.3f}ms)",
                     globalFrameCount.load(), workerCount, deltaTime * 1000);
    }
}

void ThreadedModuleSystem::logFrameEnd(float totalSyncTime) {
    if (globalFrameCount % 60 == 0) {
        logger->trace("✅ Frame {} completed (sync time: {:.2f}ms)",
                     globalFrameCount.load(), totalSyncTime);
    }
}

void ThreadedModuleSystem::logWorkerRegistration(const std::string& name, size_t threadId) {
    logger->debug("🧵 Worker thread started for '{}' (TID hash: {})", name, threadId);
}

void ThreadedModuleSystem::logWorkerShutdown(const std::string& name, float avgProcessTime) {
    logger->debug("📊 Worker '{}' final metrics:", name);
    logger->debug("   Average process time: {:.3f}ms", avgProcessTime);
}

} // namespace grove
