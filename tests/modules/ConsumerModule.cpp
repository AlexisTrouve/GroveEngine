#include "ConsumerModule.h"
#include <grove/JsonDataNode.h>
#include <iostream>

namespace grove {

ConsumerModule::ConsumerModule() {
    std::cout << "[ConsumerModule] Constructor" << std::endl;
}

ConsumerModule::~ConsumerModule() {
    std::cout << "[ConsumerModule] Destructor" << std::endl;
}

void ConsumerModule::process(const IDataNode& input) {
    if (!io) return;

    // Pull all available messages
    while (io->hasMessages() > 0) {
        try {
            auto msg = io->pullMessage();
            receivedCount++;

            // Optionally log message details
            bool verbose = input.getBool("verbose", false);
            if (verbose) {
                std::cout << "[ConsumerModule] Received message #" << receivedCount
                          << " on topic: " << msg.topic << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[ConsumerModule] Error pulling message: " << e.what() << std::endl;
        }
    }
}

void ConsumerModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[ConsumerModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    // Store config
    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
}

const IDataNode& ConsumerModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> ConsumerModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"receivedCount", receivedCount}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void ConsumerModule::shutdown() {
    std::cout << "[ConsumerModule] Shutdown - Received " << receivedCount << " messages" << std::endl;
}

std::unique_ptr<IDataNode> ConsumerModule::getState() {
    nlohmann::json state = {
        {"receivedCount", receivedCount}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void ConsumerModule::setState(const IDataNode& state) {
    receivedCount = state.getInt("receivedCount", 0);
    std::cout << "[ConsumerModule] State restored - Count: " << receivedCount << std::endl;
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::ConsumerModule();
    }
}
