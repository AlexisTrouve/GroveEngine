#pragma once

/**
 * grove::entity::EntityWorld — pure entity / component / behavior core (header-only, std-only,
 * NO IIO / renderer / SDL). The data-driven "scene/entity" layer (multi-project authoring surface).
 *
 * WHAT : an entity = a stable id + a bag of typed COMPONENTS (Transform, Sprite) + a list of
 *        data-parameterized BEHAVIORS drawn from a FIXED engine library (move / spin / lifetime).
 *        `tick(dt)` advances every behavior (mutating components; Lifetime kills its entity on expiry).
 *        `diffRender()` emits the minimal retained-render ops (Add / Update / Remove) versus the last
 *        emitted snapshot, keyed by entity id (= the renderer's `renderId`).
 *
 * WHY  : behavior must be practical ACROSS PROJECTS — so the behavior library + the render diff live
 *        engine-side and every game inherits them for free, WITHOUT a scripting language (a behavior is a
 *        fixed enum of primitives with numeric params, exactly like the VN conditions — declarative data,
 *        not a parsed expression). Kept pure + headless-testable (like grove::anim / DialogueRuntime): the
 *        EntityModule wraps this and turns the RenderOps into render:sprite:* IIO. Bespoke game logic stays
 *        consumer-side (mutate components); the engine owns the reusable common behaviors + the diff.
 *
 * HOW  : entities live in an ordered map (deterministic tick / diff order). tick() ticks each behavior in
 *        list order. diffRender() compares each alive sprite-bearing entity's render-relevant state to a
 *        cached snapshot -> Add (new id) / Update (changed) / Remove (id gone or sprite dropped), then GCs
 *        dead entities. So the consumer sends the minimal retained-render traffic — new primitives already
 *        speak the render:sprite:add/update/remove contract.
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace grove {
namespace entity {

using EntityId = uint32_t;   // stable, monotonic (never reused) -> a destroyed id can't alias a live sprite

// ---- Components (plain data) --------------------------------------------------------------------

// Position/orientation. cx,cy = CENTER (the render anchor convention — docs/design/render-anchor-convention.md).
struct Transform {
    float cx = 0.0f, cy = 0.0f;
    float rotation = 0.0f;             // radians, pivots at the box centre
    float scaleX = 1.0f, scaleY = 1.0f;
};

// What (if anything) this entity draws. `present` = false -> a logic/transform-only entity (never rendered).
struct Sprite {
    bool present = false;
    std::string asset;                 // streamed asset id ("" -> use textureId)
    int textureId = 0;
    uint32_t color = 0xFFFFFFFFu;      // 0xRRGGBBAA
    int layer = 0;
};

// ---- Behavior: a FIXED, engine-provided, data-parameterized primitive (NOT a script) ------------
// The multi-project library. Add a new reusable behavior = add a Type + a tick case here (every project
// inherits it). Params live in a,b; `age` is per-behavior state. Use the factory helpers for readable sites.
struct Behavior {
    enum class Type { Move, Spin, Lifetime };
    Type type;
    float a = 0.0f;   // Move: vx      | Spin: degPerSec | Lifetime: seconds
    float b = 0.0f;   // Move: vy      | (unused)        | (unused)
    float age = 0.0f; // Lifetime: elapsed seconds
};
inline Behavior move(float vx, float vy) { return Behavior{Behavior::Type::Move, vx, vy, 0.0f}; }
inline Behavior spin(float degPerSec)    { return Behavior{Behavior::Type::Spin, degPerSec, 0.0f, 0.0f}; }
inline Behavior lifetime(float seconds)  { return Behavior{Behavior::Type::Lifetime, seconds, 0.0f, 0.0f}; }

struct Entity {
    EntityId id = 0;
    bool alive = true;
    Transform transform;
    Sprite sprite;
    std::vector<Behavior> behaviors;
};

class EntityWorld {
public:
    // Spawn an empty entity (transform at origin, no sprite, no behaviors); returns its id.
    EntityId spawn() {
        const EntityId id = ++m_nextId;
        Entity& e = m_entities[id];
        e.id = id;
        return id;
    }

    // Mark an entity dead. It stops ticking immediately; diffRender() emits its Remove then GCs it.
    void destroy(EntityId id) {
        if (Entity* e = get(id)) e->alive = false;
    }

    Entity* get(EntityId id) {
        auto it = m_entities.find(id);
        return it == m_entities.end() ? nullptr : &it->second;
    }

    void setTransform(EntityId id, const Transform& t) { if (Entity* e = get(id)) e->transform = t; }
    void setSprite(EntityId id, const Sprite& s)       { if (Entity* e = get(id)) { e->sprite = s; e->sprite.present = true; } }
    void addBehavior(EntityId id, const Behavior& b)   { if (Entity* e = get(id)) e->behaviors.push_back(b); }

    size_t aliveCount() const {
        size_t n = 0;
        for (const auto& kv : m_entities) if (kv.second.alive) ++n;
        return n;
    }

    // Advance every alive entity's behaviors by dt (list order = deterministic). Behaviors mutate their
    // entity's components; Lifetime flips the entity dead on expiry.
    void tick(float dt) {
        for (auto& kv : m_entities) {
            Entity& e = kv.second;
            if (!e.alive) continue;
            for (Behavior& b : e.behaviors) tickBehavior(e, b, dt);
        }
    }

    // One render op the module turns into render:sprite:add / :update / :remove (keyed by id = renderId).
    struct RenderOp {
        enum class Kind { Add, Update, Remove } kind;
        EntityId id;
        Transform transform;   // valid for Add / Update
        Sprite sprite;         // valid for Add / Update
    };

    // The minimal retained-render diff vs the last emitted snapshot. Call ONCE per frame, AFTER tick().
    // Emits Add for a newly-visible entity, Update when its render state changed, Remove when it died or
    // dropped its sprite; then GCs dead entities. Idempotent if nothing changed (returns no ops).
    std::vector<RenderOp> diffRender() {
        std::vector<RenderOp> ops;

        // Add / Update every alive, sprite-bearing entity whose render state differs from the snapshot.
        for (auto& kv : m_entities) {
            Entity& e = kv.second;
            if (!e.alive || !e.sprite.present) continue;
            const RenderState cur{e.transform, e.sprite};
            auto snap = m_snapshot.find(e.id);
            if (snap == m_snapshot.end()) {
                ops.push_back({RenderOp::Kind::Add, e.id, e.transform, e.sprite});
                m_snapshot.emplace(e.id, cur);
            } else if (!(snap->second == cur)) {
                ops.push_back({RenderOp::Kind::Update, e.id, e.transform, e.sprite});
                snap->second = cur;
            }
        }

        // Remove any snapshot id that is no longer an alive, sprite-bearing entity.
        for (auto it = m_snapshot.begin(); it != m_snapshot.end();) {
            Entity* e = get(it->first);
            if (!e || !e->alive || !e->sprite.present) {
                ops.push_back({RenderOp::Kind::Remove, it->first, Transform{}, Sprite{}});
                it = m_snapshot.erase(it);
            } else {
                ++it;
            }
        }

        // GC dead entities now that their Remove has been emitted (their ids are never reused).
        for (auto it = m_entities.begin(); it != m_entities.end();) {
            if (!it->second.alive) it = m_entities.erase(it);
            else ++it;
        }
        return ops;
    }

private:
    // The render-relevant slice of an entity, cached to detect changes between frames.
    struct RenderState {
        Transform t;
        Sprite s;
        bool operator==(const RenderState& o) const {
            return t.cx == o.t.cx && t.cy == o.t.cy && t.rotation == o.t.rotation &&
                   t.scaleX == o.t.scaleX && t.scaleY == o.t.scaleY &&
                   s.asset == o.s.asset && s.textureId == o.s.textureId &&
                   s.color == o.s.color && s.layer == o.s.layer;
        }
    };

    // The fixed behavior library. One pure case per primitive.
    static void tickBehavior(Entity& e, Behavior& b, float dt) {
        switch (b.type) {
            case Behavior::Type::Move:
                e.transform.cx += b.a * dt;
                e.transform.cy += b.b * dt;
                break;
            case Behavior::Type::Spin:
                e.transform.rotation += b.a * dt * 0.01745329252f;   // deg/s -> rad
                break;
            case Behavior::Type::Lifetime:
                b.age += dt;
                if (b.age >= b.a) e.alive = false;
                break;
        }
    }

    EntityId m_nextId = 0;
    std::map<EntityId, Entity> m_entities;        // ordered -> deterministic tick / diff
    std::map<EntityId, RenderState> m_snapshot;   // last-emitted render state per id (drives the diff)
};

} // namespace entity
} // namespace grove
