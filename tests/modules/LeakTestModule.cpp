// ============================================================================
// LeakTestModule.cpp - Module for memory leak detection testing
// ============================================================================

#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include "grove/JsonDataNode.h"
#include "grove/JsonDataValue.h"
#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>

namespace grove {

class LeakTestModule : public IModule {
private:
    std::vector<uint8_t> workBuffer;
    int processCount = 0;
    int lastChecksum = 0;
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    std::unique_ptr<IDataNode> config;

public:
    LeakTestModule() = default;
    ~LeakTestModule() override = default;

    void process(const IDataNode& input) override {
        processCount++;

        // Simulate real workload with allocations (1 MB working buffer)
        workBuffer.resize(1024 * 1024);
        std::fill(workBuffer.begin(), workBuffer.end(),
                  static_cast<uint8_t>(processCount % 256));

        // Small temporary allocations (simulate logging/processing)
        std::vector<std::string> logs;
        logs.reserve(100);
        for (int i = 0; i < 100; i++) {
            logs.push_back("Process iteration " + std::to_string(processCount));
        }

        // Simulate some data processing
        int sum = 0;
        for (size_t i = 0; i < workBuffer.size(); i += 1024) {
            sum += workBuffer[i];
        }
        lastChecksum = sum;
    }

    void setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) override {
        this->io = ioPtr;
        this->scheduler = schedulerPtr;
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }

    const IDataNode& getConfiguration() override {
        if (!config) {
            config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
        }
        return *config;
    }

    std::unique_ptr<IDataNode> getHealthStatus() override {
        nlohmann::json health = {
            {"status", "healthy"},
            {"processCount", processCount},
            {"lastChecksum", lastChecksum},
            {"bufferSize", workBuffer.size()}
        };
        return std::make_unique<JsonDataNode>("health", health);
    }

    void shutdown() override {
        // Clean up
        workBuffer.clear();
        workBuffer.shrink_to_fit();
    }

    std::unique_ptr<IDataNode> getState() override {
        nlohmann::json state = {
            {"processCount", processCount},
            {"lastChecksum", lastChecksum}
        };

        // Simulate storing large state data (100 KB blob as base64)
        std::vector<uint8_t> stateBlob(100 * 1024);
        std::fill(stateBlob.begin(), stateBlob.end(), 0xAB);

        // Store as array of ints in JSON (simpler than base64 for test purposes)
        std::vector<int> blobData;
        blobData.reserve(stateBlob.size());
        for (auto byte : stateBlob) {
            blobData.push_back(byte);
        }
        state["stateBlob"] = blobData;

        return std::make_unique<JsonDataNode>("state", state);
    }

    void setState(const IDataNode& state) override {
        processCount = state.getInt("processCount", 0);
        lastChecksum = state.getInt("lastChecksum", 0);

        // Note: We don't need to restore stateBlob, it's just for testing memory
        // during serialization
    }

    std::string getType() const override {
        return "LeakTestModule";
    }

    bool isIdle() const override {
        return true;
    }
};

} // namespace grove

// ============================================================================
// Module Factory
// ============================================================================

extern "C" {

grove::IModule* createModule() {
    return new grove::LeakTestModule();
}

void destroyModule(grove::IModule* module) {
    delete module;
}

} // extern "C"
