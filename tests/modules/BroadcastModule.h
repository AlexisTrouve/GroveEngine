#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <string>

namespace grove {

/**
 * @brief Broadcast module for IO System stress testing
 *
 * Similar to ConsumerModule but used for multi-subscriber broadcast tests.
 */
class BroadcastModule : public IModule {
public:
    BroadcastModule();
    ~BroadcastModule() override;

    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "BroadcastModule"; }
    bool isIdle() const override { return true; }

    // Test helpers
    int getReceivedCount() const { return receivedCount; }

private:
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    std::unique_ptr<IDataNode> config;

    int receivedCount = 0;
};

} // namespace grove
