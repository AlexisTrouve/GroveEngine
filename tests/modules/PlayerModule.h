#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/IDataTree.h>
#include <grove/JsonDataNode.h>
#include <memory>
#include <string>
#include <vector>

namespace grove {

/**
 * @brief Module that manages player state and publishes events
 */
class PlayerModule : public IModule {
public:
    PlayerModule();
    ~PlayerModule() override;

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "PlayerModule"; }
    bool isIdle() const override { return true; }

    // Set DataTree for player data
    void setDataTree(IDataTree* tree);

private:
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    IDataTree* tree = nullptr;
    std::unique_ptr<IDataNode> config;

    int gold = 1000;
    int level = 1;
    std::string playerName = "Player1";
    std::vector<std::string> inventory;

    void handleConfigChange();
    void savePlayerData();
    void publishLevelUp();
};

} // namespace grove
