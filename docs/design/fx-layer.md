# FX / Effects Layer — design + rationale

**What it is.** `grove::fx` + `FxModule`: a data-driven layer for **ephemeral, cosmetic visual effects**
(explosions, debris, engine trails, muzzle flash, warp shimmer, floating damage numbers). Compose an effect
from components + declarative behaviors; the engine ticks them and emits `render:sprite:*`. Unity/Godot-
flavoured (components + prefabs) but declarative — no scripting language.

**What it is NOT.** A gameplay-entity system. See *Alignment* below — this is the load-bearing scope decision.

## How we got here (thesis → the naming trap)

GroveEngine serves AI consumers that must write correct code first-try. The [anchor convention](render-anchor-convention.md)
fixed that at the *field* level (`x,y`=corner / `cx,cy`=center — the name carries the meaning). This layer
was the *macro* version: give an AI a GameObject+Component authoring surface it has a huge training prior on.

Original framing was **"scene/entity layer" / `EntityModule`** — and that name was itself the exact footgun
the whole effort exists to kill: **a name broader than its safe use.** "Entity" promises general gameplay
entities; the safe zone is narrow (ephemeral effects). A future AI reads "entity layer, Unity-style," routes
its gameplay crowd through it, and hits a wall. So we renamed to **`FxModule` / `grove::fx` / `fx:*`** — the
name now carries the scope.

## Alignment with the consumer (drifterra, 2026-07-09) — the load-bearing decision

The initial question to the consumer was binary ("engine owns entities OR you keep yours?"). **That binary
was the trap** — drifterra has two radically different entity natures and the answer differs:

- **Gameplay entities → consumer-owned, non-negotiable.** Fleet/swarm (flow-fields + TTC avoidance +
  formations — the Daedalium product, SoA), ships (grid-component builds with derived stats), colony
  (emergent flow on routes), combat (CombatCore autobattler, SoA). Their movement is **exactly what
  `move{vx,vy}` can't express.** Giving the engine ownership would (1) split the source of truth with the
  products that already own it → sync hell; (2) fight the hard-won crowd-scale arch — the render wall was
  **dispatch, not GPU**, solved by the flat batch blob (`submitSpriteBatch`, memcpy, stride 8); a
  GameObject+Component model is **AoS, one node per entity = the exact opposite**, re-walling at ~3000 agents.
- **Ephemeral cosmetic VFX → the real niche.** The layer's sweet-spot (spawn prefab → `move`+`lifetime`+
  `spin` → dies) *is* effects. drifterra has nothing for it (would hand-roll an effects pool + update loop).
  Here the module removes real boilerplate **without touching any source of truth** (an explosion has no
  "real" position a gameplay system must know).

**Resolution:** the engine offers `FxModule` for VFX; **gameplay entities stay consumer-owned** (SoA + batch
render). The behavior roadmap is **effect-lifecycle** (`lifetime`/`spin` shipped; `fade`, `velocity+drag`
next) — deliberately **not** `follow`/`path`/`oscillate` (gameplay movement, the products' domain, never
routed through the engine).

## Architecture

**Pure core — `grove::fx::FxWorld`** (`include/grove/fx/FxWorld.h`, header-only, std-only, no IIO/renderer —
like `grove::anim` / `DialogueRuntime`):

- **Effect** = stable id (monotonic, never reused) + components (`Transform{cx,cy,rotation,scaleX,scaleY}` —
  `cx,cy` = CENTER; `Sprite{asset|textureId,color,layer,present}`) + `vector<Behavior>`.
- **`tick(dt)`** advances behaviors in list order (mutate components; `lifetime` kills on expiry).
- **`diffRender()`** = the retained-render diff: compare each alive sprite-bearing effect's render state to a
  cached snapshot → `Add`/`Update`/`Remove`, then GC dead effects. Rides the existing
  `render:sprite:add/update/remove` contract — **zero new renderer code**.
- **Prefabs**: `registerPrefab`/`spawnFromPrefab` — deep copy per instance (fresh state); unknown → fail soft.

**Module — `FxModule`** (`modules/FxModule/`, an `IModule`): consumes `fx:prefab/spawn/set/destroy` (nested
JSON; **robust accessors fail soft** — never throw on a malformed payload); each `process(dt)` drains →
`tick(dt)` → `diffRender()` → publishes `render:sprite:*`. **Dual façade**: the `fx:*` topics or the C++
`world()` accessor (a static-link host drives the world directly, then `process(dt)`).

## Slices (shipped, TDD — each prove-it-bites)

| Slice | Commit | What |
|-------|--------|------|
| **E1** — `FxWorld` pure core | `fc9e027` | effects + components + behavior lib (`move/spin/lifetime`) + retained-render diff. `FxWorldUnit`. |
| **E2** — `FxModule` | `e0b2c00` | `fx:*` topics + C++ `world()` → `render:sprite:*` (cx,cy). Robust fail-soft parse. `IT_059`. |
| **E3** — archetypes/prefabs | `ffec0b2` | `fx:prefab` + `fx:spawn {archetype}` with per-instance overrides; deep-copy fresh state. `FxWorldUnit [prefab]` + `IT_059c`. |
| rename → `grove::fx`/`FxModule` + VFX re-scope | (this) | the naming-footgun fix + docs re-scoped + anti-crowd panel + roadmap. |

(Commits E1-E3 predate the rename — the code they reference was `grove::entity`/`EntityModule` at the time.)
New **opt-in** module (`GROVE_BUILD_FX_MODULE=OFF` default) + a header — changes **no** existing behavior.

## Follow-ons

- **Behaviors** (effect-lifecycle only): `fade{fromA,toA}` (alpha over life), `velocity+drag`. NOT
  follow/path/oscillate.
- **Components**: `text` (damage numbers), `particle`.
- **Hot-reload** full-world serialization (`getState`/`setState` are minimal — health counter only).

## Key files

- `include/grove/fx/FxWorld.h` — the pure core (effects/components/behaviors/tick/diff/prefabs).
- `modules/FxModule/FxModule.{h,cpp}` + `CMakeLists.txt` (`FxModule_static`).
- `tests/unit/test_fx_world.cpp` (`FxWorldUnit`), `tests/integration/IT_059_fx_e2e.cpp` (`FxE2E`).
- Docs: DEVELOPER_GUIDE "Effects / FX Layer" section + CLAUDE.md FxModule entry.
- Memory: [[scene-entity-layer]]. Relates to [[render-anchor-convention]] (Transform uses cx,cy),
  [[rendering-throughput]] (why crowds use the batch path, not this), [[drifterra-consumes-groveengine]].
