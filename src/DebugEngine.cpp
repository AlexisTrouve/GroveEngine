#include <grove/DebugEngine.h>
#include <grove/BuildConfig.h>      // GROVE_DEBUG / GROVE_DEBUG_ONLY — strip the debug skin in a shipping build
#include <grove/JsonDataNode.h>
#include <grove/JsonDataValue.h>
#include <grove/ModuleSystemFactory.h>
#include <grove/SequentialModuleSystem.h>
#include <grove/ThreadedModuleSystem.h>
#include <grove/ThreadPoolModuleSystem.h>  // Phase 3: dynamic_cast for setModuleInbox + factory create // shared threaded host + setModuleInbox (archi A)
#include <grove/IModule.h>          // full IModule (setConfiguration) for static hosting
#include <grove/IntraIOManager.h>   // routed IIO instances for static modules
#include <grove/IntraIO.h>          // concrete IntraIO (createInstance return type)
#include <grove/save/SaveFile.h>    // whole-engine saveState/loadState
#include <grove/crash/ICrashHandler.h>  // crash reporter: makeCrashHandler + install/uninstall (B1c)
#include <nlohmann/json.hpp>
#include <fstream>
#include <grove/platform/FileSystem.h>
#include <sstream>
#include <logger/Logger.h>

namespace grove {

using json = nlohmann::json;

DebugEngine::DebugEngine() {
    logger = stillhammer::createDomainLogger("DebugEngine", "engine");

    logger->info("🔧 DebugEngine constructor - Maximum logging enabled");
    logger->trace("🏗️ DebugEngine object created at address: {}", static_cast<void*>(this));
}

DebugEngine::~DebugEngine() {
    logger->info("🔧 DebugEngine destructor called");
    if (running.load()) {
        logger->warn("⚠️ Engine still running during destruction - forcing shutdown");
        DebugEngine::shutdown();  // qualified: non-virtual dispatch is intended in the destructor
    }
    logger->trace("🏗️ DebugEngine object destroyed");
}

void DebugEngine::initialize() {
    logger->info("🚀 Initializing DebugEngine...");
    logEngineStart();

    // Create logs directory if it doesn't exist
    grove::fs::createDirectories("logs");
    logger->debug("📁 Ensured logs directory exists");

    engineStartTime = std::chrono::high_resolution_clock::now();
    lastFrameTime = engineStartTime;
    frameCount = 0;

    // Install the crash reporter (best-effort, gated by GROVE_CRASH_REPORTER + skipped under
    // sanitizers). From here on, an unhandled crash writes a minidump + engine-context JSON.
    installCrashReporter();

    logger->info("✅ DebugEngine initialization complete");
    logger->debug("🕐 Engine start time recorded: {}",
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      engineStartTime.time_since_epoch()).count());
}

