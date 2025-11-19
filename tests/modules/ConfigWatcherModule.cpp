#include "ConfigWatcherModule.h"
#include <iostream>

namespace grove {

ConfigWatcherModule::ConfigWatcherModule() {
    std::cout << "[ConfigWatcherModule] Constructor" << std::endl;
}

ConfigWatcherModule::~ConfigWatcherModule() {
    std::cout << "[ConfigWatcherModule] Destructor" << std::endl;
}

void ConfigWatcherModule::process(const IDataNode& input) {
    // Check for config changes if tree is available
    if (tree && tree->checkForChanges()) {
        configChangesDetected++;
        onConfigReloaded();
    }
}

void ConfigWatcherModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[ConfigWatcherModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    // Store config
    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
}

const IDataNode& ConfigWatcherModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> ConfigWatcherModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"configChangesDetected", configChangesDetected}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void ConfigWatcherModule::shutdown() {
    std::cout << "[ConfigWatcherModule] Shutdown - Detected " << configChangesDetected << " config changes" << std::endl;
}

std::unique_ptr<IDataNode> ConfigWatcherModule::getState() {
    nlohmann::json state = {
        {"configChangesDetected", configChangesDetected}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void ConfigWatcherModule::setState(const IDataNode& state) {
    configChangesDetected = state.getInt("configChangesDetected", 0);
    std::cout << "[ConfigWatcherModule] State restored" << std::endl;
}

void ConfigWatcherModule::setDataTree(IDataTree* treePtr) {
    this->tree = treePtr;
}

void ConfigWatcherModule::onConfigReloaded() {
    std::cout << "[ConfigWatcherModule] Config reloaded, publishing event" << std::endl;
    publishConfigChange("gameplay");
}

void ConfigWatcherModule::publishConfigChange(const std::string& configName) {
    if (!io) return;

    nlohmann::json data = {
        {"config", configName},
        {"timestamp", configChangesDetected}
    };

    auto dataNode = std::make_unique<JsonDataNode>("configChange", data);
    io->publish("config:" + configName + ":changed", std::move(dataNode));
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::ConfigWatcherModule();
    }
}
