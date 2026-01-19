#include "IOStressModule.h"
#include <grove/JsonDataNode.h>
#include <iostream>

namespace grove {

IOStressModule::IOStressModule() {
    std::cout << "[IOStressModule] Constructor" << std::endl;
}

IOStressModule::~IOStressModule() {
    std::cout << "[IOStressModule] Destructor" << std::endl;
}

void IOStressModule::process(const IDataNode& input) {
    if (!io) return;

    // Pull and dispatch all available messages (high-frequency consumer)
    while (io->hasMessages() > 0) {
        try {
            io->pullAndDispatch();
        } catch (const std::exception& e) {
            std::cerr << "[IOStressModule] Error pulling message: " << e.what() << std::endl;
        }
    }
}

void IOStressModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[IOStressModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());

    // Subscribe to all messages with callback that counts them
    if (io) {
        io->subscribe("*", [this](const Message& msg) {
            receivedCount++;
        });
    }
}

const IDataNode& IOStressModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> IOStressModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"receivedCount", receivedCount}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void IOStressModule::shutdown() {
    std::cout << "[IOStressModule] Shutdown - Received " << receivedCount << " messages" << std::endl;
}

std::unique_ptr<IDataNode> IOStressModule::getState() {
    nlohmann::json state = {
        {"receivedCount", receivedCount}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void IOStressModule::setState(const IDataNode& state) {
    receivedCount = state.getInt("receivedCount", 0);
    std::cout << "[IOStressModule] State restored - Count: " << receivedCount << std::endl;
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::IOStressModule();
    }
}