void DebugEngine::run() {
    logger->info("🏃 Starting DebugEngine main loop");
    logger->debug("🔄 Engine loop type: Continuous with debug capabilities");

    if (!coordinatorSocket) {
        logger->warn("⚠️ No coordinator socket registered - running in isolated mode");
    }

    if (clientSockets.empty()) {
        logger->warn("⚠️ No client sockets registered - no players will connect");
    }

    running.store(true);
    logger->info("✅ DebugEngine marked as running");

    while (running.load()) {
        if (debugPaused.load()) {
            logger->trace("⏸️ Engine paused - waiting for resume or step command");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        float deltaTime = calculateDeltaTime();
        step(deltaTime);

        // FULL VERBOSE: Log EVERY frame
        logger->trace("📊 Frame {}: deltaTime: {:.3f}ms", frameCount, deltaTime * 1000);
    }

    logger->info("🏁 DebugEngine main loop ended");
}

void DebugEngine::step(float deltaTime) {
    // Per-frame verbose logging + frame-timing are the engine's DEBUG SKIN: compiled OUT of a
    // shipping build (GROVE_DEBUG=OFF) so step() carries zero introspection cost. Everything
    // below that isn't gated (clock advance, message/module processing, IIO pump, frameCount)
    // is the PROD CORE and runs identically in both builds.
    GROVE_DEBUG_ONLY(logFrameStart(deltaTime);)

#if GROVE_DEBUG
    auto frameStartTime = std::chrono::high_resolution_clock::now();
#endif

    try {
        // Advance the authoritative engine clock FIRST: fold this frame's real deltaTime into
        // fixed sim steps so (tick, simTime, dt) are current before any module's process() reads
        // them this frame. The clock is the single source of sim time — pause / slow-mo are
        // applied here (timeScale), so a paused engine still runs step() but the sim time holds.
        m_clock.advance(deltaTime);

        // Push this frame's clock snapshot to the IIO router so it can stamp tick/simTime onto
        // every message's envelope this frame (IO contract §5). Lock-free atomic store; must
        // follow advance() so the envelope carries the CURRENT frame's time, not the previous.
        IntraIOManager::getInstance().setSimTime(m_clock.tick(), m_clock.simTime());

        // Process coordinator messages
        if (coordinatorSocket) {
            GROVE_DEBUG_ONLY(logger->trace("📨 Processing coordinator messages");)
            processCoordinatorMessages();
        }

        // Process client messages
        if (!clientSockets.empty()) {
            GROVE_DEBUG_ONLY(logger->trace("👥 Processing {} client socket(s)", clientSockets.size());)
            processClientMessages();
        }

        // Process all module systems FIRST. Self-draining modules (e.g. UIModule)
        // pull their OWN inbox inside process() — and that ordering is load-bearing:
        // UIModule resets per-frame input edges in beginFrame() then reads them in
        // processInput(), so pumping its inbox BEFORE process() would let beginFrame()
        // wipe the click before updateUI() ever sees it. Hence: process, THEN pump.
        if (!moduleSystems.empty()) {
            GROVE_DEBUG_ONLY(logger->trace("🔧 Processing {} module system(s)", moduleSystems.size());)
            processModuleSystems(deltaTime);
        }

        // Deliver any STILL-queued inter-module messages to their handlers — the
        // safety net for modules that do NOT self-drain in process() (without it
        // their routed IIO queues fill but no subscribed handler ever fires). Runs
        // AFTER processing (one-frame delivery latency, standard) so it never races a
        // self-draining module's beginFrame()/processInput() ordering above.
        pumpModuleIO();

        // Health monitoring every 30 frames — DEBUG-ONLY today: the helpers only LOG, no action
        // is taken on the health data yet (the "consider module restart" of IEngine.h is a TODO).
        // When health->action lands, that action must live OUTSIDE this gate so it survives shipping.
#if GROVE_DEBUG
        if (frameCount % 30 == 0) {
            logModuleHealth();
            logSocketHealth();
        }
#endif

        frameCount++;

    } catch (const std::exception& e) {
        logger->error("❌ Exception during step execution: {}", e.what());
        logger->error("🔍 Frame: {}, deltaTime: {:.3f}ms", frameCount, deltaTime * 1000);
        throw; // Re-throw to allow caller to handle
    }

    // Frame-timing + end-of-frame log are DEBUG-ONLY (they exist only to feed logFrameEnd, which
    // reports frame duration + a slow-frame warning). Stripped in a shipping build alongside the
    // frameStartTime that feeds them, so no unused-variable dangles.
#if GROVE_DEBUG
    auto frameEndTime = std::chrono::high_resolution_clock::now();
    float frameTime = std::chrono::duration<float, std::milli>(frameEndTime - frameStartTime).count();
    logFrameEnd(frameTime);
#endif
}

EngineClock& DebugEngine::clock() {
    // The host's handle on simulation time: read (tick/simTime/dt/realTime) and control
    // (pause/resume/setTimeScale). The same clock is injected read-only into every module.
    return m_clock;
}

void DebugEngine::shutdown() {
    logger->info("🛑 DebugEngine shutdown initiated");
    logEngineShutdown();

    // Remove the crash hook FIRST — once we start tearing down modules/sockets, the engine state
    // the reporter would snapshot is no longer coherent, and we don't want a shutdown-time fault
    // writing a misleading report.
    if (crashHandler_) { crashHandler_->uninstall(); crashHandler_.reset(); }

    running.store(false);
    logger->debug("🔄 Running flag set to false");

    // Shutdown all module systems
    if (!moduleSystems.empty()) {
        logger->info("🔧 Shutting down {} module system(s)", moduleSystems.size());
        for (size_t i = 0; i < moduleSystems.size(); ++i) {
            logger->debug("🔧 Shutting down module system: {}", moduleNames[i]);
            // Note: ModuleSystems don't have shutdown in interface yet
            // This would be added when implementing IModuleSystem
        }
        // Destroy the MODULES FIRST (clearing the systems drops the held modules),
        // while their routed IIO instances are still alive. ORDER IS LOAD-BEARING: a
        // hosted module keeps a raw IIO* (e.g. UIModule::m_io); dropping its IIO before
        // the module would dangle that pointer and use-after-free at module teardown
        // (intermittent SIGSEGV). Resources must outlive their users.
        moduleSystems.clear();
        // Destroy the SHARED threaded system too (joins every worker thread, then destroys
        // the threaded modules). MUST happen here — before dropping the IIO instances —
        // because each threaded module holds a raw IIO* (setConfiguration) and each worker
        // holds a shared_ptr to its inbox; both must die while their IIO instance is alive.
        threadedSystem_.reset();
        // Same teardown invariant for the pool system: destroy its workers + modules before we
        // drop the IIO instances (each pool module holds a raw IIO*, each worker a shared_ptr inbox).
        poolSystem_.reset();
        // Now safe: drop each static module's routed IIO instance from the global
        // router (the manager co-owns it). tryGetLiveInstance() never CREATES the
        // singleton — null if no static module was ever hosted, so we skip (teardown-safe).
        if (IntraIOManager* mgr = IntraIOManager::tryGetLiveInstance()) {
            for (size_t i = 0; i < moduleIOs.size() && i < moduleNames.size(); ++i) {
                if (moduleIOs[i]) mgr->removeInstance(moduleNames[i]);
            }
        }
        moduleIOs.clear();
        moduleNames.clear();
        moduleIsThreaded_.clear();
        logger->info("✅ All module systems shut down");
    }

    // Clear sockets
    if (coordinatorSocket) {
        logger->debug("🔌 Clearing coordinator socket");
        coordinatorSocket.reset();
    }

    if (!clientSockets.empty()) {
        logger->info("👥 Clearing {} client socket(s)", clientSockets.size());
        clientSockets.clear();
    }

    logger->info("✅ DebugEngine shutdown complete");

    // Final statistics
    auto shutdownTime = std::chrono::high_resolution_clock::now();
    auto totalRunTime = std::chrono::duration<float>(shutdownTime - engineStartTime).count();
    logger->info("📊 Total engine runtime: {:.2f} seconds", totalRunTime);
    logger->info("📊 Total frames processed: {}", frameCount);
    if (totalRunTime > 0) {
        logger->info("📊 Average FPS: {:.2f}", frameCount / totalRunTime);
    }
}

void DebugEngine::loadModules(const std::string& configPath) {
    logger->info("📦 Loading modules from config: {}", configPath);

    try {
        // Read configuration file
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            logger->error("❌ Cannot open config file: {}", configPath);
            throw std::runtime_error("Config file not found: " + configPath);
        }

        json config;
        configFile >> config;
        logger->debug("✅ Config file parsed successfully");
        logger->trace("📄 Config content: {}", config.dump(2));

        // Validate configuration
        validateConfiguration();

        if (!config.contains("modules")) {
            logger->warn("⚠️ No 'modules' section in config - no modules to load");
            return;
        }

        auto modules = config["modules"];
        logger->info("🔍 Found {} module(s) to load", modules.size());

        for (size_t i = 0; i < modules.size(); ++i) {
            const auto& moduleConfig = modules[i];
            logger->info("📦 Loading module {}/{}", i + 1, modules.size());

            if (!moduleConfig.contains("path") || !moduleConfig.contains("strategy")) {
                logger->error("❌ Module config missing 'path' or 'strategy': {}", moduleConfig.dump());
                continue;
            }

            std::string modulePath = moduleConfig["path"];
            std::string strategy = moduleConfig["strategy"];
            std::string frequency = moduleConfig.value("frequency", "60hz");

            logger->info("📂 Module path: {}", modulePath);
            logger->info("⚙️ Module strategy: {}", strategy);
            logger->info("⏱️ Module frequency: {}", frequency);

            // TODO: Create appropriate ModuleSystem based on strategy
            // For now, we'll log what would be created
            logger->info("🚧 TODO: Create {} ModuleSystem for {}", strategy, modulePath);
            logger->debug("🔮 Future: Load dynamic library from {}", modulePath);
            logger->debug("🔮 Future: Instantiate module and wrap in {} system", strategy);

            // Store module name for tracking
            moduleNames.push_back(modulePath);
        }

        logger->info("✅ Module loading configuration processed");

    } catch (const std::exception& e) {
        logger->error("❌ Failed to load modules: {}", e.what());
        throw;
    }
}

