#pragma once
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include <vector>
#include <random>
#include <memory>
#include <spdlog/spdlog.h>

namespace grove {

class TankModule : public IModule {
public:
    struct Tank {
        float x, y;           // Position
        float vx, vy;         // Vélocité
        float cooldown;       // Temps avant prochain tir
        float targetX, targetY; // Destination
        int id;               // Identifiant unique
    };

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override;
    bool isIdle() const override { return true; }

private:
    std::vector<Tank> tanks;
    int frameCount = 0;
    std::string moduleVersion = "v1.0"; // Module logging
    std::shared_ptr<spdlog::logger> logger;
    std::unique_ptr<IDataNode> config;

    void updateTank(Tank& tank, float dt);
    void spawnTanks(int count);
};

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
