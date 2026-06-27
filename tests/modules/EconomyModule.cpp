#include "EconomyModule.h"
#include <iostream>

namespace grove {

EconomyModule::EconomyModule() {
    std::cout << "[EconomyModule] Constructor" << std::endl;
}

EconomyModule::~EconomyModule() {
    std::cout << "[EconomyModule] Destructor" << std::endl;
}

void EconomyModule::process(const IDataNode& input) {
    // Pull and dispatch all pending messages (callbacks invoked automatically)
    if (io) {
        while (io->hasMessages() > 0) {
            io->pullAndDispatch();
        }
    }
}

void EconomyModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[EconomyModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    // Store config
    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());

    // Subscribe to player events with callback
    if (io) {
        io->subscribe("player:*", [this](const Message& msg) {
            playerEventsProcessed++;
            handlePlayerEvent(msg.topic, msg.data.get());
        });
    }
}

const IDataNode& EconomyModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> EconomyModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"totalBonusesApplied", totalBonusesApplied},
        {"playerEventsProcessed", playerEventsProcessed}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void EconomyModule::shutdown() {
    std::cout << "[EconomyModule] Shutdown - Processed " << playerEventsProcessed << " player events" << std::endl;
}

std::unique_ptr<IDataNode> EconomyModule::getState() {
    nlohmann::json state = {
        {"totalBonusesApplied", totalBonusesApplied},
        {"playerEventsProcessed", playerEventsProcessed}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void EconomyModule::setState(const IDataNode& state) {
    totalBonusesApplied = state.getInt("totalBonusesApplied", 0);
    playerEventsProcessed = state.getInt("playerEventsProcessed", 0);
    std::cout << "[EconomyModule] State restored" << std::endl;
}

void EconomyModule::setDataTree(IDataTree* treePtr) {
    this->tree = treePtr;
}

void EconomyModule::handlePlayerEvent(const std::string& topic, const IDataNode* data) {
    std::cout << "[EconomyModule] Handling player event: " << topic << std::endl;

    if (topic == "player:level_up") {
        // Apply economy bonus
        if (data) {
            int goldBonus = data->getInt("goldBonus", 0);
            applyEconomyBonus(goldBonus);
        }
    } else if (topic == "player:gold:updated") {
        // Verify synchronization
        if (data && tree) {
            auto dataRoot = tree->getDataRoot();
            auto player = dataRoot->getChild("player");
            if (player) {
                auto profile = player->getChild("profile");
                if (profile) {
                    int goldInData = profile->getInt("gold", 0);
                    int goldInMsg = data->getInt("gold", 0);

                    if (goldInData == goldInMsg) {
                        std::cout << "[EconomyModule] Sync OK: gold=" << goldInData << std::endl;
                    } else {
                        std::cout << "[EconomyModule] SYNC ERROR: msg=" << goldInMsg
                                  << " data=" << goldInData << std::endl;
                    }
                }
            }
        }
    }
}

void EconomyModule::applyEconomyBonus(int goldBonus) {
    totalBonusesApplied += goldBonus;

    if (!tree) return;

    auto dataRoot = tree->getDataRoot();

    nlohmann::json bonusData = {
        {"levelUpBonus", goldBonus},
        {"totalBonuses", totalBonusesApplied}
    };

    auto bonuses = std::make_unique<JsonDataNode>("bonuses", bonusData);

    std::cout << "[EconomyModule] Applied bonus: " << goldBonus << std::endl;
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::EconomyModule();
    }
}
