#include "ChaosModule.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <stdexcept>

namespace grove {

void ChaosModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    // Logger
    // Always drop and recreate the logger on setConfiguration.
    // spdlog::get() CAN return a non-null shared_ptr whose underlying sink was
    // compiled into a *previous* DLL load — that sink's vtable is now dangling
    // after dlclose(). Calling it causes SIGSEGV after many hot-reloads.
    // drop() is always safe (no-op if not present). Recreating is the only safe path.
    spdlog::drop("ChaosModule");
    logger = spdlog::stdout_color_mt("ChaosModule");
    logger->set_level(spdlog::level::debug);

    // Clone config
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        config = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    } else {
        config = std::make_unique<JsonDataNode>("config");
    }

    // Lire config
    int seed = configNode.getInt("seed", 42);
    hotReloadProbability = static_cast<float>(configNode.getDouble("hotReloadProbability", 0.30));
    crashProbability = static_cast<float>(configNode.getDouble("crashProbability", 0.10));
    corruptionProbability = static_cast<float>(configNode.getDouble("corruptionProbability", 0.10));
    invalidConfigProbability = static_cast<float>(configNode.getDouble("invalidConfigProbability", 0.05));

    // Initialiser RNG
    rng.seed(seed);

    logger->info("Initializing ChaosModule");
    logger->info("  Seed: {}", seed);
    logger->info("  Hot-reload probability: {}", hotReloadProbability);
    logger->info("  Crash probability: {}", crashProbability);
    logger->info("  Corruption probability: {}", corruptionProbability);
    logger->info("  Invalid config probability: {}", invalidConfigProbability);

    frameCount = 0;
    crashCount = 0;
    corruptionCount = 0;
    hotReloadCount = 0;
}

const IDataNode& ChaosModule::getConfiguration() {
    return *config;
}

void ChaosModule::process(const IDataNode& input) {
    isProcessing = true;
    frameCount++;

    // VRAI CHAOS: Tire aléatoirement À CHAQUE FRAME
    // Pas de "toutes les 60 frames", on peut crasher N'IMPORTE QUAND
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(rng);

    // Crash aléatoire (10% par frame = crash très fréquent)
    if (roll < crashProbability) {
        triggerChaosEvent();
    }

    // Si état corrompu, on DOIT crasher
    if (isCorrupted) {
        logger->error("❌ FATAL: State is corrupted! Module cannot continue.");
        throw std::runtime_error("FATAL: State corrupted - module is in invalid state");
    }

    isProcessing = false;
}

void ChaosModule::triggerChaosEvent() {
    crashCount++;

    // Plusieurs TYPES de crashes différents pour tester la robustesse
    std::uniform_int_distribution<int> crashTypeDist(0, 4);
    int crashType = crashTypeDist(rng);

    switch (crashType) {
        case 0:
            logger->warn("💥 Chaos: CRASH - runtime_error");
            throw std::runtime_error("CRASH: Simulated runtime error at frame " + std::to_string(frameCount));

        case 1:
            logger->warn("💥 Chaos: CRASH - logic_error");
            throw std::logic_error("CRASH: Logic error - invalid state transition at frame " + std::to_string(frameCount));

        case 2:
            logger->warn("💥 Chaos: CRASH - out_of_range");
            throw std::out_of_range("CRASH: Out of range access at frame " + std::to_string(frameCount));

        case 3:
            logger->warn("💥 Chaos: CRASH - domain_error");
            throw std::domain_error("CRASH: Domain error in computation at frame " + std::to_string(frameCount));

        case 4:
            // STATE CORRUPTION (plus vicieux - l'état devient invalide)
            logger->warn("☠️  Chaos: STATE CORRUPTION - module will fail on next frame");
            corruptionCount++;
            isCorrupted = true;
            // Pas de throw ici - on va crasher à la PROCHAINE frame
            break;
    }
}

std::unique_ptr<IDataNode> ChaosModule::getHealthStatus() {
    nlohmann::json healthJson;
    healthJson["status"] = isCorrupted ? "corrupted" : "healthy";
    healthJson["frameCount"] = frameCount;
    healthJson["crashCount"] = crashCount;
    healthJson["corruptionCount"] = corruptionCount;
    healthJson["hotReloadCount"] = hotReloadCount;
    return std::make_unique<JsonDataNode>("health", healthJson);
}

void ChaosModule::shutdown() {
    logger->info("Shutting down ChaosModule");
    logger->info("  Total frames: {}", frameCount);
    logger->info("  Crashes: {}", crashCount);
    logger->info("  Corruptions: {}", corruptionCount);
    logger->info("  Hot-reloads: {}", hotReloadCount);
}

std::string ChaosModule::getType() const {
    return "chaos";
}

std::unique_ptr<IDataNode> ChaosModule::getState() {
    nlohmann::json json;
    json["frameCount"] = frameCount;
    json["crashCount"] = crashCount;
    json["corruptionCount"] = corruptionCount;
    json["hotReloadCount"] = hotReloadCount;
    json["isCorrupted"] = isCorrupted;
    json["seed"] = 42; // Pour reproductibilité

    return std::make_unique<JsonDataNode>("state", json);
}

void ChaosModule::setState(const IDataNode& state) {
    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&state);
    if (!jsonNode) {
        if (logger) {
            logger->error("setState: Invalid state (not JsonDataNode)");
        }
        return;
    }

    const auto& json = jsonNode->getJsonData();

    // Ensure logger is initialized (needed after hot-reload).
    // We never reuse a registry entry: it may point to a sink compiled into a
    // now-unloaded DLL (dangling vtable). Always drop+recreate after reload.
    if (!logger) {
        spdlog::drop("ChaosModule");
        logger = spdlog::stdout_color_mt("ChaosModule");
    }

    // Ensure config is initialized (needed after hot-reload)
    if (!config) {
        config = std::make_unique<JsonDataNode>("config");
    }

    // VALIDATION CRITIQUE: Refuser l'état corrompu
    bool wasCorrupted = json.value("isCorrupted", false);
    if (wasCorrupted) {
        logger->error("🚫 REJECTED: Cannot restore corrupted state!");
        logger->error("   The module was corrupted before hot-reload.");
        logger->error("   Resetting to clean state instead.");

        // Reset à un état propre au lieu de restaurer la corruption
        frameCount = 0;
        crashCount = 0;
        corruptionCount = 0;
        hotReloadCount = 0;
        isCorrupted = false;

        int seed = json.value("seed", 42);
        rng.seed(seed);

        logger->warn("⚠️  State reset due to corruption - module continues with fresh state");
        return;
    }

    // Restaurer state sain
    frameCount = json.value("frameCount", 0);
    crashCount = json.value("crashCount", 0);
    corruptionCount = json.value("corruptionCount", 0);
    hotReloadCount = json.value("hotReloadCount", 0);
    isCorrupted = false; // Toujours false après validation

    int seed = json.value("seed", 42);
    rng.seed(seed);

    logger->info("State restored: frame {}, crashes {}, corruptions {}, hotReloads {}",
                 frameCount, crashCount, corruptionCount, hotReloadCount);
}

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule() {
        return new grove::ChaosModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
