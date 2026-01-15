#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/IDataTree.h>
#include <grove/JsonDataNode.h>
#include <memory>
#include <string>

namespace grove {

/**
 * @brief Module that collects metrics and publishes snapshots
 */
class MetricsModule : public IModule {
public:
    MetricsModule();
    ~MetricsModule() override;

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "MetricsModule"; }
    bool isIdle() const override { return true; }

    // Set DataTree for metrics data
    void setDataTree(IDataTree* tree);

private:
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    IDataTree* tree = nullptr;
    std::unique_ptr<IDataNode> config;

    int snapshotsPublished = 0;
    float accumulator = 0.0f;

    void collectMetrics();
    void publishSnapshot();
};

} // namespace grove
