#pragma once

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <spdlog/spdlog.h>

#include "IEngine.h"
#include "IModuleSystem.h"
#include "IIO.h"
#include "IDataNode.h"
#include "ModuleLoader.h"
#include "EngineClock.h"   // authoritative fixed-timestep clock (owned by value, advanced in step())
#include "crash/CrashContext.h"   // crash-report payload built by snapshotCrashContext()

namespace grove {

namespace crash { class ICrashHandler; }   // fwd — the engine owns one (unique_ptr, dtor in the .cpp)


/**
 * @brief Debug engine implementation with comprehensive logging
 *
 * DebugEngine provides maximum visibility into engine operations:
 * - Verbose logging of all operations
 * - Step-by-step execution capabilities
 * - Module isolation and debugging
 * - Performance metrics and timing
 * - IIO health monitoring and reporting
 * - Detailed socket management logging
 */
class DebugEngine : public IEngine {
private:
    std::shared_ptr<spdlog::logger> logger;
    std::atomic<bool> running{false};
    std::atomic<bool> debugPaused{false};

    // Module management
    std::vector<std::unique_ptr<IModuleSystem>> moduleSystems;
    std::vector<std::string> moduleNames;
    std::vector<std::unique_ptr<ModuleLoader>> moduleLoaders;
    // Per-module routed IIO instance, index-aligned with moduleSystems. Non-null
    // ONLY for static modules (registerStaticModule), which the engine wires +
    // pumps itself; file-loaded modules carry a null slot (they self-wire inside
    // the .so). The IntraIOManager singleton co-owns these — shutdown() drops them.
    std::vector<std::shared_ptr<IIO>> moduleIOs;

    // True (index-aligned with moduleIOs) when this module is hosted in a SHARED
    // WORKER-DRAINED system (threadedSystem_ OR poolSystem_) instead of its own per-module
    // system. The engine must NOT pump such a module's inbox on the engine thread — its
    // worker thread drains it (so its handlers run on the worker thread, not racing
    // process()). "Threaded" here means worker-hosted, covering the pool too. See pumpModuleIO.
    std::vector<bool> moduleIsThreaded_;

    // ONE shared ThreadedModuleSystem hosting ALL threaded static modules, so a single
    // processModules() runs them on parallel worker threads under one barrier (real
    // parallelism). Created lazily on the first registerStaticModule(THREADED). Static
    // modules in here have a null slot in moduleSystems (they are not per-module systems).
    std::unique_ptr<IModuleSystem> threadedSystem_;

    // ONE shared ThreadPoolModuleSystem (Phase 3) hosting ALL pool static modules: N modules
    // distributed over M work-stealing workers under one barrier. Created lazily on the first
    // registerStaticModule(THREAD_POOL). Like threadedSystem_, its modules are worker-drained
    // (moduleIsThreaded_ == true) and have a null moduleSystems slot. The two can coexist.
    std::unique_ptr<IModuleSystem> poolSystem_;

    // Socket management
    std::unique_ptr<IIO> coordinatorSocket;
    std::vector<std::unique_ptr<IIO>> clientSockets;

    // Performance tracking
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    std::chrono::high_resolution_clock::time_point engineStartTime;
    size_t frameCount = 0;

    // The engine's single authoritative simulation clock. Advanced once per step() with the
    // frame's deltaTime; injected read-only into every static module via setClock(). Default
    // fixed timestep (1/60). The host reads + controls it (pause/slow-mo) via clock().
    EngineClock m_clock;

    // Configuration
    std::unique_ptr<IDataNode> engineConfig;

    // Crash reporter (B1c): a process-wide handler installed in initialize() (gated by
    // GROVE_CRASH_REPORTER + skipped under sanitizers). On an unhandled crash it writes a native
    // minidump + a CrashContext JSON (engine clock/frame/modules + the last-N IIO messages) next
    // to it, under crashOutputBase_ ("<base>.dmp" / "<base>.json").
    std::unique_ptr<crash::ICrashHandler> crashHandler_;
    std::string crashOutputBase_ = "logs/crash";

    // Helper methods
    void logEngineStart();
    void logEngineShutdown();
    void logFrameStart(float deltaTime);
    void logFrameEnd(float frameTime);
    void logModuleHealth();
    void logSocketHealth();
    void processModuleSystems(float deltaTime);
    void pumpModuleIO();   // drain each static module's IIO inbox (fire handlers)
    void processClientMessages();
    void processCoordinatorMessages();
    float calculateDeltaTime();
    void validateConfiguration();

