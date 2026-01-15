#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <vector>
#include <string>

namespace grove {

/**
 * @brief Consumer module for IO System stress testing
 *
 * Subscribes to topics and collects received messages for testing.
 */
class ConsumerModule : public IModule {
public:
    ConsumerModule();
    ~ConsumerModule() override;

    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "ConsumerModule"; }
    bool isIdle() const override { return true; }

    // Test helpers
    int getReceivedCount() const { return receivedCount; }
    void clearReceived() { receivedCount = 0; }

private:
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    std::unique_ptr<IDataNode> config;

    int receivedCount = 0;
};

} // namespace grove