void DebugEngine::registerMainSocket(std::unique_ptr<IIO> socket) {
    logger->info("🔌 Registering main coordinator socket");

    if (coordinatorSocket) {
        logger->warn("⚠️ Coordinator socket already exists - replacing");
    }

    coordinatorSocket = std::move(socket);
    logger->info("✅ Main coordinator socket registered");
    logger->debug("🔍 Socket type: {}", static_cast<int>(coordinatorSocket->getType()));
}

void DebugEngine::registerNewClientSocket(std::unique_ptr<IIO> clientSocket) {
    logger->info("👥 Registering new client socket (client #{})", clientSockets.size() + 1);

    logger->debug("🔍 Client socket type: {}", static_cast<int>(clientSocket->getType()));
    clientSockets.push_back(std::move(clientSocket));

    logger->info("✅ Client socket registered - Total clients: {}", clientSockets.size());
}

EngineType DebugEngine::getType() const {
    logger->trace("🏷️ Engine type requested: DEBUG");
    return EngineType::DEBUG;
}

// Debug-specific methods
void DebugEngine::pauseExecution() {
    logger->info("⏸️ Pausing engine execution");
    debugPaused.store(true);
    logger->debug("🔄 Debug pause flag set to true");
}

void DebugEngine::resumeExecution() {
    logger->info("▶️ Resuming engine execution");
    debugPaused.store(false);
    logger->debug("🔄 Debug pause flag set to false");
}

void DebugEngine::stepSingleFrame() {
#if GROVE_DEBUG
    logger->info("👣 Executing single frame step");
    if (debugPaused.load()) {
        float deltaTime = calculateDeltaTime();
        step(deltaTime);
        logger->debug("✅ Single frame step completed");
    } else {
        logger->warn("⚠️ Cannot step single frame - engine not paused");
    }
#else
    // Shipping build: single-frame step-debugging is a debug-only tool (no-op; symbol kept).
#endif
}

bool DebugEngine::isPaused() const {
    bool paused = debugPaused.load();
    logger->trace("🔍 Pause status requested: {}", paused ? "PAUSED" : "RUNNING");
    return paused;
}

std::unique_ptr<IDataNode> DebugEngine::getDetailedStatus() const {
#if GROVE_DEBUG
    logger->debug("📊 Detailed status requested");

    json status = {
        {"type", "DEBUG"},
        {"running", running.load()},
        {"paused", debugPaused.load()},
        {"frame_count", frameCount},
        {"modules_loaded", moduleNames.size()},
        {"client_sockets", clientSockets.size()},
        {"has_coordinator", coordinatorSocket != nullptr}
    };

    // Add runtime info
    if (frameCount > 0) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration<float>(currentTime - engineStartTime).count();
        status["runtime_seconds"] = totalTime;
        status["average_fps"] = frameCount / totalTime;
    }

    logger->trace("📄 Status: {}", status.dump());
    return std::make_unique<JsonDataNode>("status", status);
#else
    // Shipping build: engine introspection is stripped. The symbol stays (callers still link) but
    // returns a minimal marker node instead of the rich internal snapshot — a shipping build must
    // neither expose that detail nor spend cycles building it.
    json status = { {"type", "DEBUG"}, {"introspection", "stripped"} };
    return std::make_unique<JsonDataNode>("status", status);
#endif
}

void DebugEngine::setLogLevel(spdlog::level::level_enum level) {
    logger->info("🔧 Setting log level to: {}", spdlog::level::to_string_view(level));
    logger->set_level(level);
    logger->debug("✅ Log level updated");
}

