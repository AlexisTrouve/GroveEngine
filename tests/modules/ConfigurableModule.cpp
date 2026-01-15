#include "ConfigurableModule.h"
#include "grove/JsonDataNode.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cmath>

namespace grove {

void ConfigurableModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    // Logger
    logger = spdlog::get("ConfigurableModule");
    if (!logger) {
        logger = spdlog::stdout_color_mt("ConfigurableModule");
    }
    logger->set_level(spdlog::level::debug);

    // Clone config en JSON
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        this->configNode = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    } else {
        this->configNode = std::make_unique<JsonDataNode>("config");
    }

    // Parse et appliquer config
    currentConfig = parseConfig(configNode);
    previousConfig = currentConfig;  // Backup initial

    std::string errorMsg;
    if (!currentConfig.validate(errorMsg)) {
        logger->error("Invalid initial configuration: {}", errorMsg);
        // Fallback to defaults
        currentConfig = Config();
    }

    logger->info("Initializing ConfigurableModule");
    logger->info("  Spawn rate: {}/s", currentConfig.spawnRate);
    logger->info("  Max entities: {}", currentConfig.maxEntities);
    logger->info("  Entity speed: {}", currentConfig.entitySpeed);
    logger->info("  Colors: {}", currentConfig.colors.size());

    frameCount = 0;
}

const IDataNode& ConfigurableModule::getConfiguration() {
    return *configNode;
}

void ConfigurableModule::process(const IDataNode& input) {
    float deltaTime = static_cast<float>(input.getDouble("deltaTime", 1.0 / 60.0));

    frameCount++;

    // Spawning logic
    if (static_cast<int>(entities.size()) < currentConfig.maxEntities) {
        spawnAccumulator += deltaTime * currentConfig.spawnRate;

        while (spawnAccumulator >= 1.0f && static_cast<int>(entities.size()) < currentConfig.maxEntities) {
            spawnEntity();
            spawnAccumulator -= 1.0f;
        }
    }

    // Update entities
    for (auto& entity : entities) {
        updateEntity(entity, deltaTime);
    }

    // Log toutes les 60 frames (1 seconde)
    if (frameCount % 60 == 0) {
        logger->trace("Frame {}: {} entities active (max: {})", frameCount, entities.size(), currentConfig.maxEntities);
    }
}

std::unique_ptr<IDataNode> ConfigurableModule::getHealthStatus() {
    nlohmann::json healthJson;
    healthJson["status"] = "healthy";
    healthJson["entityCount"] = entities.size();
    healthJson["frameCount"] = frameCount;
    healthJson["maxEntities"] = currentConfig.maxEntities;
    auto health = std::make_unique<JsonDataNode>("health", healthJson);
    return health;
}

void ConfigurableModule::shutdown() {
    logger->info("Shutting down ConfigurableModule");
    entities.clear();
}

std::string ConfigurableModule::getType() const {
    return "configurable";
}

std::unique_ptr<IDataNode> ConfigurableModule::getState() {
    nlohmann::json json;

    json["frameCount"] = frameCount;
    json["nextEntityId"] = nextEntityId;
    json["spawnAccumulator"] = spawnAccumulator;

    // Sérialiser config actuelle
    json["config"] = configToJson(currentConfig);

    // Sérialiser entities
    nlohmann::json entitiesJson = nlohmann::json::array();
    for (const auto& entity : entities) {
        entitiesJson.push_back({
            {"id", entity.id},
            {"x", entity.x},
            {"y", entity.y},
            {"vx", entity.vx},
            {"vy", entity.vy},
            {"color", entity.color},
            {"speed", entity.speed}
        });
    }
    json["entities"] = entitiesJson;

    return std::make_unique<JsonDataNode>("state", json);
}

