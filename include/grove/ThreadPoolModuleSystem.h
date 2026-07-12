#pragma once

#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>

#include "IModuleSystem.h"
#include "IModule.h"
#include "IIO.h"

namespace grove {

/**
 * @brief Phase 3 module system — a SHARED worker pool with work-stealing.
 *
 * WHAT: hosts N modules and executes them on a fixed pool of M worker threads
 *   (M ≈ CPU cores), instead of Phase 2's "one OS thread per module". Each frame,
 *   each module becomes ONE task ("process() it, then drain its IIO inbox"); the
 *   M workers pull tasks from per-worker deques and STEAL from each other when their
 *   own deque runs dry, so a few slow modules don't leave cores idle. processModules()
 *   is a per-frame barrier: it returns only once all N tasks for the frame are done.
 *
 * WHY: Phase 2 spawns one thread per module → at >8 modules the OS oversubscribes
 *   (context-switch thrash). A bounded pool decouples module count from thread count,
 *   which is what the >30 FPS / many-modules use case needs.
 *
 * CORRECTNESS INVARIANTS (carried over from the Phase 2 work — do not break):
 *   - process()-then-drain is ATOMIC per task, on the worker that runs it (archi A).
 *     A self-draining module (e.g. UIModule) pulls its own inbox inside process();
 *     draining AFTER process() keeps that ordering intact (draining before would wipe
 *     its input edges — see test_threaded_real_ui_e2e).
 *   - 1 module = 1 task per frame, never re-entrant → two workers never run the same
 *     module concurrently (a task lives in exactly one deque, popped once under a mutex).
 *   - Cross-frame happens-before: a worker finishes a task with tasksRemaining-- (release);
 *     processModules() waits tasksRemaining==0 (acquire); next frame bumps frameGeneration
 *     (release) which the next worker reads (acquire). Transitively, frame N's module-state
 *     writes are visible to frame N+1's executor — even if a different worker picks it up.
 *   - processMutex per module serializes queryModule()/extractModule() against a running task.
 *
 * Work-stealing deques are mutex-protected (owner pops the back/LIFO, thieves steal the
 * front/FIFO). At the granularity of "1 task = 1 module process()" the lock cost is
 * negligible; a lock-free Chase-Lev deque would be a later internal optimisation that
 * does not change this interface.
 */
class ThreadPoolModuleSystem : public IModuleSystem {
private:
    /**
     * @brief A hosted module. Unlike Phase 2's ModuleWorker, it owns NO thread —
     *   it is executed each frame by whichever pool worker pops its task.
     */
    struct ModuleSlot {
        std::string name;
        std::unique_ptr<IModule> module;

        // Archi A: this module's routed IIO inbox, drained right AFTER process() by the
        // worker that ran it (so subscribe handlers and process() run on the same thread →
        // no cross-thread race on the module's own state). Null for self-wiring modules.
        std::shared_ptr<IIO> inbox;

        // Serializes module->process() for THIS module across: the pool worker running its
        // frame task, queryModule() (caller thread), and extractModule(). One mutex per slot
        // → no inter-module contention (only same-module access is serialized).
        std::mutex processMutex;

        // Metric: how many times this module's task has run (atomic — read off-thread).
        std::atomic<size_t> processCallCount{0};

        ModuleSlot(std::string n, std::unique_ptr<IModule> m)
            : name(std::move(n)), module(std::move(m)) {}

        // Non-copyable, non-movable (holds a mutex + atomic).
        ModuleSlot(const ModuleSlot&) = delete;
        ModuleSlot& operator=(const ModuleSlot&) = delete;
        ModuleSlot(ModuleSlot&&) = delete;
        ModuleSlot& operator=(ModuleSlot&&) = delete;
    };

    /// One frame task: run this slot once (process-then-drain). slot is a STABLE pointer
    /// (slots live in unique_ptrs; extractModule only erases between frames, deques empty).
    struct Task { ModuleSlot* slot = nullptr; };

    /// Per-worker work-stealing deque, mutex-protected.
    struct WorkerDeque {
        std::deque<Task> tasks;
        std::mutex mutex;
    };

    std::shared_ptr<spdlog::logger> logger;
    std::unique_ptr<IIO> ioLayer;

    // Module registry — stable pointers (unique_ptr), guarded by a read-heavy shared_mutex.
    std::vector<std::unique_ptr<ModuleSlot>> modules;
    mutable std::shared_mutex modulesMutex;

    // The worker pool + its deques. deques are created BEFORE the threads (threads read them).
    size_t poolSize = 0;
    std::vector<std::unique_ptr<WorkerDeque>> deques;
    std::vector<std::thread> poolThreads;

