#include "DependentModule.h"
#include "grove/JsonDataNode.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace grove {

void DependentModule::process(const IDataNode& input) {
    processCount_++;

    // For this test, we just simulate collecting numbers
    // In a real system, this would call baseModule_->generateNumber()
    // but that requires linking, which complicates the test
    collectedNumbers_.push_back(processCount_);
}

void DependentModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
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

    logger_->info("DependentModule v{} initialized (depends on: BaseModule)", version_);
}

const IDataNode& DependentModule::getConfiguration() {
    if (!configNode_) {
        configNode_ = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *configNode_;
}

std::unique_ptr<IDataNode> DependentModule::getHealthStatus() {
    json status;
    status["status"] = "healthy";
    status["processCount"] = processCount_;
    status["version"] = version_;
    status["collectedCount"] = collectedNumbers_.size();
    status["hasBaseModule"] = (baseModule_ != nullptr);

    if (!collectedNumbers_.empty()) {
        status["lastCollected"] = collectedNumbers_.back();
    }

    return std::make_unique<JsonDataNode>("health", status);
}

void DependentModule::shutdown() {
    if (logger_) {
        logger_->info("DependentModule v{} shutting down (collected {} numbers)",
                      version_, collectedNumbers_.size());
    }
    baseModule_ = nullptr;  // Release reference
}

std::unique_ptr<IDataNode> DependentModule::getState() {
    json state;
    state["processCount"] = processCount_;
    state["version"] = version_;
    state["collectedNumbers"] = collectedNumbers_;
    return std::make_unique<JsonDataNode>("state", state);
}

void DependentModule::setState(const IDataNode& state) {
    const auto* jsonStateNode = dynamic_cast<const JsonDataNode*>(&state);
    if (jsonStateNode) {
        const auto& jsonData = jsonStateNode->getJsonData();
        if (jsonData.contains("processCount")) {
            processCount_ = jsonData["processCount"];
        }
        if (jsonData.contains("version")) {
            version_ = jsonData["version"];
        }
        if (jsonData.contains("collectedNumbers")) {
            collectedNumbers_ = jsonData["collectedNumbers"].get<std::vector<int>>();
        }
        if (logger_) {
            logger_->info("DependentModule state restored: v{}, processCount={}, collected={}",
                          version_, processCount_, collectedNumbers_.size());
        }
    } else {
        if (logger_) {
            logger_->error("DependentModule: Failed to restore state: not a JsonDataNode");
        }
    }
}

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule() {
        return new grove::DependentModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
