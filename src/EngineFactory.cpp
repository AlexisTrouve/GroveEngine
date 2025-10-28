#include <grove/EngineFactory.h>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>

using json = nlohmann::json;

namespace grove {

std::unique_ptr<IEngine> EngineFactory::createEngine(const std::string& engineType) {
    auto logger = getFactoryLogger();
    logger->info("🏭 EngineFactory: Creating engine of type '{}'", engineType);

    EngineType type = parseEngineType(engineType);
    return createEngine(type);
}

std::unique_ptr<IEngine> EngineFactory::createEngine(EngineType engineType) {
    auto logger = getFactoryLogger();
    std::string typeStr = engineTypeToString(engineType);
    logger->info("🏭 EngineFactory: Creating engine of enum type '{}'", typeStr);

    std::unique_ptr<IEngine> engine;

    switch (engineType) {
        case EngineType::DEBUG:
            logger->debug("🔧 Creating DebugEngine instance");
            engine = std::make_unique<DebugEngine>();
            logger->info("✅ DebugEngine created successfully");
            break;

        case EngineType::PRODUCTION:
            logger->error("❌ ProductionEngine not yet implemented");
            throw std::invalid_argument("ProductionEngine not yet implemented - use DEBUG for now");

        case EngineType::HIGH_PERFORMANCE:
            logger->error("❌ HighPerformanceEngine not yet implemented");
            throw std::invalid_argument("HighPerformanceEngine not yet implemented - use DEBUG for now");

        default:
            logger->error("❌ Unknown engine type enum value: {}", static_cast<int>(engineType));
            throw std::invalid_argument("Unknown engine type enum value: " + std::to_string(static_cast<int>(engineType)));
    }

    logger->debug("🎯 Engine type verification: created engine reports type '{}'",
                 engineTypeToString(engine->getType()));

    return engine;
}

std::unique_ptr<IEngine> EngineFactory::createFromConfig(const std::string& configPath) {
    auto logger = getFactoryLogger();
    logger->info("🏭 EngineFactory: Creating engine from config '{}'", configPath);

    try {
        // Read configuration file
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            logger->error("❌ Cannot open config file: {}", configPath);
            throw std::runtime_error("Cannot open engine config file: " + configPath);
        }

        json config;
        configFile >> config;
        logger->debug("✅ Config file parsed successfully");

        // Extract engine configuration
        if (!config.contains("engine")) {
            logger->error("❌ Config file missing 'engine' section");
            throw std::runtime_error("Config file missing 'engine' section");
        }

        auto engineConfig = config["engine"];

        if (!engineConfig.contains("type")) {
            logger->error("❌ Engine config missing 'type' field");
            throw std::runtime_error("Engine config missing 'type' field");
        }

        std::string engineType = engineConfig["type"];
        logger->info("📋 Config specifies engine type: '{}'", engineType);

        // Create engine
        auto engine = createEngine(engineType);

        // Apply additional configuration if available
        if (engineConfig.contains("log_level")) {
            std::string logLevel = engineConfig["log_level"];
            logger->info("🔧 Config specifies log level: '{}'", logLevel);

            // Apply log level if engine supports it (DebugEngine does)
            if (engine->getType() == EngineType::DEBUG) {
                auto debugEngine = static_cast<DebugEngine*>(engine.get());

                if (logLevel == "trace") debugEngine->setLogLevel(spdlog::level::trace);
                else if (logLevel == "debug") debugEngine->setLogLevel(spdlog::level::debug);
                else if (logLevel == "info") debugEngine->setLogLevel(spdlog::level::info);
                else if (logLevel == "warn") debugEngine->setLogLevel(spdlog::level::warn);
                else if (logLevel == "error") debugEngine->setLogLevel(spdlog::level::err);
                else {
                    logger->warn("⚠️ Unknown log level '{}' - using default", logLevel);
                }
            }
        }

        if (engineConfig.contains("features")) {
            auto features = engineConfig["features"];
            logger->debug("🎛️ Engine features configuration found: {}", features.dump());
            // TODO: Apply feature configuration when engines support it
        }

        logger->info("✅ Engine created from config successfully");
        return engine;

    } catch (const json::exception& e) {
        logger->error("❌ JSON parsing error in config file: {}", e.what());
        throw std::runtime_error("Invalid JSON in engine config file: " + std::string(e.what()));
    } catch (const std::exception& e) {
        logger->error("❌ Error creating engine from config: {}", e.what());
        throw;
    }
}

std::vector<std::string> EngineFactory::getAvailableEngineTypes() {
    return {
        "debug",
        "production",      // Not yet implemented
        "high_performance" // Not yet implemented
    };
}

bool EngineFactory::isEngineTypeSupported(const std::string& engineType) {
    try {
        parseEngineType(engineType);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

EngineType EngineFactory::parseEngineType(const std::string& engineTypeStr) {
    auto logger = getFactoryLogger();
    std::string lowerType = toLowercase(engineTypeStr);

    logger->trace("🔍 Parsing engine type: '{}' -> '{}'", engineTypeStr, lowerType);

    if (lowerType == "debug") {
        return EngineType::DEBUG;
    } else if (lowerType == "production") {
        return EngineType::PRODUCTION;
    } else if (lowerType == "high_performance" || lowerType == "high-performance" || lowerType == "highperformance") {
        return EngineType::HIGH_PERFORMANCE;
    } else {
        logger->error("❌ Unknown engine type: '{}'", engineTypeStr);
        auto availableTypes = getAvailableEngineTypes();
        std::string availableStr = "[";
        for (size_t i = 0; i < availableTypes.size(); ++i) {
            availableStr += availableTypes[i];
            if (i < availableTypes.size() - 1) availableStr += ", ";
        }
        availableStr += "]";

        throw std::invalid_argument("Unknown engine type '" + engineTypeStr + "'. Available types: " + availableStr);
    }
}

std::string EngineFactory::engineTypeToString(EngineType engineType) {
    switch (engineType) {
        case EngineType::DEBUG:
            return "debug";
        case EngineType::PRODUCTION:
            return "production";
        case EngineType::HIGH_PERFORMANCE:
            return "high_performance";
        default:
            return "unknown";
    }
}

// Private helper methods
std::shared_ptr<spdlog::logger> EngineFactory::getFactoryLogger() {
    static std::shared_ptr<spdlog::logger> logger = nullptr;

    if (!logger) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);

        logger = std::make_shared<spdlog::logger>("EngineFactory", console_sink);
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::debug);

        // Register globally
        spdlog::register_logger(logger);
    }

    return logger;
}

std::string EngineFactory::toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return std::tolower(c); });
    return result;
}

} // namespace grove