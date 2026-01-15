#include "StressModule.h"
#include <grove/JsonDataNode.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace grove {

void StressModule::setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) {
    // Initialize logger
    logger_ = spdlog::get("StressModule");
    if (!logger_) {
        logger_ = spdlog::stdout_color_mt("StressModule");
    }
    logger_->set_level(spdlog::level::info);

    // Clone config
    const auto* jsonConfigNode = dynamic_cast<const JsonDataNode*>(&configNode);
    if (jsonConfigNode) {
        config_ = std::make_unique<JsonDataNode>("config", jsonConfigNode->getJsonData());
    } else {
        config_ = std::make_unique<JsonDataNode>("config");
    }

    logger_->info("Initializing StressModule");
    initializeDummyData();
}

const IDataNode& StressModule::getConfiguration() {
    return *config_;
}

void StressModule::process(const IDataNode& input) {
    isProcessing_ = true;
    frameCount_++;

    // Lightweight processing - just validate data integrity periodically
    if (frameCount_ % 1000 == 0) {
        if (!validateDummyData()) {
            logger_->error("Data validation failed at frame {}", frameCount_);
        }
    }

    // Log progress every 60 seconds (3600 frames @ 60 FPS)
    if (frameCount_ % 3600 == 0) {
        logger_->info("Progress: {} frames, {} reloads", frameCount_, reloadCount_);
    }

    isProcessing_ = false;
}

std::unique_ptr<IDataNode> StressModule::getHealthStatus() {
    nlohmann::json healthJson;
    healthJson["status"] = validateDummyData() ? "healthy" : "corrupted";
    healthJson["frameCount"] = frameCount_;
    healthJson["reloadCount"] = reloadCount_;
    return std::make_unique<JsonDataNode>("health", healthJson);
}

void StressModule::shutdown() {
    logger_->info("Shutting down StressModule");
    logger_->info("  Total frames: {}", frameCount_);
    logger_->info("  Total reloads: {}", reloadCount_);
}

std::string StressModule::getType() const {
    return "stress";
}

std::unique_ptr<IDataNode> StressModule::getState() {
    nlohmann::json json;
    json["frameCount"] = frameCount_;
    json["reloadCount"] = reloadCount_;

    // Save dummy data as array
    nlohmann::json dataArray = nlohmann::json::array();
    for (size_t i = 0; i < DUMMY_DATA_SIZE; ++i) {
        dataArray.push_back(dummyData_[i]);
    }
    json["dummyData"] = dataArray;

    logger_->debug("State saved: frame={}, reload={}", frameCount_, reloadCount_);

    return std::make_unique<JsonDataNode>("state", json);
}

void StressModule::setState(const IDataNode& state) {
    const auto* jsonNode = dynamic_cast<const JsonDataNode*>(&state);
    if (!jsonNode) {
        if (logger_) {
            logger_->error("setState: Invalid state (not JsonDataNode)");
        }
        return;
    }

    const auto& json = jsonNode->getJsonData();

    // Ensure logger is initialized (needed after hot-reload)
    if (!logger_) {
        logger_ = spdlog::get("StressModule");
        if (!logger_) {
            logger_ = spdlog::stdout_color_mt("StressModule");
        }
    }

    // Ensure config is initialized (needed after hot-reload)
    if (!config_) {
        config_ = std::make_unique<JsonDataNode>("config");
    }

    // Defensive: Initialize dummy data first
    initializeDummyData();

    // Restore state
    frameCount_ = json.value("frameCount", 0);
    reloadCount_ = json.value("reloadCount", 0);

    // Increment reload count
    reloadCount_++;

    // Restore dummy data
    if (json.contains("dummyData") && json["dummyData"].is_array()) {
        const auto& dataArray = json["dummyData"];
        if (dataArray.size() == DUMMY_DATA_SIZE) {
            for (size_t i = 0; i < DUMMY_DATA_SIZE; ++i) {
                dummyData_[i] = dataArray[i].get<int>();
            }
        } else {
            logger_->warn("Dummy data size mismatch: expected {}, got {}",
                        DUMMY_DATA_SIZE, dataArray.size());
        }
    }

    // Validate restored data
    if (!validateDummyData()) {
        logger_->error("Data validation failed after setState");
        initializeDummyData(); // Re-initialize if corrupt
    }

    logger_->info("State restored: frame={}, reload={}", frameCount_, reloadCount_);
}

void StressModule::initializeDummyData() {
    // Initialize with predictable pattern for validation
    for (size_t i = 0; i < DUMMY_DATA_SIZE; ++i) {
        dummyData_[i] = static_cast<int>(i * 42);
    }
}

bool StressModule::validateDummyData() const {
    for (size_t i = 0; i < DUMMY_DATA_SIZE; ++i) {
        if (dummyData_[i] != static_cast<int>(i * 42)) {
            if (logger_) {
                logger_->error("Data corruption detected at index {}: expected {}, got {}",
                             i, i * 42, dummyData_[i]);
            }
            return false;
        }
    }
    return true;
}

} // namespace grove

// Factory functions
extern "C" {
    grove::IModule* createModule() {
        return new grove::StressModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