// Private helper methods
void DebugEngine::logEngineStart() {
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("🏭 WARFACTORY DEBUG ENGINE STARTING");
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("🎯 Engine Type: DEBUG (Maximum visibility mode)");
    logger->info("📊 Logging Level: TRACE (Everything logged)");
    logger->info("🔧 Features: Step debugging, health monitoring, performance tracking");
}

void DebugEngine::logEngineShutdown() {
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("🏭 WARFACTORY DEBUG ENGINE SHUTTING DOWN");
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
}

void DebugEngine::logFrameStart(float deltaTime) {
    logger->trace("🎬 Frame {} START - deltaTime: {:.3f}ms", frameCount, deltaTime * 1000);
}

void DebugEngine::logFrameEnd(float frameTime) {
    logger->trace("🏁 Frame {} END - frameTime: {:.3f}ms", frameCount, frameTime);

    // Warn about slow frames
    if (frameTime > 16.67f) { // More than 60fps target
        logger->warn("🐌 Slow frame detected: {:.2f}ms (target: <16.67ms for 60fps)", frameTime);
    }
}

void DebugEngine::logModuleHealth() {
    if (moduleSystems.empty()) {
        logger->debug("🏥 Module health check: No modules loaded");
        return;
    }

    logger->debug("🏥 Module health check: {} module system(s)", moduleSystems.size());

    for (size_t i = 0; i < moduleSystems.size(); ++i) {
        // TODO: When IModuleSystem has health methods, check them here
        logger->trace("🔍 Module '{}': Status unknown (health interface not implemented)", moduleNames[i]);
    }
}

void DebugEngine::logSocketHealth() {
    logger->debug("🌐 Socket health check:");

    if (coordinatorSocket) {
        auto health = coordinatorSocket->getHealth();
        logger->debug("📡 Coordinator socket: Queue={}/{}, Dropping={}, Rate={:.1f}msg/s",
                     health.queueSize, health.maxQueueSize, health.dropping, health.averageProcessingRate);

        if (health.dropping) {
            logger->warn("⚠️ Coordinator socket dropping messages!");
        }
        if (health.queueSize > health.maxQueueSize * 0.8) {
            logger->warn("⚠️ Coordinator socket queue 80% full ({}/{})", health.queueSize, health.maxQueueSize);
        }
    }

    for (size_t i = 0; i < clientSockets.size(); ++i) {
        auto health = clientSockets[i]->getHealth();
        logger->debug("👤 Client socket {}: Queue={}/{}, Dropping={}, Rate={:.1f}msg/s",
                     i, health.queueSize, health.maxQueueSize, health.dropping, health.averageProcessingRate);

        if (health.dropping) {
            logger->warn("⚠️ Client socket {} dropping messages!", i);
        }
    }
}

void DebugEngine::processModuleSystems(float deltaTime) {
    logger->trace("⚙️ Processing {} module system(s)", moduleSystems.size());

    for (size_t i = 0; i < moduleSystems.size(); ++i) {
        // Null slot = a threaded static module hosted in the shared threadedSystem_
        // (processed once, below) rather than in its own per-module system.
        if (!moduleSystems[i]) continue;
        logger->trace("🔧 Processing module system: {}", moduleNames[i]);

        try {
            moduleSystems[i]->processModules(deltaTime);

        } catch (const std::exception& e) {
            logger->error("❌ Module '{}' crashed: {}", moduleNames[i], e.what());
            logger->error("🔍 Frame: {}, deltaTime: {:.3f}ms", frameCount, deltaTime * 1000);

            // Automatic recovery attempt
            try {
                logger->info("🔄 Attempting automatic recovery for module '{}'...", moduleNames[i]);

                // Reload the module (will preserve state if possible)
                reloadModule(moduleNames[i]);

                logger->info("✅ Recovery successful for module '{}'", moduleNames[i]);

            } catch (const std::exception& recoveryError) {
                logger->critical("❌ Recovery failed for module '{}': {}", moduleNames[i], recoveryError.what());
                logger->critical("⚠️ Module '{}' is now in a failed state and will be skipped", moduleNames[i]);
                // Continue processing other modules - don't crash the entire engine
            }
        }
    }

    // The SHARED threaded system runs ALL its modules in PARALLEL under one barrier
    // (real parallelism). One processModules() call drives every threaded worker. A
    // crash here can't hot-reload (static modules) — log and keep the engine alive.
    if (threadedSystem_) {
        try {
            threadedSystem_->processModules(deltaTime);
        } catch (const std::exception& e) {
            logger->error("❌ Shared threaded system crashed: {} (frame {})", e.what(), frameCount);
        }
    }

    // The SHARED pool system (Phase 3) runs ALL its modules over the work-stealing worker pool
    // under one barrier — one call drives every pool task. Same crash-isolation as above.
    if (poolSystem_) {
        try {
            poolSystem_->processModules(deltaTime);
        } catch (const std::exception& e) {
            logger->error("❌ Shared pool system crashed: {} (frame {})", e.what(), frameCount);
        }
    }
}

void DebugEngine::processClientMessages() {
    for (size_t i = 0; i < clientSockets.size(); ++i) {
        auto& socket = clientSockets[i];
        int messageCount = socket->hasMessages();

        if (messageCount > 0) {
            logger->trace("📨 Client {} has {} pending message(s)", i, messageCount);

            // Process a few messages per frame to avoid blocking
            int messagesToProcess = (std::min)(messageCount, 5);

            for (int j = 0; j < messagesToProcess; ++j) {
                try {
                    socket->pullAndDispatch();

                    // TODO: Route message to appropriate module or process it
                    logger->trace("🚧 TODO: Route client message to modules");

                } catch (const std::exception& e) {
                    logger->error("❌ Error processing client {} message: {}", i, e.what());
                }
            }
        }
    }
}

