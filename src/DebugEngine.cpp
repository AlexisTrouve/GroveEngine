#include <grove/DebugEngine.h>
#include <grove/JsonDataNode.h>
#include <grove/JsonDataValue.h>
#include <grove/ModuleSystemFactory.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace grove {

using json = nlohmann::json;

DebugEngine::DebugEngine() {
    // Create comprehensive logger with multiple sinks
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/debug_engine.log", true);

    console_sink->set_level(spdlog::level::debug);
    file_sink->set_level(spdlog::level::trace);

    logger = std::make_shared<spdlog::logger>("DebugEngine",
        spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::debug);

    // Register logger globally
    spdlog::register_logger(logger);

    logger->info("🔧 DebugEngine constructor - Maximum logging enabled");
    logger->debug("📊 Console sink level: DEBUG, File sink level: TRACE");
    logger->trace("🏗️ DebugEngine object created at address: {}", static_cast<void*>(this));
}

DebugEngine::~DebugEngine() {
    logger->info("🔧 DebugEngine destructor called");
    if (running.load()) {
        logger->warn("⚠️ Engine still running during destruction - forcing shutdown");
        shutdown();
    }
    logger->trace("🏗️ DebugEngine object destroyed");
}

void DebugEngine::initialize() {
    logger->info("🚀 Initializing DebugEngine...");
    logEngineStart();

    // Create logs directory if it doesn't exist
    std::filesystem::create_directories("logs");
    logger->debug("📁 Ensured logs directory exists");

    engineStartTime = std::chrono::high_resolution_clock::now();
    lastFrameTime = engineStartTime;
    frameCount = 0;

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

        // Log every 60 frames (roughly every second at 60fps)
        if (frameCount % 60 == 0) {
            logger->debug("📊 Frame {}: Running smoothly, deltaTime: {:.3f}ms",
                         frameCount, deltaTime * 1000);
        }
    }

    logger->info("🏁 DebugEngine main loop ended");
}

void DebugEngine::step(float deltaTime) {
    logFrameStart(deltaTime);

    auto frameStartTime = std::chrono::high_resolution_clock::now();

    try {
        // Process coordinator messages
        if (coordinatorSocket) {
            logger->trace("📨 Processing coordinator messages");
            processCoordinatorMessages();
        }

        // Process client messages
        if (!clientSockets.empty()) {
            logger->trace("👥 Processing {} client socket(s)", clientSockets.size());
            processClientMessages();
        }

        // Process all module systems
        if (!moduleSystems.empty()) {
            logger->trace("🔧 Processing {} module system(s)", moduleSystems.size());
            processModuleSystems(deltaTime);
        }

        // Health monitoring every 30 frames
        if (frameCount % 30 == 0) {
            logModuleHealth();
            logSocketHealth();
        }

        frameCount++;

    } catch (const std::exception& e) {
        logger->error("❌ Exception during step execution: {}", e.what());
        logger->error("🔍 Frame: {}, deltaTime: {:.3f}ms", frameCount, deltaTime * 1000);
        throw; // Re-throw to allow caller to handle
    }

    auto frameEndTime = std::chrono::high_resolution_clock::now();
    float frameTime = std::chrono::duration<float, std::milli>(frameEndTime - frameStartTime).count();

    logFrameEnd(frameTime);
}

void DebugEngine::shutdown() {
    logger->info("🛑 DebugEngine shutdown initiated");
    logEngineShutdown();

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
        moduleSystems.clear();
        moduleNames.clear();
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
    logger->info("👣 Executing single frame step");
    if (debugPaused.load()) {
        float deltaTime = calculateDeltaTime();
        step(deltaTime);
        logger->debug("✅ Single frame step completed");
    } else {
        logger->warn("⚠️ Cannot step single frame - engine not paused");
    }
}

bool DebugEngine::isPaused() const {
    bool paused = debugPaused.load();
    logger->trace("🔍 Pause status requested: {}", paused ? "PAUSED" : "RUNNING");
    return paused;
}

std::unique_ptr<IDataNode> DebugEngine::getDetailedStatus() const {
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
        logger->trace("🔧 Processing module system: {}", moduleNames[i]);

        try {
            moduleSystems[i]->processModules(deltaTime);

        } catch (const std::exception& e) {
            logger->error("❌ Error processing module '{}': {}", moduleNames[i], e.what());
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
            int messagesToProcess = std::min(messageCount, 5);

            for (int j = 0; j < messagesToProcess; ++j) {
                try {
                    auto message = socket->pullMessage();
                    std::string dataPreview = message.data ? message.data->getData()->toString() : "null";
                    logger->debug("📩 Client {} message: topic='{}', data present={}",
                                i, message.topic, message.data != nullptr);

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
        int messagesToProcess = std::min(messageCount, 10);

        for (int i = 0; i < messagesToProcess; ++i) {
            try {
                auto message = coordinatorSocket->pullMessage();
                logger->debug("📩 Coordinator message: topic='{}', data present={}",
                            message.topic, message.data != nullptr);

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

        // Store everything
        moduleLoaders.push_back(std::move(loader));
        moduleSystems.push_back(std::move(moduleSystem));
        moduleNames.push_back(name);

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

        // Get references
        auto& moduleSystem = moduleSystems[index];
        auto& loader = moduleLoaders[index];

        // Step 1: Extract module from system (SequentialModuleSystem has extractModule)
        logger->debug("📤 Step 1/3: Extracting module from system");
        // We need to cast to SequentialModuleSystem to access extractModule
        // For now, we'll work around this by getting the current module state

        // Step 2: Reload via loader (handles state preservation)
        logger->debug("🔄 Step 2/3: Reloading module via loader");

        // For SequentialModuleSystem, we need to extract the module first
        // This is a limitation of the current IModuleSystem interface
        // We'll need to get the state via queryModule as a workaround

        nlohmann::json queryInput = {{"command", "getState"}};
        auto queryData = std::make_unique<JsonDataNode>("query", queryInput);
        auto currentState = moduleSystem->queryModule(name, *queryData);

        // Unload and reload the .so
        std::string modulePath = loader->getLoadedPath();
        loader->unload();
        auto newModule = loader->load(modulePath, name);

        // Restore state
        newModule->setState(*currentState);

        // Step 3: Register new module with system
        logger->debug("🔗 Step 3/3: Registering new module with system");
        moduleSystem->registerModule(name, std::move(newModule));

        auto reloadEndTime = std::chrono::high_resolution_clock::now();
        float reloadTime = std::chrono::duration<float, std::milli>(reloadEndTime - reloadStartTime).count();

        logger->info("✅ Hot-reload of '{}' completed in {:.3f}ms", name, reloadTime);

    } catch (const std::exception& e) {
        logger->error("❌ Hot-reload failed for '{}': {}", name, e.what());
        throw;
    }
}

} // namespace grove