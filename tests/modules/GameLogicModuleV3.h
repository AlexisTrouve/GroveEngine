#pragma once

#include "GameLogicModuleV2.h"

namespace grove {

struct EntityV3 {
    float x, y;      // Position
    float vx, vy;    // Velocity
    int id;
    bool collided;   // From v2
    float mass;      // NEW in v3: for advanced physics

    EntityV3(int _id = 0, float _x = 0.0f, float _y = 0.0f, float _vx = 0.0f, float _vy = 0.0f)
        : x(_x), y(_y), vx(_vx), vy(_vy), id(_id), collided(false), mass(1.0f) {}

    // Constructor from V2 Entity (for migration)
    EntityV3(const EntityV2& e)
        : x(e.x), y(e.y), vx(e.vx), vy(e.vy), id(e.id), collided(e.collided), mass(1.0f) {}
};

/**
 * GameLogicModule Version 3 - Advanced Physics
 *
 * Optimized logic:
 * - Movement with advanced physics (gravity, friction)
 * - Collision detection (from v2)
 * - NEW: Mass-based physics simulation
 * - State migration from v1 and v2
 */
class GameLogicModuleV3 : public IModule {
public:
    GameLogicModuleV3();
    ~GameLogicModuleV3() override = default;

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

    int getVersion() const override { return 3; }

    // V3: State migration from v1 and v2
    bool migrateStateFrom(int fromVersion, const IDataNode& oldState) override;

    // V3 specific
    void updateEntities(float dt);
    size_t getEntityCount() const { return entities_.size(); }
    const std::vector<EntityV3>& getEntities() const { return entities_; }

private:
    void applyPhysics(EntityV3& e, float dt);
    void checkCollisions(EntityV3& e);

    std::vector<EntityV3> entities_;
    std::atomic<int> processCount_{0};
    std::atomic<int> collisionCount_{0};
    std::unique_ptr<IDataNode> config_;
    IIO* io_ = nullptr;
    ITaskScheduler* scheduler_ = nullptr;
    bool initialized_ = false;

    // Physics parameters
    float gravity_ = 9.8f;
    float friction_ = 0.99f;
};

} // namespace grove

// C API for dynamic loading
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