void DebugEngine::processCoordinatorMessages() {
    int messageCount = coordinatorSocket->hasMessages();

    if (messageCount > 0) {
        logger->trace("📨 Coordinator has {} pending message(s)", messageCount);

        // Process coordinator messages with higher priority
        int messagesToProcess = (std::min)(messageCount, 10);

        for (int i = 0; i < messagesToProcess; ++i) {
            try {
                coordinatorSocket->pullAndDispatch();

                // TODO: Handle coordinator commands (shutdown, config reload, etc.)
                logger->trace("🚧 TODO: Handle coordinator commands");

            } catch (const std::exception& e) {
                logger->error("❌ Error processing coordinator message: {}", e.what());
            }
        }
    }
}

float DebugEngine::calculateDeltaTime() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;

    // Cap delta time to avoid huge jumps (e.g., after debugging pause)
    if (deltaTime > 0.1f) {
        logger->trace("⏱️ Large deltaTime detected: {:.3f}s - capping to 100ms", deltaTime);
        deltaTime = 0.1f;
    }

    return deltaTime;
}

void DebugEngine::validateConfiguration() {
    logger->debug("✅ Configuration validation passed");
    // TODO: Add actual validation logic
    logger->trace("🚧 TODO: Implement comprehensive config validation");
}

// ============================================================================
// Static module hosting — the entry point for static-linked games.
// ============================================================================
void DebugEngine::registerStaticModule(const std::string& name,
                                       std::unique_ptr<IModule> module,
                                       ModuleSystemType strategy,
                                       std::unique_ptr<IDataNode> config) {
    // QUOI : héberge un module DÉJÀ instancié dans l'engine, sans .so/.dll.
    // POURQUOI : le seul chemin d'entrée existant (registerModuleFromFile) charge
    //   depuis un .so et — pire — ne câblait jamais setConfiguration ni l'IIO, donc
    //   un jeu static-linké devait contourner l'engine entièrement. C'est LE fix :
    //   on prend le module, on le câble dans le vrai routeur, on le pilote.
    // COMMENT : 1. ModuleSystem par module (même factory que le chemin .so),
    //   2. on crée l'instance IIO ROUTÉE du module (IntraIOManager singleton) —
    //      c'est le routeur qui porte réellement le trafic inter-modules, pas le
    //      setIOLayer() vestigial, 3. setConfiguration(config, io, scheduler) où le
    //      ModuleSystem fait office d'ITaskScheduler (il EN est un), AVANT tout
    //      process(), 4. registerModule, 5. stockage index-aligné (slot loader nul
    //      = pas de .so → reloadModule le refuse proprement).
    logger->info("📦 Registering STATIC module '{}' (no .so/.dll)", name);
    logger->debug("⚙️ Module system strategy: {}", static_cast<int>(strategy));

    if (!module) {
        logger->error("❌ Cannot register a null static module '{}'", name);
        throw std::invalid_argument("registerStaticModule: null module for '" + name + "'");
    }

    try {
        // Routed IIO instance for this module — same regardless of strategy. The manager
        // co-owns it; we keep a handle to pump/drop it. A null config becomes an empty node.
        // coreResident=true: a STATIC module is linked into the core for the whole process, so its
        // published nodes never dangle past an .so unload → publish() shares them by pointer (TRUE
        // zero-copy, 0 json copies). Hot-loaded .so modules self-wire their IIO and stay re-homed.
        std::shared_ptr<IIO> io = IntraIOManager::getInstance().createInstance(name, /*coreResident=*/true);
        std::unique_ptr<IDataNode> cfg = config
            ? std::move(config)
            : std::make_unique<JsonDataNode>("config", json::object());

        // Inject the engine's authoritative clock (read-only) BEFORE the module is moved into
        // its system. Strategy-independent (the clock is engine-global), so done once here
        // rather than in each branch. Same long-lived-service-injection pattern as the io +
        // scheduler passed to setConfiguration below; m_clock outlives every module. Timeless
        // modules ignore it (default no-op).
        module->setClock(&m_clock);

        if (strategy == ModuleSystemType::THREADED) {
            // REAL PARALLELISM: every threaded static module shares ONE ThreadedModuleSystem,
            // so a single processModules() runs them all on parallel worker threads under one
            // barrier. (Per-module threaded systems would be driven sequentially by the engine
            // = threads but no parallelism.) Created lazily on the first threaded module.
            if (!threadedSystem_) {
                threadedSystem_ = ModuleSystemFactory::create(ModuleSystemType::THREADED);
            }
            // Configure with the shared system as the scheduler, then register + hand over
            // the routed inbox so the module's OWN worker thread drains it (archi A): its
            // subscribe handlers run on the worker thread, not the engine thread.
            module->setConfiguration(*cfg, io.get(), threadedSystem_.get());
            threadedSystem_->registerModule(name, std::move(module));
            if (auto* ts = dynamic_cast<ThreadedModuleSystem*>(threadedSystem_.get())) {
                ts->setModuleInbox(name, io);
            }
            moduleLoaders.push_back(nullptr);   // static module → no .so loader
            moduleSystems.push_back(nullptr);   // hosted by threadedSystem_, not a per-module system
            moduleIOs.push_back(std::move(io));
            moduleNames.push_back(name);
            moduleIsThreaded_.push_back(true);
            logger->info("✅ Static module '{}' registered into the SHARED threaded system (parallel) — total: {}",
                         name, moduleNames.size());
        } else if (strategy == ModuleSystemType::THREAD_POOL) {
            // Phase 3: every pool static module shares ONE ThreadPoolModuleSystem — N modules
            // distributed over M work-stealing workers under one barrier. Same archi A as
            // THREADED: the worker that runs a module's frame task drains its inbox right after
            // process(), so the engine must NOT pump it (moduleIsThreaded_ == true below).
            if (!poolSystem_) {
                poolSystem_ = ModuleSystemFactory::create(ModuleSystemType::THREAD_POOL);
            }
            module->setConfiguration(*cfg, io.get(), poolSystem_.get());
            poolSystem_->registerModule(name, std::move(module));
            if (auto* ps = dynamic_cast<ThreadPoolModuleSystem*>(poolSystem_.get())) {
                ps->setModuleInbox(name, io);
            }
            moduleLoaders.push_back(nullptr);   // static module → no .so loader
            moduleSystems.push_back(nullptr);   // hosted by poolSystem_, not a per-module system
            moduleIOs.push_back(std::move(io));
            moduleNames.push_back(name);
            moduleIsThreaded_.push_back(true);  // worker-drained → engine must not pump its inbox
            logger->info("✅ Static module '{}' registered into the SHARED pool system (work-stealing) — total: {}",
                         name, moduleNames.size());
        } else {
            // SEQUENTIAL (and any future per-module strategy): one ModuleSystem per module,
            // driven on the engine thread; the engine pumps its inbox (pumpModuleIO) after
            // process() — load-bearing ordering for self-draining UI modules.
            auto moduleSystem = ModuleSystemFactory::create(strategy);
            module->setConfiguration(*cfg, io.get(), moduleSystem.get());
            moduleSystem->registerModule(name, std::move(module));
            moduleLoaders.push_back(nullptr);
            moduleSystems.push_back(std::move(moduleSystem));
            moduleIOs.push_back(std::move(io));
            moduleNames.push_back(name);
            moduleIsThreaded_.push_back(false);
            logger->info("✅ Static module '{}' registered + wired (total: {})", name, moduleNames.size());
        }

    } catch (const std::exception& e) {
        logger->error("❌ Failed to register static module '{}': {}", name, e.what());
        throw;
    }
}

