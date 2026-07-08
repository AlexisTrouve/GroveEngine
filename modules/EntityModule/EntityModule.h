#pragma once

/**
 * EntityModule — the data-driven scene/entity layer as an IModule (scene/entity slice E2).
 *
 * WHAT  : Wraps the pure `entity::EntityWorld` (entities + components + the fixed behavior library) and
 *         maps it onto the bus. Each process() drains the entity:* input topics, ticks the world by dt,
 *         and turns the retained-render diff into render:sprite:add / :update / :remove (keyed by the
 *         entity id = renderId). The game composes entities/behaviors in DATA; the engine ticks the
 *         reusable behaviors and emits the render traffic — no per-project boilerplate, no scripting.
 *
 * TOPICS (consumed):
 *   entity:spawn   { id, transform?:{cx,cy,rotation,scaleX,scaleY}, sprite?:{asset|textureId,color,layer},
 *                    behaviors?:[{type:"move",vx,vy} | {type:"spin",degPerSec} | {type:"lifetime",seconds}] }
 *   entity:set     { id, transform?:{...}, sprite?:{...} }   — partial: omitted fields keep their value
 *   entity:destroy { id }
 *
 * TOPICS (published):
 *   render:sprite:add    { renderId, cx, cy, rotation, scaleX, scaleY, asset|textureId, color, layer }
 *   render:sprite:update { renderId, cx, cy, rotation, scaleX, scaleY, asset|textureId, color, layer }
 *   render:sprite:remove { renderId }
 *
 * WHY   : behavior must be practical ACROSS PROJECTS, so it lives engine-side (EntityWorld's library),
 *         reusable by every game. Pure core stays headless-testable; this only wires it to IIO. A C++
 *         static-link host (Drifterra) can skip the topics and drive `world()` directly, then process()
 *         each frame to tick + emit. Bespoke game logic stays consumer-side (mutate components).
 *
 * ID model: the authoring surface uses STRING ids ("player"); the module maps each to the world's numeric
 *         EntityId (which doubles as the renderer's renderId). The C++ API exposes the numeric world directly.
 */

#include <grove/IModule.h>
#include <grove/IIO.h>
#include <grove/ITaskScheduler.h>
#include "grove/entity/EntityWorld.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace grove {

class EntityModule : public IModule {
public:
    EntityModule();
    ~EntityModule() override;

    // IModule interface
    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override;
    void process(const IDataNode& input) override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    std::string getType() const override { return "entity"; }
    bool isIdle() const override { return true; }

    // C++ static-link API (Drifterra): drive the world directly, then process(dt) to tick + emit.
    entity::EntityWorld& world() { return m_world; }

private:
    void handleMessage(const Message& msg);
    void emitDiff();                              // diffRender() -> render:sprite:add/update/remove
    entity::EntityId resolve(const std::string& name, bool createIfMissing);
    void applyComponents(entity::EntityId id, const IDataNode& d);   // transform / sprite from a node

    IIO* m_io = nullptr;
    std::unique_ptr<IDataNode> m_config;
    entity::EntityWorld m_world;
    std::map<std::string, entity::EntityId> m_names;   // authoring string id -> world EntityId (renderId)
    uint64_t m_spawned = 0;                            // health counter
};

} // namespace grove
