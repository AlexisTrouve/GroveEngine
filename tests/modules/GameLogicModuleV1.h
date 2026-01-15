#pragma once

#include "grove/IModule.h"
#include <vector>
#include <memory>
#include <atomic>
#include <cmath>

namespace grove {

struct Entity {
    float x, y;      // Position
    float vx, vy;    // Velocity
    int id;

    Entity(int _id = 0, float _x = 0.0f, float _y = 0.0f, float _vx = 0.0f, float _vy = 0.0f)
        : x(_x), y(_y), vx(_vx), vy(_vy), id(_id) {}
};

/**
 * GameLogicModule Version 1 - Baseline
 *
 * Simple movement logic:
 * - Update position: x += vx * dt, y += vy * dt
 * - No collision detection
 * - Basic state management
 */
class GameLogicModuleV1 : public IModule {
public:
    GameLogicModuleV1();
    ~GameLogicModuleV1() override = default;

    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "GameLogic"; }
    bool isIdle() const override { return true; }

    int getVersion() const override { return 1; }

    // V1 specific
    void updateEntities(float dt);
    size_t getEntityCount() const { return entities_.size(); }
    const std::vector<Entity>& getEntities() const { return entities_; }

protected:
    std::vector<Entity> entities_;
    std::atomic<int> processCount_{0};
    std::unique_ptr<IDataNode> config_;
    IIO* io_ = nullptr;
    ITaskScheduler* scheduler_ = nullptr;
    bool initialized_ = false;
};

} // namespace grove

// C API for dynamic loading
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
