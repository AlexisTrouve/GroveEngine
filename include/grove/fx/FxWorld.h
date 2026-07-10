#pragma once

/**
 * grove::fx::FxWorld — pure entity / component / behavior core (header-only, std-only,
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
 *        FxModule wraps this and turns the RenderOps into render:sprite:* IIO. Bespoke game logic stays
 *        consumer-side (mutate components); the engine owns the reusable common behaviors + the diff.
 *
 * HOW  : entities live in an ordered map (deterministic tick / diff order). tick() ticks each behavior in
 *        list order. diffRender() compares each alive sprite-bearing entity's render-relevant state to a
 *        cached snapshot -> Add (new id) / Update (changed) / Remove (id gone or sprite dropped), then GCs
 *        dead entities. So the consumer sends the minimal retained-render traffic — new primitives already
 *        speak the render:sprite:add/update/remove contract.
 */

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace grove {
namespace fx {

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

// A text label this entity draws (floating damage numbers, callouts). i18n-agnostic: the string is
// ALREADY RESOLVED by the consumer (the engine never localizes). Rendered via render:text (its native
// anchor is the top-left CORNER, so the entity's Transform position maps to the text's x,y — see emitDiff).
struct Text {
    bool present = false;
    std::string text;                  // the pre-resolved string to draw
    uint32_t color = 0xFFFFFFFFu;      // 0xRRGGBBAA (fade ramps the AA byte, like Sprite)
    int layer = 0;
    int fontSize = 16;
};

// A particle emitter — TWO modes:
//   * ONE-SHOT BURST (oneShot=true, F3): on its next tick, spawn `count` particles at once, then self-destruct
//     (explosions / debris / muzzle flash).
//   * CONTINUOUS STREAM (oneShot=false, F4): emit `ratePerSec` particles/second EVERY tick for as long as the
//     entity lives (engine trails / smoke / exhaust). Does NOT self-destruct — the consumer stops it by
//     setting ratePerSec=0 or destroying the entity (already-spawned particles live out their own lifetime, so
//     a trail fades naturally). A moving emitter (a `move` behavior, or fx:set-ing its transform each frame)
//     drops particles at successive positions = a trail. The steady-state particle count is self-bounded by
//     the particle's own lifetime (rate × lifetime), so keep those modest — no artificial cap.
// Both modes spawn a fresh `prefab` instance per particle AT the emitter's position, launched with a random
// velocity: a direction within the cone [dirDeg ± spreadDeg/2] at a speed in [speedMin, speedMax]. The
// particle's look + fade + lifetime live in the referenced PREFAB (author once, reuse) — the emitter only adds
// the launch velocity. Randomness is a DETERMINISTIC per-emitter PRNG (seeded by the entity id; PERSISTED
// across ticks so a continuous stream keeps varying), so emission is reproducible + unit-testable. NOTE:
// particles are short-lived SPRITE entities (they ride render:sprite:*, reusing the retained diff + F1
// behaviors), NOT the renderer's render:particle primitive; for massive GPU counts use submitParticleBatch.
// Sized for VFX (tens of particles), not crowds.
struct Emitter {
    bool present = false;
    std::string prefab;                // the particle template to instantiate per particle (fail-soft if unknown)
    int count = 0;                     // one-shot: how many particles the burst spawns
    float speedMin = 0.0f, speedMax = 0.0f;   // per-particle launch speed range (px/s)
    float spreadDeg = 360.0f;          // full cone width in degrees (360 = omni-directional explosion)
    float dirDeg = 0.0f;               // cone centre direction (degrees; 0 = +x, 90 = +y / screen-down)
    bool oneShot = true;               // true = one-shot burst (self-destructs) ; false = continuous stream
    bool fired = false;                // internal (one-shot): set once the burst has been emitted (don't re-fire)
    // --- continuous-mode fields (appended so F3 aggregate-init sites keep working) ---
    float ratePerSec = 0.0f;           // continuous: particles emitted per second
    float accumulator = 0.0f;          // internal (continuous): fractional-particle carry between ticks
    uint32_t rngState = 0;             // internal: persistent PRNG state (0 = lazily seed from the entity id)
};
// One-shot burst: `count` particles in a cone. (Factory for readable call sites — the struct has many fields.)
inline Emitter burstEmitter(std::string prefab, int count, float speedMin, float speedMax,
                            float spreadDeg = 360.0f, float dirDeg = 0.0f) {
    Emitter e; e.present = true; e.prefab = std::move(prefab); e.count = count;
    e.speedMin = speedMin; e.speedMax = speedMax; e.spreadDeg = spreadDeg; e.dirDeg = dirDeg; e.oneShot = true;
    return e;
}
// Continuous stream: `ratePerSec` particles/second in a cone, until the entity dies (trails / smoke / exhaust).
inline Emitter streamEmitter(std::string prefab, float ratePerSec, float speedMin, float speedMax,
                             float spreadDeg = 360.0f, float dirDeg = 0.0f) {
    Emitter e; e.present = true; e.prefab = std::move(prefab); e.ratePerSec = ratePerSec;
    e.speedMin = speedMin; e.speedMax = speedMax; e.spreadDeg = spreadDeg; e.dirDeg = dirDeg; e.oneShot = false;
    return e;
}

// ---- Behavior: a FIXED, engine-provided, data-parameterized primitive (NOT a script) ------------
// The multi-project library. Add a new reusable behavior = add a Type + a tick case here (every project
// inherits it). Params live in a,b; `age` is per-behavior state. Use the factory helpers for readable sites.
struct Behavior {
    enum class Type { Move, Spin, Lifetime, Fade, Velocity };
    Type type;
    float a = 0.0f;   // Move: vx | Spin: degPerSec | Lifetime/Fade: seconds | Velocity: vx0
    float b = 0.0f;   // Move: vy | Fade: fromAlpha (0..1)                   | Velocity: vy0
    float c = 0.0f;   // Fade: toAlpha (0..1)                               | Velocity: drag (per-second)
    float age = 0.0f; // Lifetime/Fade: elapsed seconds
    float vx = 0.0f, vy = 0.0f;  // Velocity: current velocity (state; decays by drag). Init from a,b.
};
inline Behavior move(float vx, float vy) { return Behavior{Behavior::Type::Move, vx, vy, 0, 0, 0, 0}; }
inline Behavior spin(float degPerSec)    { return Behavior{Behavior::Type::Spin, degPerSec, 0, 0, 0, 0, 0}; }
inline Behavior lifetime(float seconds)  { return Behavior{Behavior::Type::Lifetime, seconds, 0, 0, 0, 0, 0}; }
// Fade the sprite's alpha from `fromA` to `toA` over `seconds` (then hold). Effect fade-out = fade(s,1,0).
inline Behavior fade(float seconds, float fromA = 1.0f, float toA = 0.0f) {
    return Behavior{Behavior::Type::Fade, seconds, fromA, toA, 0, 0, 0};
}
// Move at an initial velocity that DECELERATES by `drag` per second (debris/spark spread). drag 0 = constant.
inline Behavior velocity(float vx0, float vy0, float drag) {
    return Behavior{Behavior::Type::Velocity, vx0, vy0, drag, 0, vx0, vy0};
}

struct Entity {
    EntityId id = 0;
    bool alive = true;
    Transform transform;
    Sprite sprite;
    Text text;                         // optional text label (floating numbers); orthogonal to sprite
    Emitter emitter;                   // optional one-shot particle burst (spawns child particle entities)
    std::vector<Behavior> behaviors;
};

// A PREFAB / archetype: a reusable entity TEMPLATE (default components + behaviors), registered once and
// spawned N times. The biggest multi-project lever — a library of "bullet" / "pickup" / "enemy" definitions
// shared across games. Instances get a FRESH copy (behavior state, e.g. Lifetime.age, starts at 0), then the
// caller applies per-instance overrides on top (setTransform/setSprite/addBehavior).
struct Prefab {
    Transform transform;
    Sprite sprite;
    Text text;                         // optional text label carried by the template (floating-numbers archetype)
    Emitter emitter;                   // optional emitter carried by the template (an "explosion" archetype)
    std::vector<Behavior> behaviors;
};

class FxWorld {
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
    void setText(EntityId id, const Text& tx)          { if (Entity* e = get(id)) { e->text = tx; e->text.present = true; } }
    void setEmitter(EntityId id, const Emitter& em)    { if (Entity* e = get(id)) { e->emitter = em; e->emitter.present = true; } }
    void addBehavior(EntityId id, const Behavior& b)   { if (Entity* e = get(id)) e->behaviors.push_back(b); }

    // --- Prefabs / archetypes (the multi-project entity-template library) ---
    void registerPrefab(const std::string& name, const Prefab& p) { m_prefabs[name] = p; }
    bool hasPrefab(const std::string& name) const { return m_prefabs.find(name) != m_prefabs.end(); }
    // Spawn a fresh instance of a registered prefab (a deep copy of its components + behaviors, so each
    // instance has its own behavior state). Returns 0 if the name is unknown (fail soft — never a bad spawn).
    EntityId spawnFromPrefab(const std::string& name) {
        auto it = m_prefabs.find(name);
        if (it == m_prefabs.end()) return 0;
        const EntityId id = spawn();
        Entity& e = *get(id);
        e.transform = it->second.transform;
        e.sprite    = it->second.sprite;
        e.text      = it->second.text;
        e.emitter   = it->second.emitter;
        e.behaviors = it->second.behaviors;
        return id;
    }

    size_t aliveCount() const {
        size_t n = 0;
        for (const auto& kv : m_entities) if (kv.second.alive) ++n;
        return n;
    }

    // Advance every alive entity's behaviors by dt (list order = deterministic). Behaviors mutate their
    // entity's components; Lifetime flips the entity dead on expiry. Pending emitters fire AFTER the loop
    // (so the particles they spawn — which get higher ids — aren't ticked again this same frame; their first
    // move is next frame, matching the "first render after one tick" model).
    void tick(float dt) {
        std::vector<EntityId> toEmit;
        for (auto& kv : m_entities) {
            Entity& e = kv.second;
            if (!e.alive) continue;
            for (Behavior& b : e.behaviors) tickBehavior(e, b, dt);
            // A one-shot emitter fires once (until fired); a continuous emitter emits every tick.
            if (e.emitter.present && (!e.emitter.oneShot || !e.emitter.fired)) toEmit.push_back(e.id);
        }
        for (EntityId id : toEmit) emitFromEmitter(id, dt);
    }

    // One render op the module turns into render:<prim>:add / :update / :remove (keyed by id = renderId).
    // `prim` picks the retained pool: Sprite -> render:sprite:*, Text -> render:text:* (separate id spaces,
    // so one entity can carry BOTH a sprite and a text label without a renderId collision).
    struct RenderOp {
        enum class Kind { Add, Update, Remove } kind;
        enum class Prim { Sprite, Text } prim = Prim::Sprite;
        EntityId id;
        Transform transform;   // valid for Add / Update
        Sprite sprite;         // valid for Add / Update when prim == Sprite
        Text text;             // valid for Add / Update when prim == Text
    };

    // The minimal retained-render diff vs the last emitted snapshot. Call ONCE per frame, AFTER tick().
    // Emits Add for a newly-visible primitive, Update when its render state changed, Remove when the entity
    // died or dropped that component; then GCs dead entities. Idempotent if nothing changed (no ops).
    // Two independent passes: sprite (render:sprite:*) and text (render:text:*).
    std::vector<RenderOp> diffRender() {
        std::vector<RenderOp> ops;
        diffSprites(ops);
        diffTexts(ops);

        // GC dead entities now that their Removes have been emitted (their ids are never reused).
        for (auto it = m_entities.begin(); it != m_entities.end();) {
            if (!it->second.alive) it = m_entities.erase(it);
            else ++it;
        }
        return ops;
    }

private:
    // The render-relevant slice of a sprite-bearing entity, cached to detect changes between frames.
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
    // The render-relevant slice of a text-bearing entity (position + the text component).
    struct TextState {
        Transform t;
        Text tx;
        bool operator==(const TextState& o) const {
            return t.cx == o.t.cx && t.cy == o.t.cy &&
                   tx.text == o.tx.text && tx.color == o.tx.color &&
                   tx.layer == o.tx.layer && tx.fontSize == o.tx.fontSize;
        }
    };

    // Sprite diff pass: Add/Update/Remove for the render:sprite:* pool.
    void diffSprites(std::vector<RenderOp>& ops) {
        for (auto& kv : m_entities) {
            Entity& e = kv.second;
            if (!e.alive || !e.sprite.present) continue;
            const RenderState cur{e.transform, e.sprite};
            auto snap = m_snapshot.find(e.id);
            if (snap == m_snapshot.end()) {
                ops.push_back({RenderOp::Kind::Add, RenderOp::Prim::Sprite, e.id, e.transform, e.sprite, {}});
                m_snapshot.emplace(e.id, cur);
            } else if (!(snap->second == cur)) {
                ops.push_back({RenderOp::Kind::Update, RenderOp::Prim::Sprite, e.id, e.transform, e.sprite, {}});
                snap->second = cur;
            }
        }
        for (auto it = m_snapshot.begin(); it != m_snapshot.end();) {
            Entity* e = get(it->first);
            if (!e || !e->alive || !e->sprite.present) {
                ops.push_back({RenderOp::Kind::Remove, RenderOp::Prim::Sprite, it->first, Transform{}, Sprite{}, {}});
                it = m_snapshot.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Text diff pass: Add/Update/Remove for the render:text:* pool (mirrors the sprite pass).
    void diffTexts(std::vector<RenderOp>& ops) {
        for (auto& kv : m_entities) {
            Entity& e = kv.second;
            if (!e.alive || !e.text.present) continue;
            const TextState cur{e.transform, e.text};
            auto snap = m_textSnapshot.find(e.id);
            if (snap == m_textSnapshot.end()) {
                ops.push_back({RenderOp::Kind::Add, RenderOp::Prim::Text, e.id, e.transform, {}, e.text});
                m_textSnapshot.emplace(e.id, cur);
            } else if (!(snap->second == cur)) {
                ops.push_back({RenderOp::Kind::Update, RenderOp::Prim::Text, e.id, e.transform, {}, e.text});
                snap->second = cur;
            }
        }
        for (auto it = m_textSnapshot.begin(); it != m_textSnapshot.end();) {
            Entity* e = get(it->first);
            if (!e || !e->alive || !e->text.present) {
                ops.push_back({RenderOp::Kind::Remove, RenderOp::Prim::Text, it->first, Transform{}, {}, Text{}});
                it = m_textSnapshot.erase(it);
            } else {
                ++it;
            }
        }
    }

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
            case Behavior::Type::Fade: {
                // Ramp the ALPHA (the AA byte of 0xRRGGBBAA) from fromA to toA over `seconds`, then hold.
                // Applies to WHICHEVER visual component is present — sprite AND/OR text (floating numbers
                // fade out the same way). Ramping an absent component is harmless (it's never emitted).
                b.age += dt;
                const float t = b.a > 0.0f ? (b.age / b.a) : 1.0f;
                const float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                float alpha = b.b + (b.c - b.b) * tc;                     // lerp fromAlpha -> toAlpha
                alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
                const uint32_t a8 = static_cast<uint32_t>(alpha * 255.0f + 0.5f);
                e.sprite.color = (e.sprite.color & 0xFFFFFF00u) | a8;
                e.text.color   = (e.text.color   & 0xFFFFFF00u) | a8;
                break;
            }
            case Behavior::Type::Velocity: {
                // Move by the current velocity, then decay it by `drag` per second (spread that slows: debris/sparks).
                e.transform.cx += b.vx * dt;
                e.transform.cy += b.vy * dt;
                float damp = 1.0f - b.c * dt;
                if (damp < 0.0f) damp = 0.0f;
                b.vx *= damp;
                b.vy *= damp;
                break;
            }
        }
    }

    // xorshift32 -> [0,1). Deterministic + self-contained (no global rand state), so a burst is reproducible
    // and unit-testable. Advances `state` (must be non-zero — the caller seeds it | 1).
    static float rand01(uint32_t& state) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<float>(state & 0xFFFFFFu) / static_cast<float>(0x1000000);   // 24-bit -> [0,1)
    }

    // Emit particles from an emitter for this tick. ONE-SHOT: spawn `count` at once, mark fired, then the
    // (invisible) emitter retires. CONTINUOUS: accumulate ratePerSec*dt and spawn the whole-particle part this
    // tick (the fraction carries to the next), forever — the emitter keeps living. Each particle is a fresh
    // `prefab` instance AT the emitter's position, launched with a deterministic-random velocity in the cone
    // [dirDeg ± spreadDeg/2] at a speed in [speedMin, speedMax]. Unknown prefab => fail-soft no-op.
    // (std::map insert is reference-stable, so `cfg`/`em` stay valid across the spawnFromPrefab calls.)
    void emitFromEmitter(EntityId id, float dt) {
        Entity* em = get(id);
        if (!em) return;
        Emitter& cfg = em->emitter;

        int toSpawn = 0;
        if (cfg.oneShot) {
            toSpawn = cfg.count;
            cfg.fired = true;                     // set BEFORE spawning -> a self-referential prefab can't loop
        } else {
            cfg.accumulator += cfg.ratePerSec * dt;             // fractional-particle carry across ticks
            toSpawn = static_cast<int>(cfg.accumulator);        // whole particles due this tick
            cfg.accumulator -= static_cast<float>(toSpawn);
        }

        if (toSpawn > 0 && hasPrefab(cfg.prefab)) {
            if (cfg.rngState == 0) cfg.rngState = (id * 2654435761u) | 1u;   // lazy deterministic seed (non-zero)
            uint32_t rng = cfg.rngState;                        // advance a local, persist below (continuous keeps varying)
            const Transform origin = em->transform;             // particles start where the emitter is NOW (moving = trail)
            const std::string prefab = cfg.prefab;
            const float dir  = cfg.dirDeg * 0.01745329252f;     // deg -> rad
            const float half = cfg.spreadDeg * 0.5f * 0.01745329252f;
            const float sMin = cfg.speedMin, sSpan = cfg.speedMax - cfg.speedMin;
            for (int i = 0; i < toSpawn; ++i) {
                const float a     = dir + (rand01(rng) * 2.0f - 1.0f) * half;          // angle in the cone
                const float speed = sMin + rand01(rng) * sSpan;
                const EntityId p = spawnFromPrefab(prefab);
                if (!p) continue;                                                     // fail-soft
                setTransform(p, origin);
                addBehavior(p, velocity(speed * std::cos(a), speed * std::sin(a), 0.0f));
            }
            cfg.rngState = rng;                                 // persist the advanced PRNG for the next tick
        }

        if (cfg.oneShot) em->alive = false;                     // retire the spent one-shot emitter
    }

    EntityId m_nextId = 0;
    std::map<EntityId, Entity> m_entities;          // ordered -> deterministic tick / diff
    std::map<EntityId, RenderState> m_snapshot;     // last-emitted sprite render state per id
    std::map<EntityId, TextState> m_textSnapshot;   // last-emitted text render state per id
    std::map<std::string, Prefab> m_prefabs;        // the archetype/prefab library (spawn templates)
};

} // namespace fx
} // namespace grove
