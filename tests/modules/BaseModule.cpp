#include "BaseModule.h"
#include "grove/JsonDataNode.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace grove {

void BaseModule::process(const IDataNode& input) {
    processCount_++;
    // Simple processing - just increment counter
}

void BaseModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    logger_ = spdlog::get("grove");
    if (!logger_) {
        logger_ = spdlog::default_logger();
    }

    // Store configuration
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        configNode_ = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());

        // Parse configuration to determine version
        const auto& jsonData = jsonConfigNode->getJsonData();
        if (jsonData.contains("version")) {
            version_ = jsonData["version"];
        }
        if (jsonData.contains("generatedValue")) {
            generatedValue_ = jsonData["generatedValue"];
        }
    } else {
        configNode_ = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }

    logger_->info("BaseModule v{} initialized: generateNumber() will return {}", version_, generatedValue_);
}

const IDataNode& BaseModule::getConfiguration() {
    if (!configNode_) {
        configNode_ = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *configNode_;
}

std::unique_ptr<IDataNode> BaseModule::getHealthStatus() {
    json status;
    status["status"] = "healthy";
    status["processCount"] = processCount_.load();
    status["version"] = version_;
    status["generatedValue"] = generatedValue_;
    return std::make_unique<JsonDataNode>("health", status);
}

void BaseModule::shutdown() {
    if (logger_) {
        logger_->info("BaseModule v{} shutting down", version_);
    }
}

std::unique_ptr<IDataNode> BaseModule::getState() {
    json state;
    state["processCount"] = processCount_.load();
    state["version"] = version_;
    state["generatedValue"] = generatedValue_;
    return std::make_unique<JsonDataNode>("state", state);
}

void BaseModule::setState(const IDataNode& state) {
    const auto* jsonStateNode = dynamic_cast<const JsonDataNode*>(&state);
    if (jsonStateNode) {
        const auto& jsonData = jsonStateNode->getJsonData();
        if (jsonData.contains("processCount")) {
            processCount_ = jsonData["processCount"];
        }
        if (jsonData.contains("version")) {
            version_ = jsonData["version"];
        }
        if (jsonData.contains("generatedValue")) {
            generatedValue_ = jsonData["generatedValue"];
        }
        if (logger_) {
            logger_->info("BaseModule state restored: v{}, processCount={}", version_, processCount_.load());
        }
    } else {
        if (logger_) {
            logger_->error("BaseModule: Failed to restore state: not a JsonDataNode");
        }
    }
}

int BaseModule::generateNumber() const {
    return generatedValue_;
}

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule() {
        return new grove::BaseModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
