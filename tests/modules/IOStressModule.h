#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <string>

namespace grove {

/**
 * @brief IO Stress module for concurrent pub/sub testing
 *
 * Stress tests the IO system with high-frequency operations.
 */
class IOStressModule : public IModule {
public:
    IOStressModule();
    ~IOStressModule() override;

    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "IOStressModule"; }
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
