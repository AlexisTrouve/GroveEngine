#include "TankModule.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cmath>

namespace grove {

void TankModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    // Logger
    logger = spdlog::get("TankModule");
    if (!logger) {
        logger = spdlog::stdout_color_mt("TankModule");
    }
    logger->set_level(spdlog::level::debug);

    // Clone config en JSON (pour pouvoir retourner dans getConfiguration)
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        config = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    } else {
        // Fallback: créer un config vide
        config = std::make_unique<JsonDataNode>("config");
    }

    // Lire config
    int tankCount = configNode.getInt("tankCount", 50);
    moduleVersion = configNode.getString("version", moduleVersion);

    logger->info("Initializing TankModule {}", moduleVersion);
    logger->info("  Tank count: {}", tankCount);

    // Spawner tanks
    spawnTanks(tankCount);

    frameCount = 0;
}

const IDataNode& TankModule::getConfiguration() {
    return *config;
}

void TankModule::process(const IDataNode& input) {
    // Extract deltaTime from input (assurez-vous que l'input contient "deltaTime")
    float deltaTime = static_cast<float>(input.getDouble("deltaTime", 1.0 / 60.0));

    frameCount++;

    // Update tous les tanks
    for (auto& tank : tanks) {
        updateTank(tank, deltaTime);
    }

    // Log toutes les 60 frames (1 seconde)
    if (frameCount % 60 == 0) {
        logger->trace("Frame {}: {} tanks active", frameCount, tanks.size());
    }
}

std::unique_ptr<IDataNode> TankModule::getHealthStatus() {
    nlohmann::json healthJson;
    healthJson["status"] = "healthy";
    healthJson["tankCount"] = tanks.size();
    healthJson["frameCount"] = frameCount;
    auto health = std::make_unique<JsonDataNode>("health", healthJson);
    return health;
}

void TankModule::shutdown() {
    logger->info("Shutting down TankModule");
    tanks.clear();
}

std::string TankModule::getType() const {
    return "tank";
}

std::unique_ptr<IDataNode> TankModule::getState() {
    nlohmann::json json;

    json["version"] = moduleVersion;
    json["frameCount"] = frameCount;

    // Sérialiser tanks
    nlohmann::json tanksJson = nlohmann::json::array();
    for (const auto& tank : tanks) {
        tanksJson.push_back({
            {"id", tank.id},
            {"x", tank.x},
            {"y", tank.y},
            {"vx", tank.vx},
            {"vy", tank.vy},
            {"cooldown", tank.cooldown},
            {"targetX", tank.targetX},
            {"targetY", tank.targetY}
        });
    }
    json["tanks"] = tanksJson;

    return std::make_unique<JsonDataNode>("state", json);
}

void TankModule::setState(const IDataNode& state) {
    // Cast to JsonDataNode to access underlying JSON
    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&state);
    if (!jsonNode) {
        logger->error("setState: Invalid state (not JsonDataNode)");
        return;
    }

    const auto& json = jsonNode->getJsonData();

    // Ensure logger is initialized (needed after hot-reload)
    if (!logger) {
        logger = spdlog::get("TankModule");
        if (!logger) {
            logger = spdlog::stdout_color_mt("TankModule");
        }
    }

    // Ensure config is initialized (needed after hot-reload)
    if (!config) {
        config = std::make_unique<JsonDataNode>("config");
    }

    // Restaurer state
    // NOTE: Ne pas restaurer moduleVersion depuis le state!
    // La version vient du CODE, pas du state. C'est voulu pour le hot-reload.
    // moduleVersion est déjà initialisé à "v1.0" ou "v2.0 HOT-RELOADED" par le code
    frameCount = json.value("frameCount", 0);

    // Restaurer tanks
    tanks.clear();
    if (json.contains("tanks") && json["tanks"].is_array()) {
        for (const auto& tankJson : json["tanks"]) {
            Tank tank;
            tank.id = tankJson.value("id", 0);
            tank.x = tankJson.value("x", 0.0f);
            tank.y = tankJson.value("y", 0.0f);
            tank.vx = tankJson.value("vx", 0.0f);
            tank.vy = tankJson.value("vy", 0.0f);
            tank.cooldown = tankJson.value("cooldown", 0.0f);
            tank.targetX = tankJson.value("targetX", 0.0f);
            tank.targetY = tankJson.value("targetY", 0.0f);
            tanks.push_back(tank);
        }
    }

    logger->info("State restored: {} tanks, frame {}", tanks.size(), frameCount);
}

void TankModule::updateTank(Tank& tank, float dt) {
    // Update position
    tank.x += tank.vx * dt;
    tank.y += tank.vy * dt;

    // Bounce sur les bords (map 100x100)
    if (tank.x < 0.0f || tank.x > 100.0f) {
        tank.vx = -tank.vx;
        tank.x = std::clamp(tank.x, 0.0f, 100.0f);
    }
    if (tank.y < 0.0f || tank.y > 100.0f) {
        tank.vy = -tank.vy;
        tank.y = std::clamp(tank.y, 0.0f, 100.0f);
    }

    // Update cooldown
    if (tank.cooldown > 0.0f) {
        tank.cooldown -= dt;
    }

    // Déplacer vers target
    float dx = tank.targetX - tank.x;
    float dy = tank.targetY - tank.y;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist > 0.1f) {
        // Normaliser et appliquer velocity
        float speed = std::sqrt(tank.vx * tank.vx + tank.vy * tank.vy);
        tank.vx = (dx / dist) * speed;
        tank.vy = (dy / dist) * speed;
    }
}

void TankModule::spawnTanks(int count) {
    std::mt19937 rng(42); // Seed fixe pour reproductibilité
    std::uniform_real_distribution<float> posDist(0.0f, 100.0f);
    std::uniform_real_distribution<float> velDist(-5.0f, 5.0f);
    std::uniform_real_distribution<float> cooldownDist(0.0f, 5.0f);

    for (int i = 0; i < count; i++) {
        Tank tank;
        tank.id = i;
        tank.x = posDist(rng);
        tank.y = posDist(rng);
        tank.vx = velDist(rng);
        tank.vy = velDist(rng);
        tank.cooldown = cooldownDist(rng);
        tank.targetX = posDist(rng);
        tank.targetY = posDist(rng);
        tanks.push_back(tank);
    }

    logger->debug("Spawned {} tanks", count);
}

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule() {
        return new grove::TankModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
