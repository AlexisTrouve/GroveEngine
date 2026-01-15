#include "GameLogicModuleV2.h"
#include "grove/JsonDataNode.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <cmath>

using json = nlohmann::json;

namespace grove {

GameLogicModuleV2::GameLogicModuleV2() {
    config_ = std::make_unique<JsonDataNode>("config", json::object());
}

void GameLogicModuleV2::process(const IDataNode& input) {
    if (!initialized_) return;

    processCount_++;

    // Extract deltaTime from input
    float deltaTime = 0.016f;
    const auto* jsonInput = dynamic_cast<const JsonDataNode*>(&input);
    if (jsonInput) {
        const auto& jsonData = jsonInput->getJsonData();
        if (jsonData.contains("deltaTime")) {
            deltaTime = jsonData["deltaTime"];
        }
    }

    updateEntities(deltaTime);
}

void GameLogicModuleV2::updateEntities(float dt) {
    for (auto& e : entities_) {
        // V2: Movement
        e.x += e.vx * dt;
        e.y += e.vy * dt;

        // V2: NEW - Collision detection
        checkCollisions(e);

        // Wrapping
        if (e.x < 0.0f) e.x += 1000.0f;
        if (e.x > 1000.0f) e.x -= 1000.0f;
        if (e.y < 0.0f) e.y += 1000.0f;
        if (e.y > 1000.0f) e.y -= 1000.0f;
    }
}

void GameLogicModuleV2::checkCollisions(EntityV2& e) {
    bool wasCollided = e.collided;
    e.collided = (e.x < 50.0f || e.x > 950.0f || e.y < 50.0f || e.y > 950.0f);
    if (e.collided && !wasCollided) {
        collisionCount_++;
    }
}

void GameLogicModuleV2::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    io_ = io;
    scheduler_ = scheduler;

    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        config_ = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    } else {
        config_ = std::make_unique<JsonDataNode>("config", json::object());
    }

    int entityCount = 100;
    const auto* jsonConfig = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfig) {
        const auto& jsonData = jsonConfig->getJsonData();
        if (jsonData.contains("entityCount")) {
            entityCount = jsonData["entityCount"];
        }
    }

    entities_.clear();
    entities_.reserve(entityCount);

    for (int i = 0; i < entityCount; ++i) {
        float x = std::fmod(i * 123.456f, 1000.0f);
        float y = std::fmod(i * 789.012f, 1000.0f);
        float vx = ((i % 10) - 5) * 10.0f;
        float vy = ((i % 7) - 3) * 10.0f;
        entities_.emplace_back(i, x, y, vx, vy);
    }

    initialized_ = true;
}

const IDataNode& GameLogicModuleV2::getConfiguration() {
    return *config_;
}

std::unique_ptr<IDataNode> GameLogicModuleV2::getHealthStatus() {
    json health;
    health["status"] = "healthy";
    health["version"] = getVersion();
    health["entityCount"] = static_cast<int>(entities_.size());
    health["processCount"] = processCount_.load();
    health["collisionCount"] = collisionCount_.load();
    return std::make_unique<JsonDataNode>("health", health);
}

void GameLogicModuleV2::shutdown() {
    initialized_ = false;
    entities_.clear();
}

std::unique_ptr<IDataNode> GameLogicModuleV2::getState() {
    json state;
    state["version"] = getVersion();
    state["processCount"] = processCount_.load();
    state["collisionCount"] = collisionCount_.load();
    state["entityCount"] = static_cast<int>(entities_.size());

    json entitiesJson = json::object();
    for (size_t i = 0; i < entities_.size(); ++i) {
        const auto& e = entities_[i];
        json entityJson;
        entityJson["id"] = e.id;
        entityJson["x"] = e.x;
        entityJson["y"] = e.y;
        entityJson["vx"] = e.vx;
        entityJson["vy"] = e.vy;
        entityJson["collided"] = e.collided;

        std::string key = "entity_" + std::to_string(i);
        entitiesJson[key] = entityJson;
    }
    state["entities"] = entitiesJson;

    return std::make_unique<JsonDataNode>("state", state);
}

void GameLogicModuleV2::setState(const IDataNode& state) {
    const auto* jsonState = dynamic_cast<const JsonDataNode*>(&state);
    if (!jsonState) return;

    const auto& jsonData = jsonState->getJsonData();
    if (!jsonData.contains("version")) return;

    processCount_ = jsonData["processCount"];
    if (jsonData.contains("collisionCount")) {
        collisionCount_ = jsonData["collisionCount"];
    }

    int entityCount = jsonData["entityCount"];

    entities_.clear();
    entities_.reserve(entityCount);

    if (jsonData.contains("entities")) {
        const auto& entitiesJson = jsonData["entities"];
        for (int i = 0; i < entityCount; ++i) {
            std::string key = "entity_" + std::to_string(i);
            if (entitiesJson.contains(key)) {
                const auto& entityJson = entitiesJson[key];
                EntityV2 e;
                e.id = entityJson["id"];
                e.x = entityJson["x"];
                e.y = entityJson["y"];
                e.vx = entityJson["vx"];
                e.vy = entityJson["vy"];
                e.collided = entityJson.contains("collided") ? entityJson["collided"].get<bool>() : false;
                entities_.push_back(e);
            }
        }
    }

    initialized_ = true;
}

bool GameLogicModuleV2::migrateStateFrom(int fromVersion, const IDataNode& oldState) {
    if (fromVersion == 1 || fromVersion == 2) {
        setState(oldState);
        // Re-check collisions for all entities
        for (auto& e : entities_) {
            checkCollisions(e);
        }
        return true;
    }
    return false;
}

} // namespace grove

extern "C" {
    grove::IModule* createModule() {
        return new grove::GameLogicModuleV2();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
