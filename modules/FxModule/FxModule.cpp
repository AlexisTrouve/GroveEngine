#include "FxModule.h"

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
void mergeTransform(const nlohmann::json& jt, fx::Transform& t) {
    t.cx = num(jt, "cx", t.cx);         t.cy = num(jt, "cy", t.cy);
    t.rotation = num(jt, "rotation", t.rotation);
    t.scaleX = num(jt, "scaleX", t.scaleX);   t.scaleY = num(jt, "scaleY", t.scaleY);
}
void mergeSprite(const nlohmann::json& js, fx::Sprite& s) {
    if (js.contains("asset")) s.asset = str(js, "asset");
    s.textureId = inum(js, "textureId", s.textureId);
    s.color = color(js, "color", s.color);
    s.layer = inum(js, "layer", s.layer);
}
// Merge a text component. The string is taken AS-IS (already localized by the consumer — i18n-agnostic).
void mergeText(const nlohmann::json& jx, fx::Text& tx) {
    if (jx.contains("text")) tx.text = str(jx, "text");
    tx.color = color(jx, "color", tx.color);
    tx.layer = inum(jx, "layer", tx.layer);
    tx.fontSize = inum(jx, "fontSize", tx.fontSize);
}
// Merge an emitter component (a one-shot particle burst). `prefab` = the particle template to instantiate.
void mergeEmitter(const nlohmann::json& je, fx::Emitter& em) {
    if (je.contains("prefab")) em.prefab = str(je, "prefab");
    em.count = inum(je, "count", em.count);
    em.speedMin = num(je, "speedMin", em.speedMin);
    em.speedMax = num(je, "speedMax", em.speedMax);
    em.spreadDeg = num(je, "spreadDeg", em.spreadDeg);
    em.dirDeg = num(je, "dirDeg", em.dirDeg);
    if (auto it = je.find("oneShot"); it != je.end() && it->is_boolean()) em.oneShot = it->get<bool>();
}

// One behavior from the fixed library. Unknown type -> nullopt-ish (Lifetime 0 would kill instantly, so we
// signal "skip" by returning false).
bool parseBehavior(const nlohmann::json& jb, fx::Behavior& out) {
    const std::string type = str(jb, "type");
    if (type == "move")     { out = fx::move(num(jb, "vx", 0.0f), num(jb, "vy", 0.0f)); return true; }
    if (type == "spin")     { out = fx::spin(num(jb, "degPerSec", 0.0f)); return true; }
    if (type == "lifetime") { out = fx::lifetime(num(jb, "seconds", 0.0f)); return true; }
    // fade: ramp sprite alpha fromAlpha->toAlpha over `seconds` (defaults = fade-out 1->0). VFX dissolve.
    if (type == "fade")     { out = fx::fade(num(jb, "seconds", 0.0f), num(jb, "fromAlpha", 1.0f), num(jb, "toAlpha", 0.0f)); return true; }
    // velocity: initial (vx,vy) that decelerates by `drag` per second (debris/spark spread; drag 0 = constant).
    if (type == "velocity") { out = fx::velocity(num(jb, "vx", 0.0f), num(jb, "vy", 0.0f), num(jb, "drag", 0.0f)); return true; }
    return false;   // unknown behavior type — ignored (fail soft)
}

// Build a prefab TEMPLATE from a payload's transform/sprite/behaviors (same fields as a spawn, but no id).
fx::Prefab parsePrefab(const nlohmann::json& j) {
    fx::Prefab p;
    if (auto t = j.find("transform"); t != j.end() && t->is_object()) mergeTransform(*t, p.transform);
    if (auto s = j.find("sprite"); s != j.end() && s->is_object()) { mergeSprite(*s, p.sprite); p.sprite.present = true; }
    if (auto x = j.find("text"); x != j.end() && x->is_object()) { mergeText(*x, p.text); p.text.present = true; }
    if (auto e = j.find("emitter"); e != j.end() && e->is_object()) { mergeEmitter(*e, p.emitter); p.emitter.present = true; }
    if (auto b = j.find("behaviors"); b != j.end() && b->is_array())
        for (const auto& jb : *b) { fx::Behavior beh; if (parseBehavior(jb, beh)) p.behaviors.push_back(beh); }
    return p;
}
} // namespace

