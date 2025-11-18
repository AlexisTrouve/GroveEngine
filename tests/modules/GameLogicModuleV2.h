#pragma once

#include "GameLogicModuleV1.h"

namespace grove {

struct EntityV2 {
    float x, y;      // Position
    float vx, vy;    // Velocity
    int id;
    bool collided;   // NEW in v2: collision flag

    EntityV2(int _id = 0, float _x = 0.0f, float _y = 0.0f, float _vx = 0.0f, float _vy = 0.0f)
        : x(_x), y(_y), vx(_vx), vy(_vy), id(_id), collided(false) {}

    // Constructor from V1 Entity (for migration)
    EntityV2(const Entity& e)
        : x(e.x), y(e.y), vx(e.vx), vy(e.vy), id(e.id), collided(false) {}
};

/**
 * GameLogicModule Version 2 - Collision Detection
 *
 * Enhanced logic:
 * - Movement (same as v1)
 * - NEW: Collision detection with boundaries
 * - State migration from v1 (add collision flags)
 */
class GameLogicModuleV2 : public IModule {
public:
    GameLogicModuleV2();
    ~GameLogicModuleV2() override = default;

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

    int getVersion() const override { return 2; }

    // V2: State migration from v1
    bool migrateStateFrom(int fromVersion, const IDataNode& oldState) override;

    // V2 specific
    void updateEntities(float dt);
    size_t getEntityCount() const { return entities_.size(); }
    const std::vector<EntityV2>& getEntities() const { return entities_; }

private:
    void checkCollisions(EntityV2& e);

    std::vector<EntityV2> entities_;
    std::atomic<int> processCount_{0};
    std::atomic<int> collisionCount_{0};
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
