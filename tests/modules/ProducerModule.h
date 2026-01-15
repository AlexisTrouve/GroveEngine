#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <string>

namespace grove {

/**
 * @brief Producer module for IO System stress testing
 *
 * Publishes messages at configurable rates to test pub/sub system.
 */
class ProducerModule : public IModule {
public:
    ProducerModule();
    ~ProducerModule() override;

    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "ProducerModule"; }
    bool isIdle() const override { return true; }

    // Test helpers
    int getPublishedCount() const { return publishedCount; }
    void setPublishRate(float rate) { publishRate = rate; }

private:
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    std::unique_ptr<IDataNode> config;

    int publishedCount = 0;
    float publishRate = 100.0f; // Hz
    float accumulator = 0.0f;
};

} // namespace grove
