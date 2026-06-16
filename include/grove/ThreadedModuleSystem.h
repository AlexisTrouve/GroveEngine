#pragma once

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "IModuleSystem.h"
#include "IModule.h"
#include "IIO.h"

using json = nlohmann::json;

namespace grove {

/**
 * @brief Threaded module system implementation - one thread per module
 *
 * ThreadedModuleSystem executes each module in its own dedicated thread,
 * providing true parallel execution for CPU-bound modules.
 *
 * Features:
 * - Multi-module support (N modules, N threads)
 * - Parallel execution with barrier synchronization
 * - Thread-safe IIO communication (IntraIOManager handles routing)
 * - Hot-reload support with graceful thread shutdown
 * - Performance monitoring per module
 *
 * Architecture:
 * - Each module runs in a persistent worker thread
 * - Main thread coordinates via condition variables (barrier pattern)
 * - All modules process in lock-step (frame-based synchronization)
 * - shared_mutex protects module registry (read-heavy workload)
 *
 * Thread safety:
 * - Read operations (processModules, queryModule): shared_lock
 * - Write operations (registerModule, shutdown): unique_lock
 * - Per-worker synchronization: independent mutexes (no deadlock)
 *
 * Recommended usage:
 * - Module count â‰¤ CPU cores
 * - Target FPS â‰¤ 30 (for heavier processing per module)
 * - Example: BgfxRenderer + UIModule + InputModule + CustomLogic
 */
class ThreadedModuleSystem : public IModuleSystem {
private:
    /**
     * @brief Worker thread context for a single module
     *
     * Each ModuleWorker encapsulates:
     * - The module instance (unique ownership)
     * - A dedicated thread running workerThreadLoop()
     * - Synchronization primitives for frame-based execution
     * - Performance tracking (per-module metrics)
     */
    struct ModuleWorker {
        std::string name;
        std::unique_ptr<IModule> module;
        std::thread thread;

        // Synchronization for barrier pattern
        mutable std::mutex mutex;  // mutable: can be locked in const methods
        std::condition_variable cv;

        // FIX #10 (BUG D) : sérialise les appels à module->process() pour CE module.
        // POURQUOI : queryModule() appelle process() depuis le thread appelant pendant
        //   que le thread worker peut appeler process() sur le MÊME module (frame en cours)
        //   → data race sur l'état interne du module. Ce mutex, pris autour des DEUX appels
        //   (worker loop + queryModule), garantit l'exclusion mutuelle.
        // COMMENT : un mutex PAR worker → aucune contention inter-modules (le worker de X
        //   et celui de Y prennent des mutex différents), donc le parallélisme entre modules
        //   est préservé ; seul query(X) ↔ worker(X) est sérialisé. Distinct du `mutex`
        //   barrière (cv) pour ne pas interférer avec l'attente de génération.
        std::mutex processMutex;
        // REMOVED: bool shouldProcess (replaced with atomic shouldProcessAll)
        // REMOVED: bool processingComplete (replaced with atomic workersCompleted counter)
        // REMOVED: float deltaTime (replaced with shared sharedDeltaTime)
        // REMOVED: size_t frameCount (replaced with shared sharedFrameCount)
        bool shouldShutdown = false;      // Signal: terminate thread

        // Frame generation tracking (to prevent double-processing)
        // Each frame has a unique generation number that increments
        size_t lastProcessedGeneration = 0;  // Last generation this worker processed

        // Performance metrics (protected by mutex)
        std::chrono::high_resolution_clock::time_point lastProcessStart;
        float lastProcessDuration = 0.0f;
        float totalProcessTime = 0.0f;
        size_t processCallCount = 0;

        ModuleWorker(std::string moduleName, std::unique_ptr<IModule> moduleInstance)
            : name(std::move(moduleName))
            , module(std::move(moduleInstance))
        {}

        // Non-copyable, non-movable (contains mutex/cv)
        ModuleWorker(const ModuleWorker&) = delete;
        ModuleWorker& operator=(const ModuleWorker&) = delete;
        ModuleWorker(ModuleWorker&&) = delete;
        ModuleWorker& operator=(ModuleWorker&&) = delete;
    };

    std::shared_ptr<spdlog::logger> logger;
    std::unique_ptr<IIO> ioLayer;

    // Module workers (one per module) - using unique_ptr because ModuleWorker is non-movable
    std::vector<std::unique_ptr<ModuleWorker>> workers;
    mutable std::shared_mutex workersMutex;  // Protects workers vector

