#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/IDataTree.h>
#include <grove/JsonDataNode.h>
#include <memory>
#include <string>

namespace grove {

/**
 * @brief Module that watches for config changes and publishes notifications
 */
class ConfigWatcherModule : public IModule {
public:
    ConfigWatcherModule();
    ~ConfigWatcherModule() override;

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "ConfigWatcherModule"; }
    bool isIdle() const override { return true; }

    // Set DataTree for config watching
    void setDataTree(IDataTree* tree);

private:
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    IDataTree* tree = nullptr;
    std::unique_ptr<IDataNode> config;

    int configChangesDetected = 0;

    void onConfigReloaded();
    void publishConfigChange(const std::string& configName);
};

} // namespace grove
