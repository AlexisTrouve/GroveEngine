#pragma once

#include <grove/IModule.h>
#include <grove/IDataNode.h>
#include <memory>
#include <spdlog/spdlog.h>

namespace grove {

/**
 * @brief Module for stress testing hot-reload stability over long duration
 *
 * This module is intentionally simple to focus on hot-reload mechanics:
 * - No random crashes (unlike ChaosModule)
 * - Minimal state (frameCount, reloadCount)
 * - Lightweight processing
 * - Focus: memory stability, reload reliability, performance consistency
 */
class StressModule : public IModule {
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
    bool isIdle() const override { return !isProcessing_; }

private:
    uint64_t frameCount_ = 0;
    uint64_t reloadCount_ = 0;
    float totalTime_ = 0.0f;
    bool isProcessing_ = false;

    // Simple dummy data to have some state to preserve
    static constexpr size_t DUMMY_DATA_SIZE = 100;
    int dummyData_[DUMMY_DATA_SIZE];

    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<IDataNode> config_;

    void initializeDummyData();
    bool validateDummyData() const;
};

} // namespace grove

// Factory function
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