void ConfigurableModule::setState(const IDataNode& state) {
    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&state);
    if (!jsonNode) {
        logger->error("setState: Invalid state (not JsonDataNode)");
        return;
    }

    const auto& json = jsonNode->getJsonData();

    // Ensure logger is initialized
    if (!logger) {
        logger = spdlog::get("ConfigurableModule");
        if (!logger) {
            logger = spdlog::stdout_color_mt("ConfigurableModule");
        }
    }

    // Ensure configNode is initialized
    if (!configNode) {
        configNode = std::make_unique<JsonDataNode>("config");
    }

    // Restaurer state
    frameCount = json.value("frameCount", 0);
    nextEntityId = json.value("nextEntityId", 0);
    spawnAccumulator = json.value("spawnAccumulator", 0.0f);

    // Restaurer entities
    entities.clear();
    if (json.contains("entities") && json["entities"].is_array()) {
        for (const auto& entityJson : json["entities"]) {
            Entity entity;
            entity.id = entityJson.value("id", 0);
            entity.x = entityJson.value("x", 0.0f);
            entity.y = entityJson.value("y", 0.0f);
            entity.vx = entityJson.value("vx", 0.0f);
            entity.vy = entityJson.value("vy", 0.0f);
            entity.color = entityJson.value("color", "red");
            entity.speed = entityJson.value("speed", 5.0f);
            entities.push_back(entity);
        }
    }

    logger->info("State restored: {} entities, frame {}", entities.size(), frameCount);
}

bool ConfigurableModule::updateConfig(const IDataNode& newConfigNode) {
    logger->info("Attempting config hot-reload...");

    // Parse nouvelle config
    Config newConfig = parseConfig(newConfigNode);

    // Valider
    std::string errorMsg;
    if (!newConfig.validate(errorMsg)) {
        logger->error("Config validation failed: {}", errorMsg);
        return false;
    }

    // Backup current config (pour rollback potentiel)
    previousConfig = currentConfig;

    // Appliquer nouvelle config
    currentConfig = newConfig;

    // Update stored config node
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&newConfigNode);
    if (jsonConfigNode) {
        configNode = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    }

    logger->info("Config hot-reload successful!");
    logger->info("  New spawn rate: {}/s", currentConfig.spawnRate);
    logger->info("  New max entities: {}", currentConfig.maxEntities);
    logger->info("  New entity speed: {}", currentConfig.entitySpeed);

    return true;
}

bool ConfigurableModule::updateConfigPartial(const IDataNode& partialConfigNode) {
    logger->info("Attempting partial config update...");

    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&partialConfigNode);
    if (!jsonNode) {
        logger->error("Partial config update failed: not a JsonDataNode");
        return false;
    }

    const auto& partialJson = jsonNode->getJsonData();

    // Créer une nouvelle config en fusionnant avec l'actuelle
    Config mergedConfig = currentConfig;

    // Merge chaque champ présent dans partialJson
    if (partialJson.contains("spawnRate")) {
        mergedConfig.spawnRate = partialJson["spawnRate"];
    }
    if (partialJson.contains("maxEntities")) {
        mergedConfig.maxEntities = partialJson["maxEntities"];
    }
    if (partialJson.contains("entitySpeed")) {
        mergedConfig.entitySpeed = partialJson["entitySpeed"];
    }
    if (partialJson.contains("colors")) {
        mergedConfig.colors.clear();
        for (const auto& color : partialJson["colors"]) {
            mergedConfig.colors.push_back(color);
        }
    }
    if (partialJson.contains("physics")) {
        if (partialJson["physics"].contains("gravity")) {
            mergedConfig.physics.gravity = partialJson["physics"]["gravity"];
        }
        if (partialJson["physics"].contains("friction")) {
            mergedConfig.physics.friction = partialJson["physics"]["friction"];
        }
    }

    // Valider config fusionnée
    std::string errorMsg;
    if (!mergedConfig.validate(errorMsg)) {
        logger->error("Partial config validation failed: {}", errorMsg);
        return false;
    }

    // Appliquer
    previousConfig = currentConfig;
    currentConfig = mergedConfig;

    // Update stored config node with merged config
    nlohmann::json mergedJson = configToJson(currentConfig);
    configNode = std::make_unique<JsonDataNode>("config", mergedJson);

    logger->info("Partial config update successful!");

    return true;
}

