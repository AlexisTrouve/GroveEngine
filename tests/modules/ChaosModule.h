#pragma once
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include <random>
#include <memory>
#include <spdlog/spdlog.h>

namespace grove {

class ChaosModule : public IModule {
public:
    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override;
    bool isIdle() const override { return !isProcessing; }

private:
    std::mt19937 rng;
    int frameCount = 0;
    int crashCount = 0;
    int corruptionCount = 0;
    int hotReloadCount = 0;
    bool isProcessing = false;
    bool isCorrupted = false;

    // Configuration du chaos
    float hotReloadProbability = 0.30f;
    float crashProbability = 0.10f;
    float corruptionProbability = 0.10f;
    float invalidConfigProbability = 0.05f;

    std::shared_ptr<spdlog::logger> logger;
    std::unique_ptr<IDataNode> config;

    // Simulations de failures
    void triggerChaosEvent();
};

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
