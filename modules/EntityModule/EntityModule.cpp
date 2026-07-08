#include "EntityModule.h"

#include <grove/IDataNode.h>
#include <grove/JsonDataNode.h>

#include <nlohmann/json.hpp>

namespace grove {

namespace {
// Robust JSON accessors — the engine must NOT crash on an imperfect consumer payload (AI-consumer
// thesis: fail soft to a default, never throw on a type mismatch). nlohmann's get<T>() throws; these don't.
float num(const nlohmann::json& j, const char* key, float dflt) {
    auto it = j.find(key);
    if (it == j.end()) return dflt;
    if (it->is_number()) return it->get<float>();
    if (it->is_string()) { try { return std::stof(it->get<std::string>()); } catch (...) {} }
    return dflt;
}
int inum(const nlohmann::json& j, const char* key, int dflt) {
    auto it = j.find(key);
    if (it == j.end()) return dflt;
    if (it->is_number()) return it->get<int>();
    if (it->is_string()) { try { return std::stoi(it->get<std::string>(), nullptr, 0); } catch (...) {} }
    return dflt;
}
std::string str(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}
// Colour accepts a number (0xRRGGBBAA) or a "0x..."/decimal string. Reinterpreted as uint32.
uint32_t color(const nlohmann::json& j, const char* key, uint32_t dflt) {
    auto it = j.find(key);
    if (it == j.end()) return dflt;
    if (it->is_number()) return static_cast<uint32_t>(it->get<int64_t>());
    if (it->is_string()) { try { return static_cast<uint32_t>(std::stoul(it->get<std::string>(), nullptr, 0)); } catch (...) {} }
    return dflt;
}

// Merge provided fields onto an existing component (partial update: omitted keys keep their value).
void mergeTransform(const nlohmann::json& jt, entity::Transform& t) {
    t.cx = num(jt, "cx", t.cx);         t.cy = num(jt, "cy", t.cy);
    t.rotation = num(jt, "rotation", t.rotation);
    t.scaleX = num(jt, "scaleX", t.scaleX);   t.scaleY = num(jt, "scaleY", t.scaleY);
}
void mergeSprite(const nlohmann::json& js, entity::Sprite& s) {
    if (js.contains("asset")) s.asset = str(js, "asset");
    s.textureId = inum(js, "textureId", s.textureId);
    s.color = color(js, "color", s.color);
    s.layer = inum(js, "layer", s.layer);
}

// One behavior from the fixed library. Unknown type -> nullopt-ish (Lifetime 0 would kill instantly, so we
// signal "skip" by returning false).
bool parseBehavior(const nlohmann::json& jb, entity::Behavior& out) {
    const std::string type = str(jb, "type");
    if (type == "move")     { out = entity::move(num(jb, "vx", 0.0f), num(jb, "vy", 0.0f)); return true; }
    if (type == "spin")     { out = entity::spin(num(jb, "degPerSec", 0.0f)); return true; }
    if (type == "lifetime") { out = entity::lifetime(num(jb, "seconds", 0.0f)); return true; }
    return false;   // unknown behavior type — ignored (fail soft)
}

// Build a prefab TEMPLATE from a payload's transform/sprite/behaviors (same fields as a spawn, but no id).
entity::Prefab parsePrefab(const nlohmann::json& j) {
    entity::Prefab p;
    if (auto t = j.find("transform"); t != j.end() && t->is_object()) mergeTransform(*t, p.transform);
    if (auto s = j.find("sprite"); s != j.end() && s->is_object()) { mergeSprite(*s, p.sprite); p.sprite.present = true; }
    if (auto b = j.find("behaviors"); b != j.end() && b->is_array())
        for (const auto& jb : *b) { entity::Behavior beh; if (parseBehavior(jb, beh)) p.behaviors.push_back(beh); }
    return p;
}
} // namespace

EntityModule::EntityModule()
    : m_config(std::make_unique<JsonDataNode>("config")) {}

EntityModule::~EntityModule() = default;

void EntityModule::setConfiguration(const IDataNode& /*config*/, IIO* io, ITaskScheduler* /*scheduler*/) {
    m_io = io;
    if (m_io) {
        auto cb = [this](const Message& msg) { handleMessage(msg); };
        m_io->subscribe("entity:prefab", cb);   // register a reusable archetype/template
        m_io->subscribe("entity:spawn", cb);
        m_io->subscribe("entity:set", cb);
        m_io->subscribe("entity:destroy", cb);
    }
}

void EntityModule::process(const IDataNode& input) {
    // 1. Drain the entity:* input (spawn/set/destroy applied to the world).
    while (m_io && m_io->hasMessages() > 0) m_io->pullAndDispatch();
    // 2. Tick the reusable behaviors by dt, 3. emit the retained-render diff. (tick-then-emit: an entity's
    //    first rendered frame is after one tick, the standard update->render order.)
    const double dt = input.getDouble("deltaTime", 0.016);
    m_world.tick(static_cast<float>(dt));
    emitDiff();
}

void EntityModule::handleMessage(const Message& msg) {
    if (!msg.data) return;
    const auto* jn = dynamic_cast<const JsonDataNode*>(msg.data.get());
    if (!jn) return;                          // entity payloads are nested objects -> need the json
    const nlohmann::json& j = jn->getJsonData();

    // entity:prefab {name, ...} — register a reusable archetype (keyed by NAME, not an entity id).
    if (msg.topic == "entity:prefab") {
        const std::string name = str(j, "name");
        if (!name.empty()) m_world.registerPrefab(name, parsePrefab(j));
        return;
    }

    const std::string id = str(j, "id");
    if (id.empty()) return;                   // an entity needs a string id (the authoring handle)

    if (msg.topic == "entity:spawn") {
        // Optional `archetype`: instantiate a registered prefab, then the spawn's own transform/sprite
        // MERGE on top (per-instance overrides) and its behaviors ADD to the prefab's. No archetype -> a
        // plain empty entity. Unknown archetype -> falls back to a plain spawn (fail soft).
        const std::string archetype = str(j, "archetype");
        const entity::EntityId eid = (!archetype.empty() && m_world.hasPrefab(archetype))
                                         ? m_world.spawnFromPrefab(archetype)
                                         : m_world.spawn();
        m_names[id] = eid;                    // map the string handle (= renderId)
        applyComponents(eid, *msg.data);      // per-instance overrides
        if (auto b = j.find("behaviors"); b != j.end() && b->is_array()) {
            for (const auto& jb : *b) {
                entity::Behavior beh;
                if (parseBehavior(jb, beh)) m_world.addBehavior(eid, beh);
            }
        }
        ++m_spawned;
    }
    else if (msg.topic == "entity:set") {
        auto it = m_names.find(id);
        if (it != m_names.end()) applyComponents(it->second, *msg.data);
    }
    else if (msg.topic == "entity:destroy") {
        auto it = m_names.find(id);
        if (it != m_names.end()) { m_world.destroy(it->second); m_names.erase(it); }
    }
}

// Apply a message's transform/sprite sub-objects onto an entity (partial merge on top of the current values).
void EntityModule::applyComponents(entity::EntityId id, const IDataNode& node) {
    const auto* jn = dynamic_cast<const JsonDataNode*>(&node);
    if (!jn) return;
    const nlohmann::json& j = jn->getJsonData();
    entity::Entity* e = m_world.get(id);
    if (!e) return;

    if (auto t = j.find("transform"); t != j.end() && t->is_object()) {
        entity::Transform tr = e->transform;
        mergeTransform(*t, tr);
        m_world.setTransform(id, tr);
    }
    if (auto s = j.find("sprite"); s != j.end() && s->is_object()) {
        entity::Sprite sp = e->sprite;
        mergeSprite(*s, sp);
        m_world.setSprite(id, sp);   // marks the entity as sprite-bearing (renders)
    }
}

// The retained-render diff -> render:sprite:add / :update / :remove (renderId = world EntityId).
void EntityModule::emitDiff() {
    if (!m_io) return;
    for (const auto& op : m_world.diffRender()) {
        auto n = std::make_unique<JsonDataNode>("s");
        n->setInt("renderId", static_cast<int>(op.id));
        if (op.kind == entity::EntityWorld::RenderOp::Kind::Remove) {
            m_io->publish("render:sprite:remove", std::move(n));
            continue;
        }
        n->setDouble("cx", op.transform.cx);           // cx,cy = CENTER (anchor convention)
        n->setDouble("cy", op.transform.cy);
        n->setDouble("rotation", op.transform.rotation);
        n->setDouble("scaleX", op.transform.scaleX);
        n->setDouble("scaleY", op.transform.scaleY);
        if (!op.sprite.asset.empty()) n->setString("asset", op.sprite.asset);
        else                          n->setInt("textureId", op.sprite.textureId);
        n->setInt("color", static_cast<int>(op.sprite.color));   // uint32 reinterpreted, like UIRenderer
        n->setInt("layer", op.sprite.layer);
        m_io->publish(op.kind == entity::EntityWorld::RenderOp::Kind::Add ? "render:sprite:add"
                                                                          : "render:sprite:update",
                      std::move(n));
    }
}

void EntityModule::shutdown() {
    m_names.clear();
    m_world = entity::EntityWorld{};
}

// Hot-reload state: MVP keeps only the health counter. Full live-world serialization (entities + behaviors
// + snapshot) is a follow-on — on hot-reload the scene is currently rebuilt by the host re-issuing spawns.
std::unique_ptr<IDataNode> EntityModule::getState() {
    auto s = std::make_unique<JsonDataNode>("state");
    s->setInt("spawned", static_cast<int>(m_spawned));
    return s;
}
void EntityModule::setState(const IDataNode& /*state*/) {}

const IDataNode& EntityModule::getConfiguration() { return *m_config; }

std::unique_ptr<IDataNode> EntityModule::getHealthStatus() {
    auto h = std::make_unique<JsonDataNode>("health");
    h->setInt("alive", static_cast<int>(m_world.aliveCount()));
    h->setInt("spawned", static_cast<int>(m_spawned));
    return h;
}

} // namespace grove