    // Crash reporter helpers. installCrashReporter() is called from initialize() (gated);
    // writeCrashReport() is the installed callback — builds a CrashContext and writes its JSON.
    void installCrashReporter();
    void writeCrashReport(const std::string& reason, const std::string& jsonPath) const;

public:
    DebugEngine();
    virtual ~DebugEngine();

    // IEngine implementation
    void initialize() override;
    void run() override;
    void step(float deltaTime) override;
    EngineClock& clock() override;
    void shutdown() override;
    void loadModules(const std::string& configPath) override;
    void registerStaticModule(const std::string& name,
                              std::unique_ptr<IModule> module,
                              ModuleSystemType strategy,
                              std::unique_ptr<IDataNode> config = nullptr) override;
    void registerMainSocket(std::unique_ptr<IIO> coordinatorSocket) override;
    void registerNewClientSocket(std::unique_ptr<IIO> clientSocket) override;
    EngineType getType() const override;

    // Debug-specific methods
    void pauseExecution();
    void resumeExecution();
    void stepSingleFrame();
    bool isPaused() const;
    std::unique_ptr<IDataNode> getDetailedStatus() const;
    void setLogLevel(spdlog::level::level_enum level);

    // Hot-reload methods
    /**
     * @brief Register a module from .so file with hot-reload support
     * @param name Module identifier
     * @param modulePath Path to .so file
     * @param strategy Module system strategy (sequential, threaded, etc.)
     */
    void registerModuleFromFile(const std::string& name, const std::string& modulePath, ModuleSystemType strategy);

    /**
     * @brief Hot-reload a module by name
     * @param name Module identifier to reload
     *
     * This performs zero-downtime hot-reload:
     * 1. Extract state from current module
     * 2. Unload old .so
     * 3. Load new .so (recompiled version)
     * 4. Restore state to new module
     * 5. Continue execution without stopping engine
     */
    void reloadModule(const std::string& name);

    /**
     * @brief Get list of all registered module names
     */
    std::vector<std::string> getModuleNames() const { return moduleNames; }

    /**
     * @brief Redirect where the crash reporter writes its artifacts (default "logs/crash").
     * @param base path prefix — the reporter writes "<base>.dmp" (minidump) + "<base>.json" (context).
     * Call BEFORE initialize() (which installs the handler with this base). Mainly for tests / to
     * place crash artifacts under a per-run directory.
     */
    void setCrashOutputBase(const std::string& base) { crashOutputBase_ = base; }

    /**
     * @brief Snapshot the current engine state into a crash-report context (does NOT require a crash).
     * @param reason a human tag for the report (e.g. an exception name, or "manual").
     * Captures the clock (tick/simTime/timeScale), pause flag, frame count, registered module names,
     * and the last-N IIO messages from the ReplaySink (empty if the sink is disabled — fail-soft).
     * This is exactly what the installed crash handler writes on a real crash; exposed so a game can
     * also grab it for a non-fatal error report, and so it's unit-testable without crashing.
     */
    crash::CrashContext snapshotCrashContext(const std::string& reason) const;

    /**
     * @brief Save the WHOLE engine state (every registered module's getState()) to a JSON file on disk.
     *
     * Iterates the registered modules, captures each one's serialized state into a grove::save::SaveFile, and
     * writes it to `path`. The reverse of loadState(). Built on the same per-module getState()/setState()
     * contract as hot-reload, so a module that hot-reloads correctly also saves/loads correctly.
     *
     * ⚠️ Call BETWEEN frames (not during step()): getState() must not race a module's process(). Currently
     * supports SEQUENTIAL-hosted modules (same limitation as hot-reload / the state dump); THREADED / THREAD_POOL
     * modules are skipped with a warning (follow-on). @return false on any IO/serialization failure.
     */
    bool saveState(const std::string& path);

    /**
     * @brief Restore the engine state from a save file written by saveState().
     *
     * Loads the file, then for each currently-registered module with a matching saved state calls its
     * setState(). Modules absent from the save keep their current state; saved modules no longer registered are
     * ignored (the game may have evolved). A module's setState() throwing on a corrupt state is caught + logged
     * per module, so one bad entry doesn't abort the whole load. @return false only if the file can't be
     * read/parsed; true once applied (per-module failures are warnings).
     */
    bool loadState(const std::string& path);

    /**
     * @brief Dump the current state of a specific module to logs
     * @param name Module identifier
     *
     * Retrieves the module's state via getState() and pretty-prints it
     * as formatted JSON in the logs. Useful for debugging and inspection.
     */
    void dumpModuleState(const std::string& name);

    /**
     * @brief Dump the state of all registered modules to logs
     *
     * Iterates through all modules and dumps their state.
     * Useful for comprehensive system state snapshots.
     */
    void dumpAllModulesState();
};

} // namespace grove