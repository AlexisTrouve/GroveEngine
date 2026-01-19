#include "PlayerModule.h"
#include <iostream>

namespace grove {

PlayerModule::PlayerModule() {
    std::cout << "[PlayerModule] Constructor" << std::endl;
}

PlayerModule::~PlayerModule() {
    std::cout << "[PlayerModule] Destructor" << std::endl;
}

void PlayerModule::process(const IDataNode& input) {
    // Pull and dispatch all pending messages (callbacks invoked automatically)
    if (io) {
        while (io->hasMessages() > 0) {
            io->pullAndDispatch();
        }
    }
}

void PlayerModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[PlayerModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    // Store config
    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());

    // Subscribe to config changes with callback
    if (io) {
        io->subscribe("config:gameplay:changed", [this](const Message& msg) {
            handleConfigChange();
        });
    }
}

const IDataNode& PlayerModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> PlayerModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"gold", gold},
        {"level", level},
        {"playerName", playerName}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void PlayerModule::shutdown() {
    std::cout << "[PlayerModule] Shutdown - Level: " << level << ", Gold: " << gold << std::endl;
}

std::unique_ptr<IDataNode> PlayerModule::getState() {
    nlohmann::json inventoryJson = nlohmann::json::array();
    for (const auto& item : inventory) {
        inventoryJson.push_back(item);
    }

    nlohmann::json state = {
        {"gold", gold},
        {"level", level},
        {"playerName", playerName},
        {"inventory", inventoryJson}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void PlayerModule::setState(const IDataNode& state) {
    gold = state.getInt("gold", 1000);
    level = state.getInt("level", 1);
    playerName = state.getString("playerName", "Player1");

    // Restore inventory
    inventory.clear();
    auto stateData = state.getData();
    if (stateData && stateData->has("inventory")) {
        auto invData = stateData->get("inventory");
        if (invData && invData->isArray()) {
            size_t size = invData->size();
            for (size_t i = 0; i < size; i++) {
                auto item = invData->get(i);
                if (item && item->isString()) {
                    inventory.push_back(item->asString());
                }
            }
        }
    }

    std::cout << "[PlayerModule] State restored - Level: " << level << ", Gold: " << gold << std::endl;
}

void PlayerModule::setDataTree(IDataTree* treePtr) {
    this->tree = treePtr;
}

void PlayerModule::handleConfigChange() {
    std::cout << "[PlayerModule] Handling config change" << std::endl;

    if (!tree) return;

    // Read new config
    auto configRoot = tree->getConfigRoot();
    auto gameplay = configRoot->getChild("gameplay");

    if (gameplay) {
        double hpMultiplier = gameplay->getDouble("hpMultiplier", 1.0);
        std::string difficulty = gameplay->getString("difficulty", "normal");

        std::cout << "[PlayerModule] Config updated - Difficulty: " << difficulty
                  << ", HP Mult: " << hpMultiplier << std::endl;
    }
}

void PlayerModule::savePlayerData() {
    if (!tree) return;

    auto dataRoot = tree->getDataRoot();

    nlohmann::json profileData = {
        {"name", playerName},
        {"level", level},
        {"gold", gold}
    };

    auto profile = std::make_unique<JsonDataNode>("profile", profileData);

    // This would save to data/player/profile
    std::cout << "[PlayerModule] Saving player data" << std::endl;
}

void PlayerModule::publishLevelUp() {
    if (!io) return;

    nlohmann::json data = {
        {"event", "level_up"},
        {"newLevel", level},
        {"goldBonus", 500}
    };

    auto dataNode = std::make_unique<JsonDataNode>("levelUp", data);
    io->publish("player:level_up", std::move(dataNode));

    std::cout << "[PlayerModule] Published level up event" << std::endl;
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::PlayerModule();
    }
}
