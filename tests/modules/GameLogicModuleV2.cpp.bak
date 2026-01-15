#include "GameLogicModuleV1.h"
#include "grove/JsonDataNode.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <cmath>

using json = nlohmann::json;

namespace grove {

GameLogicModuleV1::GameLogicModuleV1() {
    config_ = std::make_unique<JsonDataNode>("config", json::object());
}

void GameLogicModuleV1::process(const IDataNode& input) {
    if (!initialized_) return;

    processCount_++;

    // Extract deltaTime from input
    float deltaTime = 0.016f; // Default 60 FPS
    const auto* jsonInput = dynamic_cast<const JsonDataNode*>(&input);
    if (jsonInput) {
        const auto& jsonData = jsonInput->getJsonData();
        if (jsonData.contains("deltaTime")) {
            deltaTime = jsonData["deltaTime"];
        }
    }

    // Update all entities (simple movement)
    updateEntities(deltaTime);
}

void GameLogicModuleV1::updateEntities(float dt) {
    for (auto& e : entities_) {
        // V1: Simple movement only
        e.x += e.vx * dt;
        e.y += e.vy * dt;

        // Simple wrapping (keep entities in bounds)
        if (e.x < 0.0f) e.x += 1000.0f;
        if (e.x > 1000.0f) e.x -= 1000.0f;
        if (e.y < 0.0f) e.y += 1000.0f;
        if (e.y > 1000.0f) e.y -= 1000.0f;
    }
}

void GameLogicModuleV1::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    io_ = io;
    scheduler_ = scheduler;

    // Store configuration
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        config_ = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    } else {
        config_ = std::make_unique<JsonDataNode>("config", json::object());
    }

    // Initialize entities from config
    int entityCount = 100; // Default
    const auto* jsonConfig = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfig) {
        const auto& jsonData = jsonConfig->getJsonData();
        if (jsonData.contains("entityCount")) {
            entityCount = jsonData["entityCount"];
        }
    }

    entities_.clear();
    entities_.reserve(entityCount);

    // Create entities with deterministic positions/velocities
    for (int i = 0; i < entityCount; ++i) {
        float x = std::fmod(i * 123.456f, 1000.0f);
        float y = std::fmod(i * 789.012f, 1000.0f);
        float vx = ((i % 10) - 5) * 10.0f; // -50 to 50
        float vy = ((i % 7) - 3) * 10.0f;  // -30 to 40

        entities_.emplace_back(i, x, y, vx, vy);
    }

    initialized_ = true;
}

const IDataNode& GameLogicModuleV1::getConfiguration() {
    return *config_;
}

std::unique_ptr<IDataNode> GameLogicModuleV1::getHealthStatus() {
    json health;
    health["status"] = "healthy";
    health["version"] = getVersion();
    health["entityCount"] = static_cast<int>(entities_.size());
    health["processCount"] = processCount_.load();
    return std::make_unique<JsonDataNode>("health", health);
}

void GameLogicModuleV1::shutdown() {
    initialized_ = false;
    entities_.clear();
}

std::unique_ptr<IDataNode> GameLogicModuleV1::getState() {
    json state;
    state["version"] = getVersion();
    state["processCount"] = processCount_.load();
    state["entityCount"] = static_cast<int>(entities_.size());

    // Serialize entities
    json entitiesJson = json::object();
    for (size_t i = 0; i < entities_.size(); ++i) {
        const auto& e = entities_[i];
        json entityJson;
        entityJson["id"] = e.id;
        entityJson["x"] = e.x;
        entityJson["y"] = e.y;
        entityJson["vx"] = e.vx;
        entityJson["vy"] = e.vy;

        std::string key = "entity_" + std::to_string(i);
        entitiesJson[key] = entityJson;
    }
    state["entities"] = entitiesJson;

    return std::make_unique<JsonDataNode>("state", state);
}

void GameLogicModuleV1::setState(const IDataNode& state) {
    const auto* jsonState = dynamic_cast<const JsonDataNode*>(&state);
    if (!jsonState) return;

    const auto& jsonData = jsonState->getJsonData();
    if (!jsonData.contains("version")) return;

    processCount_ = jsonData["processCount"];
    int entityCount = jsonData["entityCount"];

    // Deserialize entities
    entities_.clear();
    entities_.reserve(entityCount);

    if (jsonData.contains("entities")) {
        const auto& entitiesJson = jsonData["entities"];
        for (int i = 0; i < entityCount; ++i) {
            std::string key = "entity_" + std::to_string(i);
            if (entitiesJson.contains(key)) {
                const auto& entityJson = entitiesJson[key];
                Entity e;
                e.id = entityJson["id"];
                e.x = entityJson["x"];
                e.y = entityJson["y"];
                e.vx = entityJson["vx"];
                e.vy = entityJson["vy"];
                entities_.push_back(e);
            }
        }
    }

    initialized_ = true;
}

} // namespace grove

// C API implementation
extern "C" {
    grove::IModule* createModule() {
        return new grove::GameLogicModuleV1();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