// Drain every static module's IIO inbox so its subscribed handlers actually fire.
// QUOI : pour chaque instance IIO de module, pullAndDispatch jusqu'à vide.
// POURQUOI : la livraison IntraIO est EN FILE — publish() enfile dans l'instance
//   cible, et le callback enregistré par subscribe() ne se déclenche QUE sur
//   pullAndDispatch(). Sans ce drain, l'engine appellerait process() sur des
//   modules qui ne reçoivent jamais un seul message (l'IIO mort signalé).
// COMMENT : appelé une fois par step(), AVANT processModuleSystems(), pour que le
//   trafic entrant soit livré avant le process() de chaque module (latence 1 frame
//   par saut, standard). Cap par instance pour qu'un handler qui se republie à
//   lui-même ne fasse pas tourner la frame à l'infini. Slots nuls (modules .so qui
//   se câblent eux-mêmes) ignorés.
void DebugEngine::pumpModuleIO() {
    constexpr int kMaxDrainPerFrame = 100000;  // garde-fou anti boucle de self-republish
    for (size_t i = 0; i < moduleIOs.size(); ++i) {
        if (!moduleIOs[i]) continue;
        // Threaded modules drain their OWN inbox on their worker thread (archi A) — the
        // engine must NOT also pull here, or handlers would fire on the engine thread and
        // race the module's process() on its worker thread. Skip them.
        if (i < moduleIsThreaded_.size() && moduleIsThreaded_[i]) continue;
        int drained = 0;
        while (moduleIOs[i]->hasMessages() > 0 && drained < kMaxDrainPerFrame) {
            moduleIOs[i]->pullAndDispatch();
            ++drained;
        }
    }
}

// Hot-reload methods
void DebugEngine::registerModuleFromFile(const std::string& name, const std::string& modulePath, ModuleSystemType strategy) {
    logger->info("📦 Registering module '{}' from file: {}", name, modulePath);
    logger->debug("⚙️ Module system strategy: {}", static_cast<int>(strategy));

    try {
        // Create module loader
        auto loader = std::make_unique<ModuleLoader>();

        // Load module from .so file
        logger->debug("📥 Loading module from: {}", modulePath);
        auto module = loader->load(modulePath, name);

        // Create module system with specified strategy
        logger->debug("🏗️ Creating module system with strategy {}", static_cast<int>(strategy));
        auto moduleSystem = ModuleSystemFactory::create(strategy);

        // Register module with system
        logger->debug("🔗 Registering module with system");
        moduleSystem->registerModule(name, std::move(module));

        // Store everything (index-aligned with moduleIOs — a file-loaded module
        // self-wires its IIO inside the .so, so it gets a null slot here).
        moduleLoaders.push_back(std::move(loader));
        moduleSystems.push_back(std::move(moduleSystem));
        moduleIOs.push_back(nullptr);
        moduleNames.push_back(name);
        moduleIsThreaded_.push_back(false);  // .so module self-wires its IIO; engine doesn't pump/own it

        logger->info("✅ Module '{}' registered successfully", name);
        logger->debug("📊 Total modules loaded: {}", moduleNames.size());

    } catch (const std::exception& e) {
        logger->error("❌ Failed to register module '{}': {}", name, e.what());
        throw;
    }
}

