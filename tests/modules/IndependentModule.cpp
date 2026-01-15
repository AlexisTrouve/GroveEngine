#include "IndependentModule.h"
#include "grove/JsonDataNode.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace grove {

void IndependentModule::process(const IDataNode& input) {
    processCount_++;
    // Simple processing - just increment counter
}

void IndependentModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
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
    } else {
        configNode_ = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }

    logger_->info("IndependentModule v{} initialized (isolated witness)", version_);
}

const IDataNode& IndependentModule::getConfiguration() {
    if (!configNode_) {
        configNode_ = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *configNode_;
}

std::unique_ptr<IDataNode> IndependentModule::getHealthStatus() {
    json status;
    status["status"] = "healthy";
    status["processCount"] = processCount_.load();
    status["reloadCount"] = reloadCount_.load();
    status["version"] = version_;
    return std::make_unique<JsonDataNode>("health", status);
}

void IndependentModule::shutdown() {
    if (logger_) {
        logger_->info("IndependentModule v{} shutting down", version_);
    }
}

std::unique_ptr<IDataNode> IndependentModule::getState() {
    json state;
    state["processCount"] = processCount_.load();
    state["reloadCount"] = reloadCount_.load();
    state["version"] = version_;
    return std::make_unique<JsonDataNode>("state", state);
}

void IndependentModule::setState(const IDataNode& state) {
    reloadCount_++;  // Track reload attempts

    const auto* jsonStateNode = dynamic_cast<const JsonDataNode*>(&state);
    if (jsonStateNode) {
        const auto& jsonData = jsonStateNode->getJsonData();
        if (jsonData.contains("processCount")) {
            processCount_ = jsonData["processCount"];
        }
        if (jsonData.contains("reloadCount")) {
            reloadCount_ = jsonData["reloadCount"];
        }
        if (jsonData.contains("version")) {
            version_ = jsonData["version"];
        }
        if (logger_) {
            logger_->info("IndependentModule state restored: v{}, reloadCount={}", version_, reloadCount_.load());
        }
    } else {
        if (logger_) {
            logger_->error("IndependentModule: Failed to restore state: not a JsonDataNode");
        }
    }
}

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule() {
        return new grove::IndependentModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