ConfigurableModule::Config ConfigurableModule::parseConfig(const IDataNode& configNode) {
    Config cfg;

    // Cast to JsonDataNode to access JSON
    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (!jsonNode) {
        logger->warn("Config not a JsonDataNode, using defaults");
        return cfg;
    }

    const auto& json = jsonNode->getJsonData();

    cfg.spawnRate = json.value("spawnRate", 10);
    cfg.maxEntities = json.value("maxEntities", 50);
    cfg.entitySpeed = json.value("entitySpeed", 5.0f);

    // Parse colors
    cfg.colors.clear();
    if (json.contains("colors") && json["colors"].is_array()) {
        for (const auto& colorJson : json["colors"]) {
            cfg.colors.push_back(colorJson.get<std::string>());
        }
    }
    if (cfg.colors.empty()) {
        cfg.colors = {"red", "blue"};  // Default
    }

    // Parse physics
    if (json.contains("physics") && json["physics"].is_object()) {
        cfg.physics.gravity = json["physics"].value("gravity", 9.8f);
        cfg.physics.friction = json["physics"].value("friction", 0.5f);
    }

    return cfg;
}

nlohmann::json ConfigurableModule::configToJson(const Config& cfg) const {
    nlohmann::json json;
    json["spawnRate"] = cfg.spawnRate;
    json["maxEntities"] = cfg.maxEntities;
    json["entitySpeed"] = cfg.entitySpeed;
    json["colors"] = cfg.colors;
    json["physics"]["gravity"] = cfg.physics.gravity;
    json["physics"]["friction"] = cfg.physics.friction;
    return json;
}

void ConfigurableModule::spawnEntity() {
    Entity entity;
    entity.id = nextEntityId++;

    // Position aléatoire
    std::uniform_real_distribution<float> posDist(0.0f, 100.0f);
    entity.x = posDist(rng);
    entity.y = posDist(rng);

    // Vélocité aléatoire basée sur speed
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
    float angle = angleDist(rng);
    entity.vx = std::cos(angle) * currentConfig.entitySpeed;
    entity.vy = std::sin(angle) * currentConfig.entitySpeed;

    // Couleur aléatoire depuis la palette actuelle
    std::uniform_int_distribution<size_t> colorDist(0, currentConfig.colors.size() - 1);
    entity.color = currentConfig.colors[colorDist(rng)];

    // Snapshot de la config speed au moment de la création
    entity.speed = currentConfig.entitySpeed;

    entities.push_back(entity);

    logger->trace("Spawned entity #{} at ({:.1f}, {:.1f}) color={} speed={}",
                  entity.id, entity.x, entity.y, entity.color, entity.speed);
}

void ConfigurableModule::updateEntity(Entity& entity, float dt) {
    // Update position
    entity.x += entity.vx * dt;
    entity.y += entity.vy * dt;

    // Bounce sur les bords (map 100x100)
    if (entity.x < 0.0f || entity.x > 100.0f) {
        entity.vx = -entity.vx;
        entity.x = std::clamp(entity.x, 0.0f, 100.0f);
    }
    if (entity.y < 0.0f || entity.y > 100.0f) {
        entity.vy = -entity.vy;
        entity.y = std::clamp(entity.y, 0.0f, 100.0f);
    }

    // Appliquer gravité et friction de la config actuelle
    entity.vy += currentConfig.physics.gravity * dt;
    entity.vx *= (1.0f - currentConfig.physics.friction * dt);
    entity.vy *= (1.0f - currentConfig.physics.friction * dt);
}

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule() {
        return new grove::ConfigurableModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
