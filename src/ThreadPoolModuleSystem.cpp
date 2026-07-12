#include <grove/ThreadPoolModuleSystem.h>
#include <grove/JsonDataNode.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>      // std::memcpy
#include <logger/Logger.h>

namespace grove {

// ============================================================================
// Construction / destruction
// ============================================================================

ThreadPoolModuleSystem::ThreadPoolModuleSystem(size_t threadCount) {
    logger = stillhammer::createDomainLogger("ThreadPoolModuleSystem", "engine");

    // QUOI : dimensionner le pool. POURQUOI : on borne le nombre de threads (≈ cœurs)
    // pour découpler le nb de modules du nb de threads — c'est tout l'intérêt de Phase 3.
    // COMMENT : threadCount explicite sinon auto = cœurs-1 (on laisse un cœur au thread
    //   appelant qui attend la barrière) ; garde-fou à 1 si hardware_concurrency() ment (0).
    if (threadCount > 0) {
        poolSize = threadCount;
    } else {
        unsigned hc = std::thread::hardware_concurrency();
        poolSize = (hc > 1) ? static_cast<size_t>(hc - 1) : 1;
    }

    // Les deques DOIVENT exister avant de lancer les threads (les workers y accèdent).
    deques.reserve(poolSize);
    for (size_t i = 0; i < poolSize; ++i) {
        deques.push_back(std::make_unique<WorkerDeque>());
    }
    poolThreads.reserve(poolSize);
    for (size_t i = 0; i < poolSize; ++i) {
        poolThreads.emplace_back(&ThreadPoolModuleSystem::workerLoop, this, i);
    }

    logger->info("🚀 ThreadPoolModuleSystem initialized ({} pool workers, work-stealing)", poolSize);
}

ThreadPoolModuleSystem::~ThreadPoolModuleSystem() {
    // Windows static-destruction-order safety (same guard as ThreadedModuleSystem).
    bool loggerValid = false;
    try {
        loggerValid = logger && spdlog::get(logger->name()) != nullptr;
    } catch (...) {
        loggerValid = false;
    }

    if (loggerValid) {
        logger->info("🔧 ThreadPoolModuleSystem destructor — {} modules, {} frames, {} task runs",
                     modules.size(), globalFrameCount.load(), taskExecutionCount.load());
    }

    // QUOI : arrêter le pool. COMMENT : poser le flag SOUS frameMutex (sinon un worker en
    // train d'évaluer son prédicat de wait pourrait rater le notify → join qui bloque),
    // réveiller tout le monde, puis join.
    {
        std::lock_guard<std::mutex> lk(frameMutex);
        shutdownFlag.store(true);
    }
    frameCv.notify_all();
    for (auto& t : poolThreads) {
        if (t.joinable()) t.join();
    }

    // Shutdown des modules après l'arrêt des workers (plus aucune tâche en vol).
    for (auto& slot : modules) {
        try {
            if (slot->module) slot->module->shutdown();
        } catch (const std::exception& e) {
            if (loggerValid) logger->error("❌ Error shutting down module '{}': {}", slot->name, e.what());
        }
    }
    modules.clear();
}

// ============================================================================
// Registry helpers
// ============================================================================

std::vector<std::unique_ptr<ThreadPoolModuleSystem::ModuleSlot>>::iterator
ThreadPoolModuleSystem::findModule(const std::string& name) {
    return std::find_if(modules.begin(), modules.end(),
                        [&](const std::unique_ptr<ModuleSlot>& s) { return s->name == name; });
}

std::vector<std::unique_ptr<ThreadPoolModuleSystem::ModuleSlot>>::const_iterator
ThreadPoolModuleSystem::findModule(const std::string& name) const {
    return std::find_if(modules.begin(), modules.end(),
                        [&](const std::unique_ptr<ModuleSlot>& s) { return s->name == name; });
}

// ============================================================================
// IModuleSystem — registration
// ============================================================================

void ThreadPoolModuleSystem::registerModule(const std::string& name, std::unique_ptr<IModule> module) {
    std::unique_lock<std::shared_mutex> lock(modulesMutex);
    if (findModule(name) != modules.end()) {
        logger->warn("⚠️ Module '{}' already registered — ignoring", name);
        return;
    }
    modules.push_back(std::make_unique<ModuleSlot>(name, std::move(module)));
    logger->info("🔧 Registered module '{}' in pool (total: {})", name, modules.size());
    // No thread spawned — the existing pool will pick up this module's task next frame.
}

void ThreadPoolModuleSystem::setModuleInbox(const std::string& name, std::shared_ptr<IIO> inbox) {
    std::unique_lock<std::shared_mutex> lock(modulesMutex);
    auto it = findModule(name);
    if (it == modules.end()) {
        logger->warn("⚠️ setModuleInbox: module '{}' not found", name);
        return;
    }
    (*it)->inbox = std::move(inbox);
    logger->debug("📥 Module '{}' inbox wired (archi A) — drained after process() on its pool worker", name);
}

// ============================================================================
// Frame execution — the barrier + work-stealing core
// ============================================================================

void ThreadPoolModuleSystem::processModules(float deltaTime) {
    // Hold the registry shared across the whole frame: blocks register/extract mid-frame
    // (they take it exclusive), so the deques' slot pointers stay valid for the duration.
    std::shared_lock<std::shared_mutex> lock(modulesMutex);

    const size_t n = modules.size();
    if (n == 0) return;

    // 1. Hand out one task per module, round-robin across the worker deques. Done under each
    //    deque's mutex (workers may already be spinning from a prior frame).
    for (size_t i = 0; i < n; ++i) {
        WorkerDeque& d = *deques[i % poolSize];
        std::lock_guard<std::mutex> dlk(d.mutex);
        d.tasks.push_back(Task{ modules[i].get() });
    }

    // 2. Publish this frame's shared inputs (encode the float as bits — atomic, no UB).
    uint32_t dtBits = 0;
    std::memcpy(&dtBits, &deltaTime, sizeof(dtBits));
    sharedDeltaTimeBits.store(dtBits, std::memory_order_relaxed);
    size_t fc = globalFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
    sharedFrameCount.store(fc, std::memory_order_relaxed);

    // 3. Arm the frame. Reset the per-frame ack counter FIRST: every worker from the previous
    //    frame is at rest (processModules only returns once workersDone==poolSize), so nobody
    //    bumps it between this reset and the wakeup. tasksRemaining is the release store that
    //    publishes the deque contents; frameGeneration (under frameMutex) wakes the workers.
    workersDone.store(0, std::memory_order_relaxed);
    tasksRemaining.store(static_cast<int>(n), std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(frameMutex);
        frameGeneration.fetch_add(1, std::memory_order_release);
    }
    frameCv.notify_all();

    // 4. Barrier: wait until EVERY worker has LEFT this frame's task loop. workersDone==poolSize
    //    implies tasksRemaining==0 (a worker only leaves runFrameTasks once tasks hit 0) AND
    //    guarantees no worker is still spinning in the loop — so next frame re-arming
    //    tasksRemaining cannot have a decrement stolen by a straddling worker. Acquire pairs
    //    with each worker's release fetch_add, so all module-state writes are visible here.
    while (workersDone.load(std::memory_order_acquire) < static_cast<int>(poolSize)) {
        std::this_thread::yield();
    }
}

void ThreadPoolModuleSystem::workerLoop(size_t workerIndex) {
    size_t myLastGen = 0;
    while (true) {
        // Sleep until a new frame is opened (generation advanced) or we're shutting down.
        {
            std::unique_lock<std::mutex> lk(frameMutex);
            frameCv.wait(lk, [&] {
                return frameGeneration.load(std::memory_order_acquire) > myLastGen
                    || shutdownFlag.load(std::memory_order_acquire);
            });
        }
        if (shutdownFlag.load(std::memory_order_acquire)) break;

        myLastGen = frameGeneration.load(std::memory_order_acquire);
        runFrameTasks(workerIndex);
        // Acknowledge: I have LEFT this frame's task loop. Release so processModules()'s
        // acquire-load of workersDone sees all my module-state writes — and, crucially, so it
        // knows I will not touch tasksRemaining again until the next generation wakes me.
        workersDone.fetch_add(1, std::memory_order_release);
    }
}

void ThreadPoolModuleSystem::runFrameTasks(size_t workerIndex) {
    // Drain own deque, steal from others; spin (yield) while tasks are still in flight on
    // other workers. Exits once tasksRemaining hits 0 — i.e. the whole frame is done.
    while (tasksRemaining.load(std::memory_order_acquire) > 0) {
        Task t;
        if (tryPopOwn(workerIndex, t) || trySteal(workerIndex, t)) {
            executeTask(t);
            tasksRemaining.fetch_sub(1, std::memory_order_release);
        } else {
            std::this_thread::yield();  // nothing to take right now; others are still running
        }
    }
}

bool ThreadPoolModuleSystem::tryPopOwn(size_t workerIndex, Task& out) {
    WorkerDeque& d = *deques[workerIndex];
    std::lock_guard<std::mutex> lk(d.mutex);
    if (d.tasks.empty()) return false;
    out = d.tasks.back();   // owner pops the back (LIFO — most recently pushed, cache-warm)
    d.tasks.pop_back();
    return true;
}

bool ThreadPoolModuleSystem::trySteal(size_t workerIndex, Task& out) {
    // Scan the other workers round-robin; steal from the FRONT (FIFO) to minimise collision
    // with the victim's own back-pops.
    for (size_t i = 1; i < poolSize; ++i) {
        size_t victim = (workerIndex + i) % poolSize;
        WorkerDeque& d = *deques[victim];
        std::lock_guard<std::mutex> lk(d.mutex);
        if (d.tasks.empty()) continue;
        out = d.tasks.front();
        d.tasks.pop_front();
        return true;
    }
    return false;
}

void ThreadPoolModuleSystem::executeTask(const Task& task) {
    ModuleSlot& slot = *task.slot;

    // Concurrency probe: record the running maximum of in-flight process() bodies.
    int now = currentConcurrency.fetch_add(1, std::memory_order_relaxed) + 1;
    int prev = maxConcurrency.load(std::memory_order_relaxed);
    while (now > prev && !maxConcurrency.compare_exchange_weak(prev, now, std::memory_order_relaxed)) {}

    // Decode this frame's shared inputs.
    uint32_t dtBits = sharedDeltaTimeBits.load(std::memory_order_relaxed);
    float dt = 0.0f;
    std::memcpy(&dt, &dtBits, sizeof(dt));
    size_t frameCount = sharedFrameCount.load(std::memory_order_relaxed);

    {
        // Archi A: process() FIRST, then drain THIS module's inbox — see the header's
        // CORRECTNESS INVARIANTS. processMutex serializes vs queryModule()/extractModule().
        std::lock_guard<std::mutex> processGuard(slot.processMutex);
        auto input = createInputDataNode(dt, frameCount, slot.name);
        slot.module->process(*input);

        if (slot.inbox) {
            int drained = 0;
            while (slot.inbox->hasMessages() > 0 && drained < 100000) {
                slot.inbox->pullAndDispatch();
                ++drained;
            }
        }
    }

    slot.processCallCount.fetch_add(1, std::memory_order_relaxed);
    taskExecutionCount.fetch_add(1, std::memory_order_relaxed);
    currentConcurrency.fetch_sub(1, std::memory_order_relaxed);
}

// ============================================================================
// IModuleSystem — query / type / pending / IO
// ============================================================================

void ThreadPoolModuleSystem::setIOLayer(std::unique_ptr<IIO> io) {
    logger->info("🌐 Setting IO layer for ThreadPoolModuleSystem");
    ioLayer = std::move(io);
}

std::unique_ptr<IDataNode> ThreadPoolModuleSystem::queryModule(const std::string& name, const IDataNode& input) {
    std::shared_lock<std::shared_mutex> lock(modulesMutex);
    auto it = findModule(name);
    if (it == modules.end()) {
        logger->error("❌ Module '{}' not found", name);
        throw std::invalid_argument("Module '" + name + "' not found");
    }
    // Serialize with the pool worker that may be running this module's frame task.
    {
        std::lock_guard<std::mutex> processGuard((*it)->processMutex);
        (*it)->module->process(input);
    }
    return std::make_unique<JsonDataNode>("query_result", nlohmann::json{{"status", "processed"}});
}

std::unique_ptr<IDataNode> ThreadPoolModuleSystem::captureModuleState(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(modulesMutex);
    auto it = findModule(name);
    if (it == modules.end()) return nullptr;   // not hosted here — fail-soft
    // Serialize with the pool worker that may be running this module's frame task (per-slot mutex).
    std::lock_guard<std::mutex> processGuard((*it)->processMutex);
    return (*it)->module->getState();
}

bool ThreadPoolModuleSystem::restoreModuleState(const std::string& name, const IDataNode& state) {
    std::shared_lock<std::shared_mutex> lock(modulesMutex);
    auto it = findModule(name);
    if (it == modules.end()) return false;
    std::lock_guard<std::mutex> processGuard((*it)->processMutex);
    (*it)->module->setState(state);   // host-owned node (engine-built) — cross-DLL-safe
    return true;
}

ModuleSystemType ThreadPoolModuleSystem::getType() const {
    return ModuleSystemType::THREAD_POOL;
}

int ThreadPoolModuleSystem::getPendingTaskCount(const std::string& moduleName) const {
    std::shared_lock<std::shared_mutex> lock(modulesMutex);
    auto it = findModule(moduleName);
    if (it == modules.end()) return 0;
    // A frame is synchronous (barrier) — between frames nothing is pending. During a frame,
    // report 1 while the module's task may still be in flight (best-effort, like Phase 2).
    return (tasksRemaining.load(std::memory_order_relaxed) > 0) ? 1 : 0;
}

std::unique_ptr<IModule> ThreadPoolModuleSystem::extractModule(const std::string& name) {
    // Called BETWEEN frames. Exclusive registry lock → cannot run during processModules()
    // (which holds it shared for the whole frame), so the frame's tasks are all done and the
    // deques are empty: no in-flight task references this slot.
    std::unique_lock<std::shared_mutex> lock(modulesMutex);
    auto it = findModule(name);
    if (it == modules.end()) {
        logger->warn("⚠️ extractModule: module '{}' not found", name);
        return nullptr;
    }
    // Make sure no concurrent queryModule() is mid-process() on this slot before we move it.
    {
        std::lock_guard<std::mutex> processGuard((*it)->processMutex);
    }
    std::unique_ptr<IModule> mod = std::move((*it)->module);
    modules.erase(it);
    logger->info("📤 Extracted module '{}' from pool (remaining: {})", name, modules.size());
    return mod;
}

// ============================================================================
// ITaskScheduler — immediate-execution stubs (no async queue yet, like Phase 1/2)
// ============================================================================

void ThreadPoolModuleSystem::scheduleTask(const std::string& taskType, std::unique_ptr<IDataNode>) {
    logger->debug("⚙️ Task '{}' executed immediately (no async queue yet)", taskType);
    taskExecutionCount.fetch_add(1, std::memory_order_relaxed);
}

int ThreadPoolModuleSystem::hasCompletedTasks() const {
    return 0;
}

std::unique_ptr<IDataNode> ThreadPoolModuleSystem::getCompletedTask() {
    throw std::runtime_error("ThreadPoolModuleSystem executes tasks immediately — no completed-task queue");
}

// ============================================================================
// Misc
// ============================================================================

size_t ThreadPoolModuleSystem::getModuleCount() const {
    std::shared_lock<std::shared_mutex> lock(modulesMutex);
    return modules.size();
}

std::unique_ptr<IDataNode> ThreadPoolModuleSystem::createInputDataNode(
    float deltaTime, size_t frameCount, const std::string& moduleName) {

    auto now = std::chrono::high_resolution_clock::now();
    nlohmann::json inputJson = {
        {"deltaTime", deltaTime},
        {"frameCount", frameCount},
        {"system", "thread_pool"},
        {"moduleName", moduleName},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count()}
    };
    return std::make_unique<JsonDataNode>("input", inputJson);
}

} // namespace grove
