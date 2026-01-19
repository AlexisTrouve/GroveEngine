#include "BatchModule.h"
#include <grove/JsonDataNode.h>
#include <iostream>

namespace grove {

BatchModule::BatchModule() {
    std::cout << "[BatchModule] Constructor" << std::endl;
}

BatchModule::~BatchModule() {
    std::cout << "[BatchModule] Destructor" << std::endl;
}

void BatchModule::process(const IDataNode& input) {
    if (!io) return;

    // Pull and dispatch batched messages (callbacks invoked automatically)
    while (io->hasMessages() > 0) {
        try {
            io->pullAndDispatch();
        } catch (const std::exception& e) {
            std::cerr << "[BatchModule] Error pulling message: " << e.what() << std::endl;
        }
    }
}

void BatchModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[BatchModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());

    // Subscribe to all messages with callback that counts batches
    if (io) {
        io->subscribe("*", [this](const Message& msg) {
            batchCount++;
        });
    }
}

const IDataNode& BatchModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> BatchModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"batchCount", batchCount}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void BatchModule::shutdown() {
    std::cout << "[BatchModule] Shutdown - Received " << batchCount << " batches" << std::endl;
}

std::unique_ptr<IDataNode> BatchModule::getState() {
    nlohmann::json state = {
        {"batchCount", batchCount}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void BatchModule::setState(const IDataNode& state) {
    batchCount = state.getInt("batchCount", 0);
    std::cout << "[BatchModule] State restored - Batch count: " << batchCount << std::endl;
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::BatchModule();
    }
}
