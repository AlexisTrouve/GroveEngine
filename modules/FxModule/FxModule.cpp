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
// Merge an emitter component. `prefab` = the particle template. oneShot=true (default) = a one-shot burst of
// `count` particles; oneShot=false = a continuous stream of `ratePerSec` particles/second (trails/smoke).
void mergeEmitter(const nlohmann::json& je, fx::Emitter& em) {
    if (je.contains("prefab")) em.prefab = str(je, "prefab");
    em.count = inum(je, "count", em.count);
    em.ratePerSec = num(je, "ratePerSec", em.ratePerSec);
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

// ---- Hot-reload FULL-STATE serialization (struct <-> json) -----------------------------------------------
// A distinct, VERBOSE wire format from the authoring fx:* topics: it captures EVERY field incl. INTERNAL state
// (a behavior's elapsed `age` + decayed velocity, the emitter's `fired`/accumulator/rngState, each component's
// `present` flag) so a reloaded module resumes effects mid-flight and preserves entity ids (= renderIds). The
// render snapshots are NOT serialized — see FxWorld's serialization note (the next diffRender re-Adds, which is
// idempotent on the renderer). All loaders are fail-soft (robust accessors default; never throw on a bad node).
bool boolOf(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}
nlohmann::json dumpTransform(const fx::Transform& t) {
    return {{"cx", t.cx}, {"cy", t.cy}, {"rot", t.rotation}, {"sx", t.scaleX}, {"sy", t.scaleY}};
}
fx::Transform loadTransform(const nlohmann::json& j) {
    fx::Transform t; t.cx = num(j, "cx", 0); t.cy = num(j, "cy", 0); t.rotation = num(j, "rot", 0);
    t.scaleX = num(j, "sx", 1); t.scaleY = num(j, "sy", 1); return t;
}
nlohmann::json dumpSprite(const fx::Sprite& s) {
    return {{"p", s.present}, {"asset", s.asset}, {"tex", s.textureId},
            {"color", static_cast<int64_t>(s.color)}, {"layer", s.layer}};
}
fx::Sprite loadSprite(const nlohmann::json& j) {
    fx::Sprite s; s.present = boolOf(j, "p"); s.asset = str(j, "asset"); s.textureId = inum(j, "tex", 0);
    s.color = color(j, "color", 0xFFFFFFFFu); s.layer = inum(j, "layer", 0); return s;
}
nlohmann::json dumpText(const fx::Text& t) {
    return {{"p", t.present}, {"text", t.text}, {"color", static_cast<int64_t>(t.color)},
            {"layer", t.layer}, {"fs", t.fontSize}};
}
fx::Text loadText(const nlohmann::json& j) {
    fx::Text t; t.present = boolOf(j, "p"); t.text = str(j, "text"); t.color = color(j, "color", 0xFFFFFFFFu);
    t.layer = inum(j, "layer", 0); t.fontSize = inum(j, "fs", 16); return t;
}
nlohmann::json dumpEmitter(const fx::Emitter& e) {
    return {{"p", e.present}, {"prefab", e.prefab}, {"count", e.count}, {"smin", e.speedMin},
            {"smax", e.speedMax}, {"spread", e.spreadDeg}, {"dir", e.dirDeg}, {"one", e.oneShot},
            {"fired", e.fired}, {"rate", e.ratePerSec}, {"acc", e.accumulator},
            {"rng", static_cast<int64_t>(e.rngState)}};
}
fx::Emitter loadEmitter(const nlohmann::json& j) {
    fx::Emitter e; e.present = boolOf(j, "p"); e.prefab = str(j, "prefab"); e.count = inum(j, "count", 0);
    e.speedMin = num(j, "smin", 0); e.speedMax = num(j, "smax", 0); e.spreadDeg = num(j, "spread", 360);
    e.dirDeg = num(j, "dir", 0); e.oneShot = boolOf(j, "one"); e.fired = boolOf(j, "fired");
    e.ratePerSec = num(j, "rate", 0); e.accumulator = num(j, "acc", 0);
    e.rngState = static_cast<uint32_t>(inum(j, "rng", 0)); return e;
}
// A behavior serializes RAW (type enum + the a/b/c params + the age/vx/vy state) — NOT the authoring form.
nlohmann::json dumpBehavior(const fx::Behavior& b) {
    return {{"t", static_cast<int>(b.type)}, {"a", b.a}, {"b", b.b}, {"c", b.c},
            {"age", b.age}, {"vx", b.vx}, {"vy", b.vy}};
}
fx::Behavior loadBehavior(const nlohmann::json& j) {
    fx::Behavior b; b.type = static_cast<fx::Behavior::Type>(inum(j, "t", 0));
    b.a = num(j, "a", 0); b.b = num(j, "b", 0); b.c = num(j, "c", 0);
    b.age = num(j, "age", 0); b.vx = num(j, "vx", 0); b.vy = num(j, "vy", 0); return b;
}
nlohmann::json dumpBehaviors(const std::vector<fx::Behavior>& v) {
    nlohmann::json a = nlohmann::json::array();
    for (const auto& b : v) a.push_back(dumpBehavior(b));
    return a;
}
std::vector<fx::Behavior> loadBehaviors(const nlohmann::json& j) {
    std::vector<fx::Behavior> v;
    if (j.is_array()) for (const auto& jb : j) v.push_back(loadBehavior(jb));
    return v;
}
nlohmann::json dumpEntity(const fx::Entity& e) {
    return {{"id", static_cast<int64_t>(e.id)}, {"alive", e.alive}, {"t", dumpTransform(e.transform)},
            {"s", dumpSprite(e.sprite)}, {"x", dumpText(e.text)}, {"e", dumpEmitter(e.emitter)},
            {"b", dumpBehaviors(e.behaviors)}};
}
fx::Entity loadEntity(const nlohmann::json& j) {
    fx::Entity e; e.id = static_cast<fx::EntityId>(inum(j, "id", 0)); e.alive = boolOf(j, "alive");
    if (auto it = j.find("t"); it != j.end()) e.transform = loadTransform(*it);
    if (auto it = j.find("s"); it != j.end()) e.sprite = loadSprite(*it);
    if (auto it = j.find("x"); it != j.end()) e.text = loadText(*it);
    if (auto it = j.find("e"); it != j.end()) e.emitter = loadEmitter(*it);
    if (auto it = j.find("b"); it != j.end()) e.behaviors = loadBehaviors(*it);
    return e;
}
nlohmann::json dumpPrefab(const std::string& name, const fx::Prefab& p) {
    return {{"name", name}, {"t", dumpTransform(p.transform)}, {"s", dumpSprite(p.sprite)},
            {"x", dumpText(p.text)}, {"e", dumpEmitter(p.emitter)}, {"b", dumpBehaviors(p.behaviors)}};
}
fx::Prefab loadPrefabState(const nlohmann::json& j) {
    fx::Prefab p;
    if (auto it = j.find("t"); it != j.end()) p.transform = loadTransform(*it);
    if (auto it = j.find("s"); it != j.end()) p.sprite = loadSprite(*it);
    if (auto it = j.find("x"); it != j.end()) p.text = loadText(*it);
    if (auto it = j.find("e"); it != j.end()) p.emitter = loadEmitter(*it);
    if (auto it = j.find("b"); it != j.end()) p.behaviors = loadBehaviors(*it);
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

// Hot-reload state: serialize the FULL live world so a reloaded module resumes seamlessly. Entities are dumped
// VERBATIM (ids = renderIds preserved) with their internal behavior/emitter state; plus the prefab library, the
// string-id -> EntityId name map, and the id counter. NOT serialized: the render snapshots (the next diffRender
// re-Adds every entity, which the renderer applies idempotently) — see FxWorld's serialization note. Preserving
// the ids is what avoids the real hazard: orphaned renderer sprites a state-less reload could never remove.
std::unique_ptr<IDataNode> FxModule::getState() {
    nlohmann::json j;
    j["nextId"]  = static_cast<int64_t>(m_world.peekNextId());
    j["spawned"] = static_cast<int64_t>(m_spawned);

    nlohmann::json names = nlohmann::json::array();
    for (const auto& kv : m_names) names.push_back({{"n", kv.first}, {"id", static_cast<int64_t>(kv.second)}});
    j["names"] = std::move(names);

    nlohmann::json prefabs = nlohmann::json::array();
    for (const auto& kv : m_world.prefabs()) prefabs.push_back(dumpPrefab(kv.first, kv.second));
    j["prefabs"] = std::move(prefabs);

    nlohmann::json ents = nlohmann::json::array();
    for (const auto& kv : m_world.entities()) ents.push_back(dumpEntity(kv.second));
    j["entities"] = std::move(ents);

    return std::make_unique<JsonDataNode>("state", std::move(j));
}

void FxModule::setState(const IDataNode& state) {
    const auto* jn = dynamic_cast<const JsonDataNode*>(&state);
    if (!jn) return;                                  // only our JSON state format is understood (fail soft)
    const nlohmann::json& j = jn->getJsonData();

    m_world = fx::FxWorld{};                           // start from a clean world, then restore into it
    m_names.clear();
    m_spawned = static_cast<uint64_t>(inum(j, "spawned", 0));

    if (auto it = j.find("prefabs"); it != j.end() && it->is_array())
        for (const auto& jp : *it) m_world.registerPrefab(str(jp, "name"), loadPrefabState(jp));

    if (auto it = j.find("entities"); it != j.end() && it->is_array())
        for (const auto& je : *it) m_world.restoreEntity(loadEntity(je));   // verbatim -> ids (renderIds) preserved

    if (auto it = j.find("names"); it != j.end() && it->is_array())
        for (const auto& jn2 : *it) m_names[str(jn2, "n")] = static_cast<fx::EntityId>(inum(jn2, "id", 0));

    // The id counter LAST — after the restores — so future spawn()s never alias a restored renderId.
    m_world.setNextId(static_cast<fx::EntityId>(inum(j, "nextId", 0)));
}

const IDataNode& FxModule::getConfiguration() { return *m_config; }

std::unique_ptr<IDataNode> FxModule::getHealthStatus() {
    auto h = std::make_unique<JsonDataNode>("health");
    h->setInt("alive", static_cast<int>(m_world.aliveCount()));
    h->setInt("spawned", static_cast<int>(m_spawned));
    return h;
}

} // namespace grove
