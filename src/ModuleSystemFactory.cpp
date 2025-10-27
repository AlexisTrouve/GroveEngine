#include <warfactory/ModuleSystemFactory.h>
#include <algorithm>
#include <thread>
#include <spdlog/sinks/stdout_color_sinks.h>

// Include implemented systems
#include <warfactory/SequentialModuleSystem.h>
// Forward declarations for future implementations
// #include "ThreadedModuleSystem.h"
// #include "ThreadPoolModuleSystem.h"
// #include "ClusterModuleSystem.h"

namespace warfactory {

std::unique_ptr<IModuleSystem> ModuleSystemFactory::create(const std::string& strategy) {
    auto logger = getFactoryLogger();
    logger->info("⚙️ ModuleSystemFactory: Creating strategy '{}'", strategy);

    ModuleSystemType type = parseStrategy(strategy);
    return create(type);
}

std::unique_ptr<IModuleSystem> ModuleSystemFactory::create(ModuleSystemType systemType) {
    auto logger = getFactoryLogger();
    std::string typeStr = strategyToString(systemType);
    logger->info("⚙️ ModuleSystemFactory: Creating enum type '{}'", typeStr);

    std::unique_ptr<IModuleSystem> moduleSystem;

    switch (systemType) {
        case ModuleSystemType::SEQUENTIAL:
            logger->debug("🔧 Creating SequentialModuleSystem instance");
            moduleSystem = std::make_unique<SequentialModuleSystem>();
            logger->info("✅ SequentialModuleSystem created successfully");
            break;

        case ModuleSystemType::THREADED:
            logger->debug("🔧 Creating ThreadedModuleSystem instance");
            // TODO: Implement ThreadedModuleSystem
            // moduleSystem = std::make_unique<ThreadedModuleSystem>();
            logger->error("❌ ThreadedModuleSystem not yet implemented");
            throw std::invalid_argument("ThreadedModuleSystem not yet implemented");

        case ModuleSystemType::THREAD_POOL:
            logger->debug("🔧 Creating ThreadPoolModuleSystem instance");
            // TODO: Implement ThreadPoolModuleSystem
            // moduleSystem = std::make_unique<ThreadPoolModuleSystem>();
            logger->error("❌ ThreadPoolModuleSystem not yet implemented");
            throw std::invalid_argument("ThreadPoolModuleSystem not yet implemented");

        case ModuleSystemType::CLUSTER:
            logger->debug("🔧 Creating ClusterModuleSystem instance");
            // TODO: Implement ClusterModuleSystem
            // moduleSystem = std::make_unique<ClusterModuleSystem>();
            logger->error("❌ ClusterModuleSystem not yet implemented");
            throw std::invalid_argument("ClusterModuleSystem not yet implemented");

        default:
            logger->error("❌ Unknown ModuleSystemType enum value: {}", static_cast<int>(systemType));
            throw std::invalid_argument("Unknown ModuleSystemType enum value: " + std::to_string(static_cast<int>(systemType)));
    }

    logger->debug("🎯 ModuleSystem type verification: created system reports type '{}'",
                 strategyToString(moduleSystem->getType()));

    return moduleSystem;
}

std::unique_ptr<IModuleSystem> ModuleSystemFactory::createFromConfig(const json& config) {
    auto logger = getFactoryLogger();
    logger->info("⚙️ ModuleSystemFactory: Creating from config");
    logger->trace("📄 Config: {}", config.dump());

    try {
        if (!config.contains("strategy")) {
            logger->error("❌ Config missing 'strategy' field");
            throw std::invalid_argument("ModuleSystem config missing 'strategy' field");
        }

        std::string strategy = config["strategy"];
        logger->info("📋 Config specifies strategy: '{}'", strategy);

        // Create base ModuleSystem
        auto moduleSystem = create(strategy);

        // Apply additional configuration based on strategy type
        auto systemType = moduleSystem->getType();

        if (systemType == ModuleSystemType::THREAD_POOL) {
            if (config.contains("thread_count")) {
                int threadCount = config["thread_count"];
                logger->info("🔧 Thread pool config: {} threads", threadCount);
                // TODO: Apply thread count when ThreadPoolModuleSystem is implemented
            }

            if (config.contains("queue_size")) {
                int queueSize = config["queue_size"];
                logger->info("🔧 Thread pool config: queue size {}", queueSize);
                // TODO: Apply queue size when ThreadPoolModuleSystem is implemented
            }
        }

        if (config.contains("priority")) {
            std::string priority = config["priority"];
            logger->info("🔧 ModuleSystem priority: {}", priority);
            // TODO: Apply priority settings when implementations support it
        }

        logger->info("✅ ModuleSystem created from config successfully");
        return moduleSystem;

    } catch (const json::exception& e) {
        logger->error("❌ JSON parsing error in config: {}", e.what());
        throw std::invalid_argument("Invalid JSON in ModuleSystem config: " + std::string(e.what()));
    } catch (const std::exception& e) {
        logger->error("❌ Error creating ModuleSystem from config: {}", e.what());
        throw;
    }
}

std::vector<std::string> ModuleSystemFactory::getAvailableStrategies() {
    return {
        "sequential",
        "threaded",
        "thread_pool",
        "cluster"
    };
}

bool ModuleSystemFactory::isStrategySupported(const std::string& strategy) {
    try {
        parseStrategy(strategy);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

ModuleSystemType ModuleSystemFactory::parseStrategy(const std::string& strategyStr) {
    auto logger = getFactoryLogger();
    std::string lowerStrategy = toLowercase(strategyStr);

    logger->trace("🔍 Parsing strategy: '{}' -> '{}'", strategyStr, lowerStrategy);

    if (lowerStrategy == "sequential") {
        return ModuleSystemType::SEQUENTIAL;
    } else if (lowerStrategy == "threaded") {
        return ModuleSystemType::THREADED;
    } else if (lowerStrategy == "thread_pool" || lowerStrategy == "threadpool" || lowerStrategy == "thread-pool") {
        return ModuleSystemType::THREAD_POOL;
    } else if (lowerStrategy == "cluster") {
        return ModuleSystemType::CLUSTER;
    } else {
        logger->error("❌ Unknown strategy: '{}'", strategyStr);
        auto availableStrategies = getAvailableStrategies();
        std::string availableStr = "[";
        for (size_t i = 0; i < availableStrategies.size(); ++i) {
            availableStr += availableStrategies[i];
            if (i < availableStrategies.size() - 1) availableStr += ", ";
        }
        availableStr += "]";

        throw std::invalid_argument("Unknown strategy '" + strategyStr + "'. Available strategies: " + availableStr);
    }
}

std::string ModuleSystemFactory::strategyToString(ModuleSystemType systemType) {
    switch (systemType) {
        case ModuleSystemType::SEQUENTIAL:
            return "sequential";
        case ModuleSystemType::THREADED:
            return "threaded";
        case ModuleSystemType::THREAD_POOL:
            return "thread_pool";
        case ModuleSystemType::CLUSTER:
            return "cluster";
        default:
            return "unknown";
    }
}

ModuleSystemType ModuleSystemFactory::getRecommendedStrategy(int targetFPS, int moduleCount, int cpuCores) {
    auto logger = getFactoryLogger();

    if (cpuCores == 0) {
        cpuCores = detectCpuCores();
    }

    logger->debug("🎯 Recommending strategy for: {}fps, {} modules, {} cores",
                 targetFPS, moduleCount, cpuCores);

    // Simple recommendation logic
    if (moduleCount <= 1) {
        logger->debug("💡 Single module -> SEQUENTIAL");
        return ModuleSystemType::SEQUENTIAL;
    } else if (moduleCount <= cpuCores && targetFPS <= 30) {
        logger->debug("💡 Few modules, low FPS -> THREADED");
        return ModuleSystemType::THREADED;
    } else if (targetFPS > 30 || moduleCount > cpuCores) {
        logger->debug("💡 High performance needs -> THREAD_POOL");
        return ModuleSystemType::THREAD_POOL;
    } else {
        logger->debug("💡 Default fallback -> SEQUENTIAL");
        return ModuleSystemType::SEQUENTIAL;
    }
}

// Private helper methods
std::shared_ptr<spdlog::logger> ModuleSystemFactory::getFactoryLogger() {
    static std::shared_ptr<spdlog::logger> logger = nullptr;

    if (!logger) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);

        logger = std::make_shared<spdlog::logger>("ModuleSystemFactory", console_sink);
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::debug);

        spdlog::register_logger(logger);
    }

    return logger;
}

std::string ModuleSystemFactory::toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return std::tolower(c); });
    return result;
}

int ModuleSystemFactory::detectCpuCores() {
    int cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 4; // Fallback
    return cores;
}

} // namespace warfactory