void DebugEngine::reloadModule(const std::string& name) {
    logger->info("🔄 Hot-reloading module '{}'", name);

    auto reloadStartTime = std::chrono::high_resolution_clock::now();

    try {
        // Find module index
        auto it = std::find(moduleNames.begin(), moduleNames.end(), name);
        if (it == moduleNames.end()) {
            logger->error("❌ Module '{}' not found", name);
            throw std::runtime_error("Module not found: " + name);
        }

        size_t index = std::distance(moduleNames.begin(), it);
        logger->debug("🔍 Found module '{}' at index {}", name, index);

        // A static module (registerStaticModule) has no .so loader — there is
        // nothing to reload from. Refuse cleanly instead of dereferencing the null
        // loader slot (which the auto-recovery path in processModuleSystems could
        // otherwise hit and crash on).
        if (!moduleLoaders[index]) {
            logger->error("❌ Module '{}' is STATIC (no .so) — hot-reload not supported", name);
            throw std::runtime_error("Cannot hot-reload a static module: " + name);
        }

        // Get references
        auto& moduleSystem = moduleSystems[index];
        auto& loader = moduleLoaders[index];

        // Step 1: Extract module from system
        logger->debug("📤 Step 1/4: Extracting module from system");

        // Try to cast to SequentialModuleSystem to access extractModule()
        auto* seqSystem = dynamic_cast<SequentialModuleSystem*>(moduleSystem.get());
        if (!seqSystem) {
            logger->error("❌ Hot-reload only supported for SequentialModuleSystem currently");
            throw std::runtime_error("Hot-reload not supported for this module system type");
        }

        auto currentModule = seqSystem->extractModule();
        if (!currentModule) {
            logger->error("❌ Failed to extract module from system");
            throw std::runtime_error("Failed to extract module");
        }

        logger->debug("✅ Module extracted successfully");

        // Step 2: Wait for clean state (module idle + no pending tasks)
        logger->debug("⏳ Step 2/5: Waiting for clean state");
        bool cleanState = loader->waitForCleanState(currentModule.get(), seqSystem, 5.0f);
        if (!cleanState) {
            logger->error("❌ Module did not reach clean state within timeout");
            throw std::runtime_error("Hot-reload timeout - module not idle or has pending tasks");
        }
        logger->debug("✅ Clean state reached");

        // Step 3: Get current state
        logger->debug("💾 Step 3/5: Extracting module state");
        auto currentState = currentModule->getState();
        logger->debug("✅ State extracted successfully");

        // Step 4: Reload module via loader
        logger->debug("🔄 Step 4/5: Reloading .so file");
        auto newModule = loader->reload(std::move(currentModule));
        logger->debug("✅ Module reloaded successfully");

        // Step 5: Register new module back with system
        logger->debug("🔗 Step 5/5: Registering new module with system");
        moduleSystem->registerModule(name, std::move(newModule));
        logger->debug("✅ Module registered successfully");

        auto reloadEndTime = std::chrono::high_resolution_clock::now();
        float reloadTime = std::chrono::duration<float, std::milli>(reloadEndTime - reloadStartTime).count();

        logger->info("✅ Hot-reload of '{}' completed in {:.3f}ms", name, reloadTime);

    } catch (const std::exception& e) {
        logger->error("❌ Hot-reload failed for '{}': {}", name, e.what());
        throw;
    }
}

void DebugEngine::dumpModuleState(const std::string& name) {
#if GROVE_DEBUG
    logger->info("╔══════════════════════════════════════════════════════════════");
    logger->info("║ 📊 STATE DUMP: {}", name);
    logger->info("╠══════════════════════════════════════════════════════════════");

    try {
        // Find module index
        auto it = std::find(moduleNames.begin(), moduleNames.end(), name);
        if (it == moduleNames.end()) {
            logger->error("║ ❌ Module '{}' not found", name);
            logger->info("╚══════════════════════════════════════════════════════════════");
            return;
        }

        size_t index = std::distance(moduleNames.begin(), it);
        auto& moduleSystem = moduleSystems[index];

        // Try to cast to SequentialModuleSystem to access module
        auto* seqSystem = dynamic_cast<SequentialModuleSystem*>(moduleSystem.get());
        if (!seqSystem) {
            logger->warn("║ ⚠️ State dump only supported for SequentialModuleSystem currently");
            logger->info("╚══════════════════════════════════════════════════════════════");
            return;
        }

        // Extract module temporarily
        auto module = seqSystem->extractModule();
        if (!module) {
            logger->error("║ ❌ Failed to extract module");
            logger->info("╚══════════════════════════════════════════════════════════════");
            return;
        }

        // Get state
        auto state = module->getState();

        // Cast to JsonDataNode to access JSON
        auto* jsonNode = dynamic_cast<JsonDataNode*>(state.get());
        if (!jsonNode) {
            logger->warn("║ ⚠️ State is not JsonDataNode, cannot dump as JSON");
            moduleSystem->registerModule(name, std::move(module));
            logger->info("╚══════════════════════════════════════════════════════════════");
            return;
        }

        // Convert to JSON and pretty print
        const auto& jsonState = jsonNode->getJsonData();
        std::string prettyJson = jsonState.dump(2);  // 2 spaces indentation

        // Split into lines and print with border
        std::istringstream stream(prettyJson);
        std::string line;
        while (std::getline(stream, line)) {
            logger->info("║ {}", line);
        }

        // Re-register module (we only borrowed it)
        moduleSystem->registerModule(name, std::move(module));

        logger->info("╚══════════════════════════════════════════════════════════════");

    } catch (const std::exception& e) {
        logger->error("║ ❌ Error dumping state: {}", e.what());
        logger->info("╚══════════════════════════════════════════════════════════════");
    }
#else
    (void)name;  // Shipping build: state-dump introspection stripped (symbol kept for linkers).
#endif
}