    // ATOMIC BARRIER COORDINATION (lock-free synchronization)
    // These atomics replace per-worker bool flags (shouldProcess, processingComplete)
    // Benefits: No mutex locking in hot path, 2-4x performance gain
    std::atomic<int> workersCompleted{0};         // Count of workers that finished processing
    std::atomic<size_t> currentFrameGeneration{0}; // Frame generation counter (increments each frame)
    std::atomic<bool> isProcessingFrame{false};   // True during processModules() (prevents extractModule deadlock)
    std::atomic<bool> extractionRequested{false}; ///< Set by extractModule() BEFORE spinning, so processModules() yields early — breaks reader-writer starvation

    // FIX: [BUG F] -- sharedDeltaTime/sharedFrameCount replaced by atomics to avoid UB
    // (data race between main thread writing and worker threads reading; even if x86 is
    // safe in practice, this is formal C++ UB). float encoded as uint32_t bits via memcpy
    // to guarantee consistency without precision loss.
    std::atomic<uint32_t> sharedDeltaTimeBits{0};  // float deltaTime encoded as uint32_t bits
    std::atomic<size_t> sharedFrameCount{0};        // frameCount (replaces plain size_t)

    // Global frame tracking
    std::atomic<size_t> globalFrameCount{0};
    std::chrono::high_resolution_clock::time_point systemStartTime;
    std::chrono::high_resolution_clock::time_point lastFrameTime;

    // Task scheduling tracking (for ITaskScheduler interface)
    std::atomic<size_t> taskExecutionCount{0};

    // Helper methods
    void logSystemStart();
    void logFrameStart(float deltaTime, size_t workerCount);
    void logFrameEnd(float totalSyncTime);
    void logWorkerRegistration(const std::string& name, size_t threadId);
    void logWorkerShutdown(const std::string& name, float avgProcessTime);
    void validateWorkerIndex(size_t index) const;

    /**
     * @brief Worker thread main loop
     * @param workerPtr Raw pointer to the WorkerData this thread owns.
     *
     * FIX: [BUG A] -- Changed from size_t workerIndex to WorkerData* workerPtr.
     * Rationale: workerIndex becomes stale after extractModule() calls workers.erase(),
     * causing workers[workerIndex] to reference the wrong (or non-existent) element.
     * A raw pointer captured at thread-creation time stays valid because:
     *   1. The unique_ptr<ModuleWorker> remains in the vector until extractModule()
     *   2. extractModule() joins the thread BEFORE erasing the unique_ptr
     *   => no dangling pointer possible.
     *
     * Thread-safe: Only accesses its own WorkerData (no cross-worker access)
     */
    void workerThreadLoop(ModuleWorker* workerPtr);

    /**
     * @brief Create input DataNode for module processing
     * @param deltaTime Time since last frame
     * @param frameCount Current frame number
     * @param moduleName Name of the module being processed
     * @return JsonDataNode with frame metadata
     */
    std::unique_ptr<IDataNode> createInputDataNode(float deltaTime, size_t frameCount, const std::string& moduleName);

    /**
     * @brief Find worker by name (must hold workersMutex)
     * @param name Module name to find
     * @return Iterator to worker, or workers.end() if not found
     */
    std::vector<std::unique_ptr<ModuleWorker>>::iterator findWorker(const std::string& name);
    std::vector<std::unique_ptr<ModuleWorker>>::const_iterator findWorker(const std::string& name) const;

public:
    ThreadedModuleSystem();
    virtual ~ThreadedModuleSystem();

    // IModuleSystem implementation
    void registerModule(const std::string& name, std::unique_ptr<IModule> module) override;
    void processModules(float deltaTime) override;
    void setIOLayer(std::unique_ptr<IIO> ioLayer) override;
    std::unique_ptr<IDataNode> queryModule(const std::string& name, const IDataNode& input) override;
    ModuleSystemType getType() const override;
    int getPendingTaskCount(const std::string& moduleName) const override;

    /**
     * @brief Extract module for hot-reload
     * @param name Name of module to extract
     * @return Extracted module instance (thread already joined)
     *
     * Workflow:
     * 1. Lock workers (exclusive)
     * 2. Signal worker thread to shutdown
     * 3. Join worker thread (wait for completion)
     * 4. Extract module instance
     * 5. Remove worker from vector
     *
     * CRITICAL: Thread must be joined BEFORE returning module,
     * otherwise module might be destroyed while thread is still running.
     */
    std::unique_ptr<IModule> extractModule(const std::string& name);

    // ITaskScheduler implementation (inherited)
    void scheduleTask(const std::string& taskType, std::unique_ptr<IDataNode> taskData) override;
    int hasCompletedTasks() const override;
    std::unique_ptr<IDataNode> getCompletedTask() override;

    // Debug and monitoring methods
    json getPerformanceMetrics() const;
    void resetPerformanceMetrics();
    size_t getGlobalFrameCount() const;
    size_t getWorkerCount() const;
    size_t getTaskExecutionCount() const;

    // Configuration
    void setLogLevel(spdlog::level::level_enum level);
};

} // namespace grove
