#include "BroadcastModule.h"
#include <grove/JsonDataNode.h>
#include <iostream>

namespace grove {

BroadcastModule::BroadcastModule() {
    std::cout << "[BroadcastModule] Constructor" << std::endl;
}

BroadcastModule::~BroadcastModule() {
    std::cout << "[BroadcastModule] Destructor" << std::endl;
}

void BroadcastModule::process(const IDataNode& input) {
    if (!io) return;

    // Pull all available messages
    while (io->hasMessages() > 0) {
        try {
            auto msg = io->pullMessage();
            receivedCount++;

            bool verbose = input.getBool("verbose", false);
            if (verbose) {
                std::cout << "[BroadcastModule] Received message #" << receivedCount
                          << " on topic: " << msg.topic << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[BroadcastModule] Error pulling message: " << e.what() << std::endl;
        }
    }
}

void BroadcastModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[BroadcastModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
}

const IDataNode& BroadcastModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> BroadcastModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"receivedCount", receivedCount}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void BroadcastModule::shutdown() {
    std::cout << "[BroadcastModule] Shutdown - Received " << receivedCount << " messages" << std::endl;
}

std::unique_ptr<IDataNode> BroadcastModule::getState() {
    nlohmann::json state = {
        {"receivedCount", receivedCount}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void BroadcastModule::setState(const IDataNode& state) {
    receivedCount = state.getInt("receivedCount", 0);
    std::cout << "[BroadcastModule] State restored - Count: " << receivedCount << std::endl;
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::BroadcastModule();
    }
}