FxModule::FxModule()
    : m_config(std::make_unique<JsonDataNode>("config")) {}

FxModule::~FxModule() = default;

void FxModule::setConfiguration(const IDataNode& /*config*/, IIO* io, ITaskScheduler* /*scheduler*/) {
    m_io = io;
    if (m_io) {
        auto cb = [this](const Message& msg) { handleMessage(msg); };
        m_io->subscribe("fx:prefab", cb);   // register a reusable archetype/template
        m_io->subscribe("fx:spawn", cb);
        m_io->subscribe("fx:set", cb);
        m_io->subscribe("fx:destroy", cb);
    }
}

void FxModule::process(const IDataNode& input) {
    // 1. Drain the entity:* input (spawn/set/destroy applied to the world).
    while (m_io && m_io->hasMessages() > 0) m_io->pullAndDispatch();
    // 2. Tick the reusable behaviors by dt, 3. emit the retained-render diff. (tick-then-emit: an entity's
    //    first rendered frame is after one tick, the standard update->render order.)
    const double dt = input.getDouble("deltaTime", 0.016);
    m_world.tick(static_cast<float>(dt));
    emitDiff();
}

void FxModule::handleMessage(const Message& msg) {
    if (!msg.data) return;
    const auto* jn = dynamic_cast<const JsonDataNode*>(msg.data.get());
    if (!jn) return;                          // entity payloads are nested objects -> need the json
    const nlohmann::json& j = jn->getJsonData();

    // entity:prefab {name, ...} — register a reusable archetype (keyed by NAME, not an entity id).
    if (msg.topic == "fx:prefab") {
        const std::string name = str(j, "name");
        if (!name.empty()) m_world.registerPrefab(name, parsePrefab(j));
        return;
    }

    const std::string id = str(j, "id");
    if (id.empty()) return;                   // an entity needs a string id (the authoring handle)

    if (msg.topic == "fx:spawn") {
        // Optional `archetype`: instantiate a registered prefab, then the spawn's own transform/sprite
        // MERGE on top (per-instance overrides) and its behaviors ADD to the prefab's. No archetype -> a
        // plain empty entity. Unknown archetype -> falls back to a plain spawn (fail soft).
        const std::string archetype = str(j, "archetype");
        const fx::EntityId eid = (!archetype.empty() && m_world.hasPrefab(archetype))
                                         ? m_world.spawnFromPrefab(archetype)
                                         : m_world.spawn();
        m_names[id] = eid;                    // map the string handle (= renderId)
        applyComponents(eid, *msg.data);      // per-instance overrides
        if (auto b = j.find("behaviors"); b != j.end() && b->is_array()) {
            for (const auto& jb : *b) {
                fx::Behavior beh;
                if (parseBehavior(jb, beh)) m_world.addBehavior(eid, beh);
            }
        }
        ++m_spawned;
    }
    else if (msg.topic == "fx:set") {
        auto it = m_names.find(id);
        if (it != m_names.end()) applyComponents(it->second, *msg.data);
    }
    else if (msg.topic == "fx:destroy") {
        auto it = m_names.find(id);
        if (it != m_names.end()) { m_world.destroy(it->second); m_names.erase(it); }
    }
}