    // Frame barrier / wakeup.
    std::atomic<size_t> frameGeneration{0};  ///< ++ each frame; workers wake when it advances
    std::atomic<int> tasksRemaining{0};      ///< tasks not yet completed this frame; reaches 0 == frame done
    // Acknowledgement counter: each worker bumps it once it has LEFT runFrameTasks() for the
    // current frame. processModules() waits for workersDone==poolSize before returning, so the
    // next frame never re-arms tasksRemaining while a worker still spins in the old frame's loop
    // (that frame-straddle was the cause of an intermittent barrier hang — a stolen decrement
    // landing on the wrong frame). This mirrors Phase 2's workersCompleted barrier.
    std::atomic<int> workersDone{0};
    std::atomic<bool> shutdownFlag{false};
    std::mutex frameMutex;                    ///< guards the frameCv predicate (avoids missed wakeup)
    std::condition_variable frameCv;

    // Shared per-frame inputs (atomics: written by caller, read by workers — avoid formal UB).
    std::atomic<uint32_t> sharedDeltaTimeBits{0};  ///< float deltaTime encoded as uint32_t bits
    std::atomic<size_t> sharedFrameCount{0};
    std::atomic<size_t> globalFrameCount{0};
    std::atomic<size_t> taskExecutionCount{0};

    // Observed parallelism (proof/debug): max number of process() bodies in flight at once.
    std::atomic<int> currentConcurrency{0};
    std::atomic<int> maxConcurrency{0};

    // --- internals ---
    void workerLoop(size_t workerIndex);                 ///< persistent pool-thread main loop
    void runFrameTasks(size_t workerIndex);              ///< drain+steal until the frame is done
    bool tryPopOwn(size_t workerIndex, Task& out);       ///< owner pop (LIFO, deque back)
    bool trySteal(size_t workerIndex, Task& out);        ///< steal from another worker (FIFO, deque front)
    void executeTask(const Task& task);                  ///< archi-A: process() then drain inbox
    std::unique_ptr<IDataNode> createInputDataNode(float deltaTime, size_t frameCount,
                                                   const std::string& moduleName);
    std::vector<std::unique_ptr<ModuleSlot>>::iterator findModule(const std::string& name);
    std::vector<std::unique_ptr<ModuleSlot>>::const_iterator findModule(const std::string& name) const;

public:
    /**
     * @param threadCount number of pool workers; 0 = auto (max(1, hardware_concurrency()-1),
     *   leaving a core for the calling/render thread).
     */
    explicit ThreadPoolModuleSystem(size_t threadCount = 0);
    ~ThreadPoolModuleSystem() override;

    // IModuleSystem
    void registerModule(const std::string& name, std::unique_ptr<IModule> module) override;
    void processModules(float deltaTime) override;
    void setIOLayer(std::unique_ptr<IIO> ioLayer) override;
    std::unique_ptr<IDataNode> queryModule(const std::string& name, const IDataNode& input) override;

    // Thread-safe state snapshot / restore for whole-engine save-load. Runs getState()/setState()
    // UNDER the module's per-slot processMutex (same guard as queryModule) so it can't race the pool
    // worker running the module's frame task. nullptr / false if `name` isn't hosted here (fail-soft).
    std::unique_ptr<IDataNode> captureModuleState(const std::string& name);
    bool restoreModuleState(const std::string& name, const IDataNode& state);

    ModuleSystemType getType() const override;
    int getPendingTaskCount(const std::string& moduleName) const override;

    /// Archi-A wiring (mirrors ThreadedModuleSystem): hand a module its routed inbox so the
    /// pool worker drains it after process(). Call after registerModule, before first frame.
    void setModuleInbox(const std::string& name, std::shared_ptr<IIO> inbox);

    /// Hot-reload: pull a module out (for reload). Called between frames; the slot's task is
    /// already done (barrier) and deques are empty, so no in-flight task references it.
    std::unique_ptr<IModule> extractModule(const std::string& name);

    // ITaskScheduler (inherited) — immediate-execution stubs, like Phase 1/2 (no async queue yet).
    void scheduleTask(const std::string& taskType, std::unique_ptr<IDataNode> taskData) override;
    int hasCompletedTasks() const override;
    std::unique_ptr<IDataNode> getCompletedTask() override;

    // Debug / monitoring.
    size_t getPoolSize() const { return poolSize; }
    size_t getModuleCount() const;
    size_t getGlobalFrameCount() const { return globalFrameCount.load(); }
    size_t getTaskExecutionCount() const { return taskExecutionCount.load(); }
    int getMaxObservedConcurrency() const { return maxConcurrency.load(); }
    void resetConcurrencyStats() { maxConcurrency.store(0); }
};

} // namespace grove
