#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <string>

namespace grove {

/**
 * @brief Batch module for IO System low-frequency subscription testing
 *
 * Tests batching and low-frequency message delivery.
 */
class BatchModule : public IModule {
public:
    BatchModule();
    ~BatchModule() override;

    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "BatchModule"; }
    bool isIdle() const override { return true; }

    // Test helpers
    int getBatchCount() const { return batchCount; }

private:
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    std::unique_ptr<IDataNode> config;

    int batchCount = 0;
};

} // namespace grove
