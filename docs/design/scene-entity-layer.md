# Scene / Entity Layer — design + rationale

**Thesis.** GroveEngine serves AI consumers that write gameplay code against its contract and must get it
**right the first time**. The anchor convention was a *micro*-fix of that contract. This is the *macro*
version: a **data-driven scene/entity layer** (Unity/Godot-style GameObject + Component) so an AI composes
entities/components/behaviors — a shape it has a huge training prior on — instead of hand-managing entities
and emitting raw `render:sprite:*`.

## The decision (direction C)

Three options were on the table:

- **A — thin engine (status quo):** the consumer owns entities + the game loop; the engine is a renderer +
  IIO bus. Max control, zero new engine scope.
- **B — full ECS / GameObject:** entities + component registry + a systems scheduler + lifecycle +
  serialization. Powerful, but a large new subsystem (and EnTT/flecs exist if we ever want real ECS).
- **C — thin data-driven scene layer (CHOSEN):** entities are bags of components + declarative behaviors;
  the engine ticks them and emits the IIO the renderer **already** consumes. Rides the retained-render
  contract + the data-driven philosophy already proven in UI/dialogue.

**C wins**: max AI-prior for min new engine, and it keeps the engine's identity — the engine *presents*
entities, the consumer keeps its bespoke logic. (This is a deliberate, Alexi-approved nudge from "engine"
toward "engine + reusable scene layer", not a full game framework.)

## The behavior decision (Alexi: "le plus pratique pour multi-projet")

The crux (where Unity has MonoBehaviour scripts): behavior must be **practical across projects**. So it can't
live per-consumer (each game re-writes it → no reuse) and it can't be a scripting language (against the
thin-engine identity + the "no expression language" guardrail). The resolution:

> **Behaviors are a FIXED, engine-side library of data-parameterized primitives.** Every project inherits
> them for free. A behavior is an enum of primitives with numeric params — declarative data, like the VN
> conditions, **not** a parsed expression.

Starting library: `move{vx,vy}` · `spin{degPerSec}` · `lifetime{seconds}`. Extending = add a `Type` + a tick
case in `EntityWorld` (all projects gain it). **Prefabs/archetypes** extend the same reuse idea to whole
entity definitions. Bespoke, non-reusable game logic stays **consumer-side** (mutate components).

## Architecture

**Pure core — `grove::entity::EntityWorld`** (`include/grove/entity/EntityWorld.h`, header-only, std-only, no
IIO/renderer — like `grove::anim` / `DialogueRuntime`):

- **Entity** = stable id (monotonic, never reused → a destroyed id can't alias a live sprite) + components
  (`Transform{cx,cy,rotation,scaleX,scaleY}` — `cx,cy` = CENTER, the [anchor convention](render-anchor-convention.md);
  `Sprite{asset|textureId,color,layer,present}`) + `vector<Behavior>`.
- **`tick(dt)`** advances every alive entity's behaviors in list order (they mutate components; `lifetime`
  kills its entity on expiry).
- **`diffRender()`** = the retained-render diff: compare each alive sprite-bearing entity's render state to a
  cached snapshot → `Add` (new id) / `Update` (changed) / `Remove` (died or dropped its sprite), then GC dead
  entities. So the layer emits the **minimal** retained traffic and rides the existing
  `render:sprite:add/update/remove` contract with **zero new renderer code**.
- **Prefabs**: `registerPrefab(name, Prefab)` / `spawnFromPrefab(name)` — a deep copy per instance (fresh
  behavior state); unknown name → 0 (fail soft).

**Module — `EntityModule`** (`modules/EntityModule/`, an `IModule`): wraps the world onto the bus. Consumes
`entity:prefab/spawn/set/destroy` (nested JSON; **robust accessors fail soft** — never throw on a malformed
payload, the AI-consumer thesis applied); each `process(dt)` drains → `tick(dt)` → `diffRender()` → publishes
`render:sprite:*` (renderId = entity id). **Dual façade**: the `entity:*` topics **or** the C++ `world()`
accessor (a static-link host like Drifterra drives the world directly, then `process(dt)` to tick + emit).

## Slices (shipped, TDD — each with prove-it-bites)

| Slice | Commit | What |
|-------|--------|------|
| **E1** — `EntityWorld` pure core | `fc9e027` | entities + components + behavior lib (`move/spin/lifetime`) + retained-render diff. `EntityWorldUnit`. |
| **E2** — `EntityModule` | `e0b2c00` | `entity:*` topics + C++ `world()` API → `render:sprite:*` (cx,cy). Robust fail-soft parse. `IT_059`. |
| **E3** — archetypes/prefabs | `ffec0b2` | `entity:prefab` + `entity:spawn {archetype}` with per-instance overrides; deep-copy fresh state. `EntityWorldUnit [prefab]` + `IT_059c`. |

All local on `master`, **not pushed** as of writing. Full suite 153/153. It's a new **opt-in** module
(`GROVE_BUILD_ENTITY_MODULE=OFF` default) + a header — it changes **no** existing behavior, so pushing it is
non-breaking (unlike the anchor change).

## Open / follow-ons

- **Validate with drifterra** (separate repo, its own Claude): does it WANT the engine to own entities, or
  prefer owning them + emitting IIO itself? Building further before that risks an unused subsystem. The
  behavior library + prefabs are the hook (bullet/enemy/pickup).
- **More behaviors**: `follow{targetId,lerp}` · `oscillate{axis,amp,hz}` · `path{waypoints,speed}`.
- **More components**: `text`, `particle` (the renderer already consumes `render:text` / `render:particle`).
- **Hot-reload full-world serialization** — `getState`/`setState` are minimal (health counter only); a live
  entity world isn't preserved across a module hot-reload yet.

## Key files

- `include/grove/entity/EntityWorld.h` — the pure core (entities/components/behaviors/tick/diff/prefabs).
- `modules/EntityModule/EntityModule.{h,cpp}` + `CMakeLists.txt` (`EntityModule_static`).
- `tests/unit/test_entity_world.cpp` (`EntityWorldUnit`), `tests/integration/IT_059_entity_e2e.cpp` (`EntityE2E`).
- Docs: DEVELOPER_GUIDE "Scene / Entity Layer" section + CLAUDE.md EntityModule entry.
- Memory: [[scene-entity-layer]]. Relates to [[render-anchor-convention]] (Transform uses cx,cy).
