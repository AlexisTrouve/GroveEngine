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
  `cx,cy` = CENTER; `Sprite{asset|textureId,color,layer,present}`; **`Text{text,color,layer,fontSize,present}`** —
  optional label, string already-localized by the consumer; **`Emitter{prefab,count,speedMin/Max,spreadDeg,dirDeg,oneShot}`** —
  a one-shot particle burst) + `vector<Behavior>`. Sprite & Text are **orthogonal** — an effect may bear either/both.
- **Emitter (F3)** = a one-shot particle **burst**. On its next tick, `fireEmitter` spawns `count` fresh
  instances of the particle `prefab` AT the emitter, each launched with a random velocity in the cone
  `[dirDeg ± spreadDeg/2]` at speed `[speedMin,speedMax]`. Randomness = a **deterministic xorshift PRNG seeded
  by the entity id** (reproducible + unit-testable — no global rand state, respects the engine's determinism
  posture). A one-shot emitter (invisible) self-destructs after firing. Particles are short-lived **sprite**
  entities (ride `render:sprite:*` + the F1 behaviors) — **not** the renderer's `render:particle` primitive;
  sized for VFX bursts (tens), not GPU masses (those → `submitParticleBatch`, see [[rendering-throughput]]).
  Emitters fire AFTER the behavior-tick loop (particles get higher ids → not re-ticked the same frame).
- **`tick(dt)`** advances behaviors in list order (mutate components; `lifetime` kills on expiry). Behavior lib:
  `move/spin/lifetime` (E1) + **`fade{seconds,fromA,toA}`** (ramps the AA byte of sprite AND text color) +
  **`velocity{vx,vy,drag}`** (initial velocity decaying by drag/s; explicit-Euler → tick at frame dt) (F1).
- **`diffRender()`** = the retained-render diff, run as **two independent passes** — a **sprite** pass
  (→ `render:sprite:*`, `cx,cy` CENTER) and a **text** pass (→ `render:text:*`, `x,y` CORNER). Each compares an
  effect's render state to its own cached snapshot → `Add`/`Update`/`Remove`, then GC dead effects. Separate id
  spaces (sprite renderId ≠ text renderId) so one effect with both never collides. Rides the existing
  `render:sprite:*` / `render:text:*` contracts — **zero new renderer code**.
- **Prefabs**: `registerPrefab`/`spawnFromPrefab` — deep copy per instance (fresh state, incl. the `text`
  component); unknown → fail soft.

**Module — `FxModule`** (`modules/FxModule/`, an `IModule`): consumes `fx:prefab/spawn/set/destroy` (nested
JSON; **robust accessors fail soft** — never throw on a malformed payload); each `process(dt)` drains →
`tick(dt)` → `diffRender()` → publishes `render:sprite:*` / `render:text:*` (routed by the op's `Prim`).
**Dual façade**: the `fx:*` topics or the C++ `world()` accessor (a static-link host drives the world directly
via `spawn`/`setSprite`/`setText`/`setEmitter`/`addBehavior`, then `process(dt)`).

## Slices (shipped, TDD — each prove-it-bites)

| Slice | Commit | What |
|-------|--------|------|
| **E1** — `FxWorld` pure core | `fc9e027` | effects + components + behavior lib (`move/spin/lifetime`) + retained-render diff. `FxWorldUnit`. |
| **E2** — `FxModule` | `e0b2c00` | `fx:*` topics + C++ `world()` → `render:sprite:*` (cx,cy). Robust fail-soft parse. `IT_059`. |
| **E3** — archetypes/prefabs | `ffec0b2` | `fx:prefab` + `fx:spawn {archetype}` with per-instance overrides; deep-copy fresh state. `FxWorldUnit [prefab]` + `IT_059c`. |
| rename → `grove::fx`/`FxModule` + VFX re-scope | `9c64901` | the naming-footgun fix + docs re-scoped + anti-crowd panel + roadmap. |
| **F1** — `fade` + `velocity+drag` behaviors | `96e8608` | two lifecycle primitives; fade ramps sprite/text alpha, velocity spreads + decays. `FxWorldUnit [fade]/[velocity]` + `IT_059d`. |
| **F2** — `Text` component + floating-numbers | `96e8608` | `Text` component + own `render:text:*` diff pass; `damage_number` archetype (drifterra #1). `FxWorldUnit [text]` + `IT_059e`. |
| **F3** — `Emitter` component (particle burst) | (this) | one-shot burst → `count` prefab particles in a cone, deterministic-PRNG velocity, self-cleaning; explosion archetype carries the emitter. `FxWorldUnit [emitter]` + `IT_059f`. |

(Commits E1-E3 predate the rename — the code they reference was `grove::entity`/`EntityModule` at the time.)
New **opt-in** module (`GROVE_BUILD_FX_MODULE=OFF` default) + a header — changes **no** existing behavior.

## Follow-ons

- ~~**Behaviors**: `fade`, `velocity+drag`~~ — **shipped (F1)**. Remaining lifecycle ideas stay effect-only
  (NOT follow/path/oscillate — gameplay movement is consumer-owned).
- ~~**Components**: `text` (damage numbers), `particle`~~ — **shipped (F2 text, F3 particle-burst Emitter)**.
- **Continuous (rate-based) emitter** for trails / smoke / streams (F3 is one-shot burst only — covers
  explosions/debris/muzzle-flash). A rate emitter emits N/sec over the emitter's lifetime.
- **Hot-reload** full-world serialization (`getState`/`setState` are minimal — health counter only).
- ~~by-eye windowed VFX demo~~ — **shipped** (`tests/visual/test_fx_demo.cpp`): explosion bursts + rising
  damage numbers rendered through BgfxRenderer. Interactive window (LMB/Space/auto) + a headless `--shot`
  PNG. Verified by eye (bursts spread + fade, numbers rise). Windows/SDL, compiles the module directly.

## Key files

- `include/grove/fx/FxWorld.h` — the pure core (effects/components/behaviors/tick/diff/prefabs).
- `modules/FxModule/FxModule.{h,cpp}` + `CMakeLists.txt` (`FxModule_static`).
- `tests/unit/test_fx_world.cpp` (`FxWorldUnit`), `tests/integration/IT_059_fx_e2e.cpp` (`FxE2E`).
- `tests/visual/test_fx_demo.cpp` — the by-eye windowed demo (explosions + damage numbers; `--shot` PNG).
- Docs: DEVELOPER_GUIDE "Effects / FX Layer" section + CLAUDE.md FxModule entry.
- Memory: [[scene-entity-layer]]. Relates to [[render-anchor-convention]] (Transform uses cx,cy),
  [[rendering-throughput]] (why crowds use the batch path, not this), [[drifterra-consumes-groveengine]].