// Apply a message's transform/sprite sub-objects onto an entity (partial merge on top of the current values).
void FxModule::applyComponents(fx::EntityId id, const IDataNode& node) {
    const auto* jn = dynamic_cast<const JsonDataNode*>(&node);
    if (!jn) return;
    const nlohmann::json& j = jn->getJsonData();
    fx::Entity* e = m_world.get(id);
    if (!e) return;

    if (auto t = j.find("transform"); t != j.end() && t->is_object()) {
        fx::Transform tr = e->transform;
        mergeTransform(*t, tr);
        m_world.setTransform(id, tr);
    }
    if (auto s = j.find("sprite"); s != j.end() && s->is_object()) {
        fx::Sprite sp = e->sprite;
        mergeSprite(*s, sp);
        m_world.setSprite(id, sp);   // marks the entity as sprite-bearing (renders)
    }
    if (auto x = j.find("text"); x != j.end() && x->is_object()) {
        fx::Text tx = e->text;
        mergeText(*x, tx);
        m_world.setText(id, tx);     // marks the entity as text-bearing (render:text)
    }
    if (auto em = j.find("emitter"); em != j.end() && em->is_object()) {
        fx::Emitter cfg = e->emitter;
        mergeEmitter(*em, cfg);
        m_world.setEmitter(id, cfg); // arms a one-shot particle burst (fires on the next tick)
    }
}

// The retained-render diff -> render:<prim>:add / :update / :remove (renderId = world EntityId). A Sprite op
// routes to render:sprite:* (cx,cy = CENTER); a Text op routes to render:text:* (x,y = top-left CORNER, that
// primitive's native anchor — so the entity's Transform position maps to the text's x,y).
void FxModule::emitDiff() {
    if (!m_io) return;
    using Prim = fx::FxWorld::RenderOp::Prim;
    using Kind = fx::FxWorld::RenderOp::Kind;
    for (const auto& op : m_world.diffRender()) {
        auto n = std::make_unique<JsonDataNode>("s");
        n->setInt("renderId", static_cast<int>(op.id));

        if (op.prim == Prim::Text) {
            if (op.kind == Kind::Remove) { m_io->publish("render:text:remove", std::move(n)); continue; }
            n->setDouble("x", op.transform.cx);        // render:text anchor = top-left corner (see note above)
            n->setDouble("y", op.transform.cy);
            n->setString("text", op.text.text);        // already-resolved string (i18n = consumer's job)
            n->setInt("color", static_cast<int>(op.text.color));
            n->setInt("layer", op.text.layer);
            n->setInt("fontSize", op.text.fontSize);
            m_io->publish(op.kind == Kind::Add ? "render:text:add" : "render:text:update", std::move(n));
            continue;
        }

        // Sprite primitive.
        if (op.kind == Kind::Remove) { m_io->publish("render:sprite:remove", std::move(n)); continue; }
        n->setDouble("cx", op.transform.cx);           // cx,cy = CENTER (anchor convention)
        n->setDouble("cy", op.transform.cy);
        n->setDouble("rotation", op.transform.rotation);
        n->setDouble("scaleX", op.transform.scaleX);
        n->setDouble("scaleY", op.transform.scaleY);
        if (!op.sprite.asset.empty()) n->setString("asset", op.sprite.asset);
        else                          n->setInt("textureId", op.sprite.textureId);
        n->setInt("color", static_cast<int>(op.sprite.color));   // uint32 reinterpreted, like UIRenderer
        n->setInt("layer", op.sprite.layer);
        m_io->publish(op.kind == Kind::Add ? "render:sprite:add" : "render:sprite:update", std::move(n));
    }
}

void FxModule::shutdown() {
    m_names.clear();
    m_world = fx::FxWorld{};
}

// Hot-reload state: MVP keeps only the health counter. Full live-world serialization (entities + behaviors
// + snapshot) is a follow-on — on hot-reload the scene is currently rebuilt by the host re-issuing spawns.
std::unique_ptr<IDataNode> FxModule::getState() {
    auto s = std::make_unique<JsonDataNode>("state");
    s->setInt("spawned", static_cast<int>(m_spawned));
    return s;
}
void FxModule::setState(const IDataNode& /*state*/) {}

const IDataNode& FxModule::getConfiguration() { return *m_config; }

std::unique_ptr<IDataNode> FxModule::getHealthStatus() {
    auto h = std::make_unique<JsonDataNode>("health");
    h->setInt("alive", static_cast<int>(m_world.aliveCount()));
    h->setInt("spawned", static_cast<int>(m_spawned));
    return h;
}

} // namespace grove
