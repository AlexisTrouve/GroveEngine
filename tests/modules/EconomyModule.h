#pragma once

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/IDataTree.h>
#include <grove/JsonDataNode.h>
#include <memory>
#include <string>

namespace grove {

/**
 * @brief Module that manages economy and responds to player events
 */
class EconomyModule : public IModule {
public:
    EconomyModule();
    ~EconomyModule() override;

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "EconomyModule"; }
    bool isIdle() const override { return true; }

    // Set DataTree for economy data
    void setDataTree(IDataTree* tree);

private:
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    IDataTree* tree = nullptr;
    std::unique_ptr<IDataNode> config;

    int totalBonusesApplied = 0;
    int playerEventsProcessed = 0;

    void handlePlayerEvent(const std::string& topic, IDataNode* data);
    void applyEconomyBonus(int goldBonus);
};

} // namespace grove