// Whole-engine SAVE: capture every SEQUENTIAL-hosted module's getState() into a SaveFile and write it to disk.
// Non-destructive (getModule(), not extractModule()). THREADED/THREAD_POOL modules are skipped with a warning
// (same limitation as hot-reload / the state dump). Call between frames — getState() must not race process().
bool DebugEngine::saveState(const std::string& path) {
    save::SaveFile sf;
    int captured = 0, skipped = 0;
    for (size_t i = 0; i < moduleNames.size(); ++i) {
        auto* seq = (i < moduleSystems.size()) ? dynamic_cast<SequentialModuleSystem*>(moduleSystems[i].get())
                                               : nullptr;
        if (!seq) {
            logger->warn("💾 saveState: module '{}' skipped (only SequentialModuleSystem supported)", moduleNames[i]);
            ++skipped; continue;
        }
        if (IModule* m = seq->getModule()) { sf.captureModule(moduleNames[i], *m); ++captured; }
    }
    if (!sf.save(path)) { logger->error("💾 saveState: failed to write '{}'", path); return false; }
    logger->info("💾 saveState: {} module(s) saved to '{}' ({} skipped)", captured, path, skipped);
    return true;
}

// Whole-engine LOAD: read the SaveFile and apply each saved state to the matching registered module via
// setState(). Fail-soft per module (a corrupt state that makes setState() throw is caught + logged, not fatal);
// modules absent from the save keep their state, saved modules no longer present are ignored.
bool DebugEngine::loadState(const std::string& path) {
    save::SaveFile sf;
    if (!sf.load(path)) { logger->error("📂 loadState: failed to read/parse '{}'", path); return false; }
    int restored = 0;
    for (size_t i = 0; i < moduleNames.size(); ++i) {
        auto* seq = (i < moduleSystems.size()) ? dynamic_cast<SequentialModuleSystem*>(moduleSystems[i].get())
                                               : nullptr;
        if (!seq) continue;
        IModule* m = seq->getModule();
        if (!m || !sf.has(moduleNames[i])) continue;
        try {
            if (sf.restoreInto(moduleNames[i], *m)) ++restored;
        } catch (const std::exception& e) {
            logger->warn("📂 loadState: module '{}' rejected the saved state: {}", moduleNames[i], e.what());
        }
    }
    logger->info("📂 loadState: {} module(s) restored from '{}'", restored, path);
    return true;
}

void DebugEngine::dumpAllModulesState() {
#if GROVE_DEBUG
    logger->info("╔══════════════════════════════════════════════════════════════");
    logger->info("║ 📊 DUMPING ALL MODULE STATES ({} modules)", moduleNames.size());
    logger->info("╚══════════════════════════════════════════════════════════════");

    for (const auto& moduleName : moduleNames) {
        dumpModuleState(moduleName);
        logger->info("");  // Blank line between modules
    }

    logger->info("✅ All module states dumped");
#endif
    // Shipping build: no-op (all module-state dumping is debug-only introspection).
}

// --- Crash reporter (B1c) --------------------------------------------------------------------

crash::CrashContext DebugEngine::snapshotCrashContext(const std::string& reason) const {
    crash::CrashContext ctx;
    ctx.reason      = reason;
    ctx.tick        = m_clock.tick();
    ctx.simTime     = m_clock.simTime();
    ctx.timeScale   = m_clock.timeScale();
    ctx.paused      = debugPaused.load();
    ctx.frameCount  = frameCount;
    ctx.moduleNames = moduleNames;

    // The last-N IIO messages from the OPT-IN ReplaySink — the event trail that led here, the most
    // valuable post-mortem artifact for a message-bus engine. Fail-soft: a disabled sink yields an
    // empty timeline → no recentMessages. Bounded to the tail so a huge ring can't bloat the report.
    const auto timeline = IntraIOManager::getInstance().replaySink().timeline();
    constexpr size_t kMaxTrail = 200;
    const size_t start = timeline.size() > kMaxTrail ? timeline.size() - kMaxTrail : 0;
    ctx.recentMessages.reserve(timeline.size() - start);
    for (size_t i = start; i < timeline.size(); ++i) {
        const auto& e = timeline[i];
        ctx.recentMessages.push_back(crash::MessageTrace{
            e.topic, e.env.source, e.env.tick, e.env.seq, e.env.lamport});
    }
    return ctx;
}

void DebugEngine::writeCrashReport(const std::string& reason, const std::string& jsonPath) const {
    // Runs IN a crash context: keep it minimal and DON'T log (spdlog's mutex could be poisoned by
    // the fault). Just build the context and write the JSON — that file is the artifact.
    const crash::CrashContext ctx = snapshotCrashContext(reason);
    std::ofstream out(jsonPath);
    if (out) out << crash::toJson(ctx).dump(2);
}

void DebugEngine::installCrashReporter() {
#if GROVE_CRASH_REPORTER && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    crashHandler_ = crash::makeCrashHandler();
    crashHandler_->setDumpPath(crashOutputBase_ + ".dmp");
    const std::string jsonPath = crashOutputBase_ + ".json";
    // On a crash the handler calls back here (with `this` + the json path) to write the context.
    // The handler is a member → uninstalled when the engine dies, so `this` never dangles.
    crashHandler_->install([this, jsonPath](const std::string& reason) {
        this->writeCrashReport(reason, jsonPath);
    });
    logger->info("🧯 Crash reporter installed (artifacts: {}.dmp / {}.json)", crashOutputBase_, crashOutputBase_);
#else
    logger->debug("🧯 Crash reporter disabled (GROVE_CRASH_REPORTER off or sanitizer build)");
#endif
}

} // namespace grove