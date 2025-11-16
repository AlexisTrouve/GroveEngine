#include "ErrorRecoveryModule.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <stdexcept>
#include <csignal>

namespace grove {

void ErrorRecoveryModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    // Logger
    logger = spdlog::get("ErrorRecoveryModule");
    if (!logger) {
        logger = spdlog::stdout_color_mt("ErrorRecoveryModule");
    }
    logger->set_level(spdlog::level::debug);

    // Clone config
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        config = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    } else {
        config = std::make_unique<JsonDataNode>("config");
    }

    // Lire configuration
    crashAtFrame = configNode.getInt("crashAtFrame", -1);
    crashType = configNode.getInt("crashType", 0);
    enableAutoRecovery = configNode.getBool("enableAutoRecovery", true);
    versionTag = configNode.getString("versionTag", "v1.0");

    logger->info("Initializing ErrorRecoveryModule");
    logger->info("  Version: {}", versionTag);
    logger->info("  Crash at frame: {}", crashAtFrame);
    logger->info("  Crash type: {}", crashType);
    logger->info("  Auto-recovery enabled: {}", enableAutoRecovery);

    frameCount = 0;
    crashCount = 0;
    recoveryCount = 0;
    hasCrashed = false;
}

const IDataNode& ErrorRecoveryModule::getConfiguration() {
    return *config;
}

void ErrorRecoveryModule::process(const IDataNode& input) {
    isProcessing = true;
    frameCount++;

    // Si crash planifié à cette frame précise
    if (crashAtFrame > 0 && frameCount == crashAtFrame) {
        triggerConfiguredCrash();
    }

    isProcessing = false;
}

void ErrorRecoveryModule::triggerConfiguredCrash() {
    crashCount++;
    hasCrashed = true;

    logger->warn("💥 CRASH TRIGGERED at frame {}", frameCount);
    logger->warn("   Crash type: {}", crashType);

    switch (crashType) {
        case 0:
            logger->error("Throwing runtime_error");
            throw std::runtime_error("CRASH: Controlled runtime error at frame " + std::to_string(frameCount));

        case 1:
            logger->error("Throwing logic_error");
            throw std::logic_error("CRASH: Logic error at frame " + std::to_string(frameCount));

        case 2:
            logger->error("Throwing out_of_range");
            throw std::out_of_range("CRASH: Out of range at frame " + std::to_string(frameCount));

        case 3:
            logger->error("Throwing domain_error");
            throw std::domain_error("CRASH: Domain error at frame " + std::to_string(frameCount));

        default:
            logger->error("Unknown crash type, defaulting to runtime_error");
            throw std::runtime_error("CRASH: Unknown crash type at frame " + std::to_string(frameCount));
    }
}

std::unique_ptr<IDataNode> ErrorRecoveryModule::getHealthStatus() {
    nlohmann::json healthJson;
    healthJson["status"] = hasCrashed ? "crashed" : "healthy";
    healthJson["frameCount"] = frameCount;
    healthJson["crashCount"] = crashCount;
    healthJson["recoveryCount"] = recoveryCount;
    healthJson["versionTag"] = versionTag;
    return std::make_unique<JsonDataNode>("health", healthJson);
}

void ErrorRecoveryModule::shutdown() {
    logger->info("Shutting down ErrorRecoveryModule");
    logger->info("  Version: {}", versionTag);
    logger->info("  Total frames: {}", frameCount);
    logger->info("  Crashes: {}", crashCount);
    logger->info("  Recoveries: {}", recoveryCount);
}

std::string ErrorRecoveryModule::getType() const {
    return "error-recovery";
}

std::unique_ptr<IDataNode> ErrorRecoveryModule::getState() {
    nlohmann::json json;
    json["frameCount"] = frameCount;
    json["crashCount"] = crashCount;
    json["recoveryCount"] = recoveryCount;
    json["hasCrashed"] = hasCrashed;
    json["versionTag"] = versionTag;
    json["crashAtFrame"] = crashAtFrame;

    return std::make_unique<JsonDataNode>("state", json);
}

void ErrorRecoveryModule::setState(const IDataNode& state) {
    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&state);
    if (!jsonNode) {
        if (logger) {
            logger->error("setState: Invalid state (not JsonDataNode)");
        }
        return;
    }

    const auto& json = jsonNode->getJsonData();

    // Ensure logger is initialized (needed after hot-reload)
    if (!logger) {
        logger = spdlog::get("ErrorRecoveryModule");
        if (!logger) {
            logger = spdlog::stdout_color_mt("ErrorRecoveryModule");
        }
    }

    // Ensure config is initialized (needed after hot-reload)
    if (!config) {
        config = std::make_unique<JsonDataNode>("config");
    }

    // AUTO-RECOVERY: Si le module avait crashé et que auto-recovery est activé
    bool hadCrashed = json.value("hasCrashed", false);
    if (hadCrashed && enableAutoRecovery) {
        logger->warn("🔄 AUTO-RECOVERY TRIGGERED");
        logger->warn("   Module had crashed before reload");
        logger->warn("   Applying recovery strategy...");

        // Récupérer l'état mais reset le flag de crash
        frameCount = json.value("frameCount", 0);
        crashCount = json.value("crashCount", 0);
        recoveryCount = json.value("recoveryCount", 0) + 1; // Incrémenter recovery count
        hasCrashed = false; // RECOVERY: On n'est plus en état crashé

        // Désactiver le crash planifié pour éviter de re-crasher
        crashAtFrame = -1;

        versionTag = json.value("versionTag", "v1.0");

        logger->info("✅ RECOVERY SUCCESSFUL");
        logger->info("   Frame count preserved: {}", frameCount);
        logger->info("   Recovery count: {}", recoveryCount);
        logger->info("   Crash trigger disabled");
        return;
    }

    // État normal (pas de crash)
    frameCount = json.value("frameCount", 0);
    crashCount = json.value("crashCount", 0);
    recoveryCount = json.value("recoveryCount", 0);
    hasCrashed = json.value("hasCrashed", false);
    versionTag = json.value("versionTag", "v1.0");
    crashAtFrame = json.value("crashAtFrame", -1);

    logger->info("State restored: frame {}, crashes {}, recoveries {}, version {}",
                 frameCount, crashCount, recoveryCount, versionTag);
}

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule() {
        return new grove::ErrorRecoveryModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
