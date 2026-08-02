# GroveEngine - Developer Guide

**Comprehensive guide for building applications with GroveEngine**

⚠️ **IMPORTANT**: GroveEngine is currently in **development stage** - suitable for prototyping and experimentation, **not production games**. The engine is non-deterministic and optimized for rapid iteration, not stability. See [Current Limitations](#current-limitations) below.

## Table of Contents

1. [Current Limitations](#current-limitations)
2. [Getting Started](#getting-started)
3. [Core System](#core-system)
4. [Available Modules](#available-modules)
   - [BgfxRenderer - 2D Rendering](#bgfxrenderer---2d-rendering)
   - [UIModule - User Interface](#uimodule---user-interface)
   - [InputModule - Input Handling](#inputmodule---input-handling)
5. [IIO Topics Reference](#iio-topics-reference)
6. [Complete Application Example](#complete-application-example)
7. [Building Your First Game](#building-your-first-game)
8. [Advanced Topics](#advanced-topics)

---

## Current Limitations

⚠️ **GroveEngine is EXPERIMENTAL and NOT production-ready.** Understand these limitations before building with it:

### Non-Deterministic Execution
- **Module execution order is NOT guaranteed** - modules may run in different orders between frames
- **Not suitable for networked games** - no deterministic replay or synchronization
- **Race conditions possible** in the experimental ThreadedModuleSystem (Phase 2 ✅); the SequentialModuleSystem path stays single-threaded + deterministic

### Development Stage
- **Optimized for rapid iteration**, not stability
- **No error recovery** - crashes are not handled gracefully
- **Limited performance optimizations** - no profiling, memory pooling, or SIMD
- **ThreadedModuleSystem** ✅ shipped (Phase 2 — one thread per module); ThreadPool + Cluster module systems still planned

### Module Limitations
- **InputModule**: Mouse and keyboard only (gamepad Phase 2 not implemented)
- **BgfxRenderer**: 8x8 bitmap font (UTF-8 decoded; ASCII + French Latin-1 accents é è à ç ô…; uppercase accents alias to the base letter — no room in 8x8; œ/æ ligatures + crisp TTF atlas not yet)
- **UIModule**: Functional but no advanced layout constraints

### What GroveEngine IS Good For
✅ **Rapid prototyping** - 0.4ms hot-reload for instant iteration
✅ **Learning modular architecture** - clean interface-based design
✅ **AI-assisted development** - subsystem-granular modules optimized for Claude Code
✅ **Experimentation** - test game ideas quickly

### Production Roadmap
To make GroveEngine production-ready, the following is needed:
- Deterministic execution guarantees
- Error recovery and graceful degradation
- Higher-performance module systems (ThreadPool, Cluster — ThreadedModuleSystem ✅ shipped)
- Performance profiling and optimization
- Network IO and distributed messaging
- Complete gamepad support
- Advanced text rendering

---

## Getting Started

### Prerequisites

- **C++17** compiler (GCC, Clang, or MSVC)
- **CMake** 3.20+
- **Git** for dependency management

### Quick Start

```bash
# Clone GroveEngine
git clone <grove-engine-repo> GroveEngine
cd GroveEngine

# Build with all modules
cmake -B build -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_INPUT_MODULE=ON
cmake --build build -j4

# Run tests
cd build && ctest --output-on-failure
```

### Debug vs Shipping build (`GROVE_DEBUG`)

Debug and prod are **one engine, two builds** — not two engine classes. `DebugEngine` *is* the
engine (its threaded/pool module hosting, authoritative clock, asset streaming and save/load are
the production core). A single CMake flag, `GROVE_DEBUG` (default **ON**), gates the engine's
**debug skin** and compiles it out of a lean shipping build:

```bash
# Normal dev build — full introspection + verbose per-frame logging
cmake -B build && cmake --build build -j4

# Shipping build — debug skin stripped, zero introspection cost, identical core
cmake -B build-shipping -DGROVE_DEBUG=OFF && cmake --build build-shipping -j4
```

- **Stripped when `GROVE_DEBUG=OFF`**: `step()`'s per-frame logging + frame-timing;
  `DebugEngine::getDetailedStatus()` (returns a minimal marker node); `dumpModuleState` /
  `dumpAllModulesState` / `stepSingleFrame` (become no-ops — the symbols stay so callers link).
- **Always present** (both builds): the whole prod core, error logging, `saveState`/`loadState`,
  and engine **control** — `pauseExecution` / `resumeExecution` / `isPaused` (a pause menu is a
  real shipping feature, not introspection).
- In your own code you can gate debug-only work the same way: include `<grove/BuildConfig.h>` and
  use `if constexpr (grove::kDebugBuild)` or wrap a whole statement in `GROVE_DEBUG_ONLY(...)`
  (it vanishes entirely — arguments not even evaluated — in a shipping build).

The `EngineType::PRODUCTION` / `HIGH_PERFORMANCE` factory types were never implemented and remain
stubs (`EngineFactory` throws for them) — the debug/prod axis is this build flag, not an engine
type. Full rationale + roadmap: [engine-debug-prod-plan.md](design/engine-debug-prod-plan.md).

### Documentation Structure

- **[USER_GUIDE.md](USER_GUIDE.md)** - Module system basics, hot-reload, IIO communication
- **[BgfxRenderer README](../modules/BgfxRenderer/README.md)** - 2D rendering module details
- **[InputModule README](../modules/InputModule/README.md)** - Input handling details
- **This document** - Complete integration guide and examples

---

## Core System

### Architecture Overview

GroveEngine uses a **module-based architecture** with hot-reload support:

```
┌─────────────────────────────────────────────────────────────┐
│                    Your Application                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │  Input   │  │   UI     │  │ Renderer │  │  Game    │   │
│  │  Module  │  │  Module  │  │  Module  │  │  Logic   │   │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘   │
│       └─────────────┼─────────────┼─────────────┘          │
│                     │  IIO Pub/Sub System                   │
└─────────────────────┼───────────────────────────────────────┘
                      │
                IntraIOManager
```

### Key Concepts

| Component | Purpose | Documentation |
|-----------|---------|---------------|
| **IModule** | Module interface | [USER_GUIDE.md](USER_GUIDE.md#imodule) |
| **IIO** | Pull-based pub/sub with callback dispatch | [USER_GUIDE.md](USER_GUIDE.md#iio) |
| **IDataNode** | Configuration & data | [USER_GUIDE.md](USER_GUIDE.md#idatanode) |
| **ModuleLoader** | Hot-reload system | [USER_GUIDE.md](USER_GUIDE.md#moduleloader) |

#### IIO Callback Dispatch Pattern

GroveEngine uses a **pull-based callback dispatch** pattern for message processing:

```cpp
// OLD API (deprecated):
// io->subscribe("topic:pattern");
// while (io->hasMessages()) {
//     auto msg = io->pullMessage();
//     if (msg.topic == "topic:pattern") { /* handle */ }
// }

// NEW API (callback-based):
io->subscribe("topic:pattern", [this](const Message& msg) {
    // Handle message - no if-forest needed
});

while (io->hasMessages()) {
    io->pullAndDispatch();  // Callbacks invoked automatically
}
```

**Key advantages:**
- **No if-forest dispatch**: Register handlers at subscription, not in process loop
- **Module controls WHEN**: Pull-based processing for deterministic ordering
- **Callbacks handle HOW**: Clean separation of concerns
- **Thread-safe**: Callbacks invoked in module's thread context

#### The message payload — shared, immutable, `const`

A handler receives the payload as **`msg.data`**, typed `std::shared_ptr<const IDataNode>`. The bus
delivers **one immutable node shared by pointer across every subscriber** (zero-copy delivery) — so
the payload is `const` on purpose: no subscriber can mutate what another is reading.

**Reading it** — use the `const` accessors (this is what almost every handler does):

```cpp
io->subscribe("ui:action", [this](const grove::Message& msg) {
    std::string action = msg.data->getString("action", "");   // const getters: getString/getInt/
    int button         = msg.data->getInt("button", 0);        // getDouble/getBool/hasProperty...
    // Need the raw json (arrays, nested objects)? cast to const and use the const getJsonData():
    if (auto* jn = dynamic_cast<const grove::JsonDataNode*>(msg.data.get())) {
        const auto& j = jn->getJsonData();                     // const ref — read only
    }
});
```

**Do NOT** mutate the payload or call destructive/lazy-materializing methods on it (`getChild`,
`setX`, ...): they don't compile on a `const` node, and on a shared node they would corrupt the
data other subscribers see. Read the json directly instead (as above).

**Lifetime** — the payload lives only as long as the `Message`, i.e. the duration of your handler.
To keep it **after** the handler returns, copy the `shared_ptr` (a ref-count bump, not a json copy):

```cpp
std::shared_ptr<const grove::IDataNode> kept;   // a member, say
io->subscribe("game:state", [&](const grove::Message& msg) { kept = msg.data; });  // extends its life
```

**Performance** — the bus never copies the payload per subscriber. A module registered with
`registerStaticModule` (the normal static/linked-in host, e.g. a game built on the engine) publishes
with **zero json copies** (its node is shared directly); a hot-loaded `.so` module's payload is
re-homed into one core node on publish (one copy, for cross-`.so` safety). Either way fan-out to N
subscribers is `O(1)` copies, not `O(N)`. See `docs/design/rendering-throughput-handoff.md` for numbers.

---

## Available Modules

### BgfxRenderer - 2D Rendering

**Status:** ✅ Development Ready (Phase 8 complete) | ⚠️ Non-deterministic, experimental

Multi-backend 2D renderer using bgfx (DirectX 11/12, OpenGL, Vulkan, Metal).

#### Features

- Sprite rendering with batching
- Text rendering with bitmap fonts
- Tilemap support
- Particle effects
- Debug shapes (lines, rectangles)
- Layer-based Z-ordering
- Multi-texture batching
- Headless mode for testing

#### Configuration

```cpp
JsonDataNode config("config");
config.setInt("windowWidth", 1920);
config.setInt("windowHeight", 1080);
config.setString("backend", "auto");  // auto, opengl, vulkan, dx11, dx12, metal, noop
config.setString("shaderPath", "./shaders");
config.setBool("vsync", true);
config.setInt("maxSpritesPerBatch", 10000);
config.setInt("nativeWindowHandle", (int)(intptr_t)hwnd);  // Platform window handle

renderer->setConfiguration(config, rendererIO.get(), nullptr);
```

#### Rendering a Sprite

```cpp
// Publish sprite to render
auto sprite = std::make_unique<JsonDataNode>("sprite");
sprite->setDouble("cx", 100.0);   // cx,cy = CENTER (anchor convention)
sprite->setDouble("cy", 200.0);
sprite->setDouble("scaleX", 1.0);
sprite->setDouble("scaleY", 1.0);
sprite->setDouble("rotation", 0.0);        // Radians
sprite->setInt("color", 0xFFFFFFFF);       // RGBA
sprite->setInt("textureId", playerTexture);
sprite->setInt("layer", 10);               // Z-order (higher = front)
io->publish("render:sprite", std::move(sprite));
```

#### Sprite transforms — rotation & mirror

A sprite carries three optional transforms. Omit them and the sprite renders exactly as before.

| Field | Type | Default | Effect |
|---|---|---|---|
| `rotation` | radians | `0` | rotate around the sprite's **own centre** — i.e. `(cx, cy)` |
| `flipX` | bool | `false` | mirror horizontally |
| `flipY` | bool | `false` | mirror vertically |

There is **no separate pivot**: the pivot IS `(cx, cy)`, the anchor the sprite already provides.

Application order is **flip → rotate → scale → translate**. The mirror lives in texture space, so the
image is mirrored inside its quad first, and the quad then turns — which is what you want when a
mirrored limb also has to swing.

```cpp
// A paper-doll piece: an arm that swings, on a character facing left.
auto arm = std::make_unique<JsonDataNode>("sprite");
arm->setDouble("cx", charX + 6.0);       // cx,cy = CENTER, and also the rotation pivot
arm->setDouble("cy", charY - 10.0);
arm->setDouble("scaleX", 1.0);
arm->setDouble("scaleY", 1.0);
arm->setDouble("rotation", swingAngle);  // radians; pivots around (cx, cy)
arm->setBool("flipX", facingLeft);       // mirror the piece, angle unaffected
arm->setString("asset", "hero/arm");     // atlas-aware: the mirror stays inside the sub-rect
arm->setInt("layer", 12);                // draw order between the body and the tool
io->publish("render:sprite", std::move(arm));
```

Flips are honoured on **all three** sprite topics — `render:sprite`, `:add` **and `:update`** — so a
retained character can turn around without being re-published from scratch. On `:update` the flip is
**absolute state, not a toggle**: publish `flipX: true` every frame while your character walks left
and it stays mirrored; omit it and it faces right again. Publish your state, not your transitions.

**One limit worth knowing before you build on this:** a sprite with **`textureId: 0`** is a flat
tinted quad — there is no image to mirror, so a flip on it is a visual no-op. Textured sprites
(including `asset` ids) mirror as expected.

> ⚠️ **Until 2026-07-31 the flip did nothing on `asset`-backed sprites**, and this guide told you to
> re-publish ephemerally each frame to work around a `:update` limitation that no longer exists.
> Both are fixed. If your game mirrors with a **negative `scaleX`** because of it, know that a
> negative scale also reverses the apparent direction of `rotation` — switching to `flipX` means
> un-negating whatever angle you compensated with. Design notes: `docs/design/sprite-transforms.md`.

#### Additive sprites — glowing, stretched quads (`blend`)

`render:sprite` and `render:sprite:add` take an optional **`blend`**:

| Value | Effect |
|-------|--------|
| absent, or `"alpha"` | the historical behaviour, **bit for bit** |
| `"additive"` | the same quad, blended ADDITIVELY — overlapping sprites BRIGHTEN |

Everything else is unchanged: `cx,cy`, `scaleX/scaleY`, `rotation`, `color` tint, `textureId`/`asset`
and `layer` all behave exactly as before.

```cpp
// An engine plume: stretched, rotated, glowing.
auto s = std::make_unique<JsonDataNode>("d");
s->setDouble("cx", nozzleX); s->setDouble("cy", nozzleY);
s->setDouble("scaleX", 260.0); s->setDouble("scaleY", 26.0);   // stretched along the jet
s->setDouble("rotation", heading);
s->setString("blend", "additive");
s->setString("asset", "fx/plume_gradient");
io->publish("render:sprite", std::move(s));
```

**Why this exists.** Before it, a glowing *stretched* quad was impossible: `render:sprite` could
stretch and rotate a texture but only in alpha, and `render:particle` was additive but a square
billboard. Neither could draw the one shape an engine plume needs.

**Batching**: a batch carries one render state, so a blend change **splits the batch** exactly as a
texture or clip change does. Sprites sharing a blend still batch together — grouping your additive
sprites on their own layer keeps the draw count down.

An unknown value falls back to alpha rather than to something surprising.

#### 2D lighting — ambient + radial lights

Two topics. The scene renders into an offscreen target, lights accumulate into a second one, and a
full-screen pass composites them:

```
final.rgb = scene.rgb × (ambient.rgb + lightAccum.rgb)
```

| Topic | Payload | Notes |
|-------|---------|-------|
| `render:ambient` | `{color}` | the global term — **the on/off switch for the whole feature** |
| `render:light` | `{cx, cy, radius, color, intensity?, dirDeg?, spreadDeg?}` | one light, **ephemeral** (re-publish each frame). Omni by default; a cone with `spreadDeg` |
| `render:bloom` | `{intensity, threshold?, radius?}` | post-processing glow, **persistent**. `intensity 0` = off (the default); needs lighting active |

##### ⚠️ Nothing is lit until you publish an ambient

With no `render:ambient`, lighting is **completely off**: no offscreen targets are created, no view
is redirected, the composite records nothing, and the frame is byte-identical to an engine built
without lighting. Publishing `render:light` alone changes nothing — the lights accumulate into a
buffer that is never composited.

This is deliberate: every game that does not light anything must keep paying exactly what it paid
before. It is also the first thing to check when "the lights don't work".

##### You do NOT need a dark game to use lights

The reflex is to think lighting means a night scene. It does not. Publish a **white ambient** and the
scene comes out unchanged (`scene × 1.0`) while lights **add** on top:

```cpp
// Scene looks exactly as it does today; lights only ever brighten.
auto amb = std::make_unique<JsonDataNode>("d");
amb->setInt("color", static_cast<int>(0xFFFFFFFFu));
io->publish("render:ambient", std::move(amb));
```

A dimmer ambient (`0x2A3040FF`) gives night, and the lights carve it back out. Same machinery.

##### A light, every frame

```cpp
// Thruster glow: republished each frame at the nozzle, brightness driven by throttle.
auto l = std::make_unique<JsonDataNode>("d");
l->setDouble("cx", nozzleX);          // cx,cy = CENTRE (anchor convention)
l->setDouble("cy", nozzleY);
l->setDouble("radius", 90.0);         // WORLD units — it zooms with the camera
l->setInt("color", static_cast<int>(0xFFC070FFu));
l->setDouble("intensity", 0.5 + 2.0 * throttle);
io->publish("render:light", std::move(l));
```

- **Ephemeral, like `render:sprite`/`render:particle`.** A light not re-published this frame is gone.
  That is what a moving lamp wants; keeping it would smear it across the scene.
- **`radius` is in world units**, so a light zooms with everything else.
- The colour's **alpha byte is ignored** — a light adds, it does not blend. Brightness is `intensity`.
- **`intensity` above 1 is legal and useful.** The accumulation target is RGBA16F, so overlapping
  lights overshoot 1.0 instead of clipping. That headroom is what the **bloom** feeds on (see below);
  in RGBA8 a bright core would flatten to featureless white — and a threshold above 1 would be
  unreachable, so the default bloom setting would never glow.
- The falloff is `(1 − d/r)²`, reaching **exactly** zero at `radius`.

##### Cone lights (spotlights, thrusters)

Add `dirDeg` + `spreadDeg` and the light becomes a cone. **Both are optional and `spreadDeg`
defaults to 360 — a light that says nothing about a cone is a full disc**, exactly as before cones
existed.

```cpp
l->setDouble("dirDeg", 90.0);      // axis: 0 = +x, 90 = +y (screen-DOWN)
l->setDouble("spreadDeg", 45.0);   // FULL width, so 22.5 either side of the axis
```

The convention is **degrees, borrowed from `grove::fx::Emitter`** - not from `render:sector`'s
`a0`/`a1` radians. That is deliberate and practical: a thruster's flame emitter and the light it
casts then take **the same numbers**, so you author the cone once instead of converting between two
conventions.

The rim fades rather than cutting: a hard angular edge reads as a cardboard pie slice, for the same
reason a linear radial falloff reads as a hard-edged disc.

##### Walls — occluders that cast shadows

Publish a rectangle and light stops passing through it:

| Topic | Payload | Notes |
|-------|---------|-------|
| `render:occluder` | `{x, y, w, h}` | opaque rect, **ephemeral** (re-publish each frame) |
| `render:occluder:add` / `:update` / `:remove` | `{renderId, x?, y?, w?, h?}` | **retained** — for static level geometry |

**`x, y` is the top-left CORNER**, not the centre — a rect's anchor is its corner, a light's is its
centre, and the field name is what says which. Getting it wrong shifts every wall by half its size,
which reads as "the shadows are offset" rather than as an anchor mistake.

**Use the retained form for level geometry.** This is the opposite advice to lights, for the
opposite reason: a light usually follows something that moves, a wall does not. Re-publishing a
level's walls every frame charges a cost proportional to its size for data that never changes. An
`:update` merges — a sliding door can move without restating its extent.

The two modes coexist: retained walls plus an ephemeral shutter in the same frame occlude together.

A wall is not a special case anywhere in the engine: it writes transmittance 0 into an occlusion map
that the light march multiplies through, so everything beyond it on that ray goes dark as an
arithmetic consequence. That is also why coloured and partial transmission (stained glass, fog) fit
the same mechanism — see `docs/design/lighting-transmittance-core.md`.

⚠️ **Occluders only block LIGHT, not sight.** A wall does not hide what is behind it from the
player; that is a visibility system and it lives in game code, not in the renderer.

⚠️ **Matter has a minimum useful thickness — about 3 screen pixels.** The light shader marches the
occlusion map in steps of a fixed **pixel** length, so matter thinner than one step can be stepped
over and the shadow comes out with holes in it. Three pixels *on screen*, which means a thin wall
becomes unreliable when you **zoom out** far enough, not when the lamp gets bigger. If you need a
hairline wall to keep occluding at any zoom, make the *occluder* rect thicker than the *sprite* that
draws it — nothing requires the two to match.

Shadow edges are hard (no penumbra) but **antialiased**: the lamp pass dithers the boundary across
neighbouring pixels and the composite resolves that into a ramp, so a diagonal edge reads as a clean
line rather than a staircase. The march is bounded at 64 samples, so a lamp wider than ~400 px on
screen loses a little edge precision at its rim. Locked by `LightingGpu [march]`, which fits a line
through the measured shadow boundary and fails if it bends.

##### Filters — stained glass that tints the light it lets through

A wall writes transmittance 0. A **filter** writes a *colour*, and that is the entire difference:
light passes, but it is not the same light on the other side.

| Topic | Payload | Notes |
|-------|---------|-------|
| `render:filter` | `{x, y, w, h, color, opacity?}` | tinting rect, **ephemeral** (re-publish each frame) |
| `render:filter:add` / `:update` / `:remove` | `{renderId, x?, y?, w?, h?, color?, opacity?}` | **retained** — for static stained glass |

**`x, y` is the top-left CORNER**, like every rect. `color` is `0xRRGGBBAA`; its alpha byte is
**ignored** — "how much of this tint applies" is `opacity` (default 1), and reading it from two
places would make one of them silently lose.

**`color` is the tint after ONE perpendicular crossing of the pane's thin axis**, i.e. of
`min(w, h)`. A ray entering at an angle travels further inside the glass and comes out darker, which
is what you want. So a red window is simply `color: 0xFF3333FF` — you never compute a per-unit
coefficient by hand, even though that is what the engine stores internally.

```cpp
// A tall, thin, red pane: 4 units thick, so light crossing it head-on comes out at (1.0, 0.2, 0.2).
auto f = std::make_unique<JsonDataNode>("f");
f->setInt("renderId", 42);
f->setDouble("x", 320.0); f->setDouble("y", 0.0);
f->setDouble("w", 4.0);   f->setDouble("h", 240.0);
f->setInt("color", static_cast<int>(0xFF3333FFu));
io->publish("render:filter:add", std::move(f));
```

**`opacity` is your guard rail.** The model is multiplicative, so it bites harder than instinct
suggests: three panes at 0.3 leave 2.7 % of the light. `opacity` blends the stated tint back towards
vacuum, and 0 is a *true* no-op — a filter dialled to nothing leaves the scene exactly as bright as
no filter at all.

⚠️ **A filter tints the LIGHT, not the VIEW.** Looking *through* stained glass will not colour what
you see behind it; only light that crosses the pane is affected. Same boundary as walls blocking
light but not sight.

⚠️ **A filter is not an occluder.** It tints without being opaque. A window that must both tint *and*
darken simply declares a dark tint — the model does not need both notions.

Filters and walls write into the same map, **multiplicatively**, so overlapping matter composes and
the order it was published in does not matter. Two panes stacked give the product of their tints,
either way round.

⚠️ **Precision on very thick panes.** The map is RGBA8, and the stored per-unit value crowds towards
1 as a pane thickens — past roughly 100 units the 8-bit quantum starts to shift the resulting tint
by a visible fraction. It is uniform across the pane (so it bands nothing), but a very large tinted
volume is better expressed as fog than as one enormous filter.

##### Fog — a medium that absorbs along the way, and glows

| Topic | Payload | Notes |
|-------|---------|-------|
| `render:fog` | `{x, y, w, h, density, color?, scatter?}` | absorbing volume, **ephemeral** |
| `render:fog:add` / `:update` / `:remove` | `{renderId, x?, y?, w?, h?, density?, color?, scatter?}` | **retained** — for a nebula, which does not move |

**`density` is the Beer-Lambert coefficient α, NOT an opacity.** It has no upper bound, and doubling
the distance travelled through the volume doubles its effect *in the exponent* — so what survives is
squared. This is the opposite convention to a filter's `color`, deliberately: a pane has a fixed
thickness and can state a result, a cloud cannot. Start an order of magnitude lower than instinct
suggests — combined with the `(1−d/r)²` falloff, absorption bites fast. `0.01`–`0.05` is a usable
haze over a few hundred world units.

**`color` (default white) makes the absorption selective**: `α_channel = density / colour_channel`, so
a channel at half brightness extinguishes twice as fast. That is what gives sunsets — blue dies before
red — and tinted nebulae. White is exactly neutral.

**`scatter` (default 0) is what makes the medium VISIBLE.** Without it a medium only *darkens*, which
in the void looks like a bug rather than an atmosphere. With it, the light crossing the volume is
re-emitted towards the viewer:

```
final = scene × (ambient + light) + light × scatter
```

⚠️ **That term is ADDED, never multiplied by the scene** — and it is the reason a beam is visible in
empty space at all. In the void `scene` is black, so `scene × light` is zero; a multiplicative
scattering term would only ever put a halo on solid surfaces and leave the void dark, which is the
exact opposite of a nebula.

Absorption and scattering are separate on purpose: a medium may absorb a great deal while scattering
little (black smoke) or the reverse (bright haze). Folding them into one number would delete half the
expressible matter.

⚠️ **Nothing attenuates the ambient.** The ambient term has no path — it is global by construction —
so a very foggy but strongly ambient scene will not look foggy. Consistent with the model, and
thoroughly confusing if you have not read this line.

##### Nebulae — a medium whose density varies

`render:fog` is a **rectangle of uniform density**: right for a fog bank or a smoke-filled room,
unusable as a cloud. `render:nebula` is a **disc** whose density peaks at the core and falls to
exactly zero at the rim.

| Topic | Payload | Notes |
|-------|---------|-------|
| `render:nebula` | `{cx, cy, radius, density, color?, scatter?}` | soft radial medium, **ephemeral** |
| `render:nebula:add` / `:update` / `:remove` | `{renderId, cx?, cy?, radius?, density?, color?, scatter?}` | **retained** — the right form for a cloud |

**`cx, cy` is the CENTRE** — a disc's anchor, unlike the rect media above. `density` is the *peak*
Beer-Lambert α, in the same units as `render:fog`, so a value tuned on one transfers to the other.
`color` and `scatter` behave exactly as they do for fog.

**Overlap several to build a cloud, and use the RETAINED form for it.** Each volume fades to vacuum
at its own rim, so overlapping discs give an organic silhouette with no geometry showing — but that
means a cloud is four to six volumes, and re-publishing all of them every frame charges a per-frame
cost for data that never changes. `:add` them once; `:update` merges, so a drifting cloud moves by
naming only `cx`/`cy`. ⚠️ Do NOT try this with `render:fog`: a
stack of rectangles produces visible concentric outlines — a ziggurat, not a cloud. That is measured,
not theoretical, and it is why this primitive exists.

⚠️ **Start around `0.02`, not `0.9`.** The density is per unit of length, so it compounds: a volume
of radius 150 at `density: 0.9` absorbs `exp(-135)` through its core — a black disc. Every author's
first instinct here is one to two orders of magnitude too high.

⚠️ **Overbright is easy to reach.** The scattered term is additive and unclamped (RGBA16F), so a dense
medium under an intense lamp saturates. That is intended — it is what the **bloom** feeds on — but
it means `scatter` near 1 with `intensity` above 2 will flatten to white.

##### Asking "is this point lit?" from gameplay

The same curve is available as plain C++ (`include/grove/light/Light.h`, header-only, no renderer
dependency) so stealth, visibility or spawn rules can query it without reading back a texture:

```cpp
#include <grove/light/Light.h>

grove::light::Light2D lamp{nozzleX, nozzleY, 90.0f, 1.0f, 0.75f, 0.44f, 1.5f};
float r, g, b;
grove::light::contribution(lamp, playerX, playerY, r, g, b);
const bool inTheLight = (r + g + b) > 0.15f;
```

##### What it costs

A light is **one draw**, and its real cost is fill rate — the pixels it covers, blended additively.
Many *small* lights are therefore cheaper than one huge one, and that gap widened considerably once
shadows landed: every covered pixel now marches the occlusion map, up to **64 texture samples**, and
the sample count follows how far the pixel sits from the lamp *on screen*. Ten small lamps are
comfortably cheaper than two that fill the viewport.

**Measured** (`tests/visual/benchmark_lighting.cpp`, RTX 4060 Laptop, 1280×720). The cost is
proportional to **covered viewports** — the lamps' areas summed and divided by the screen, with
overlap counted twice — and to essentially nothing else. Lamp *count* does not appear in the model:

| | per covered viewport | 60 fps budget (16.6 ms) |
|---|---|---|
| no matter published | **19.5 µs** | ~850 covered viewports |
| any matter published | **355 µs** | **~47 covered viewports** |

Read that second row carefully: **publishing a single occluder multiplies the cost of every lamp by
18**, because the march then samples a real screen-sized map instead of the 1×1 vacuum placeholder.
It is not proportional to how *much* matter there is — one wall costs the same as five hundred.

Concretely, at 1280×720 with walls in the scene: **~45 lamps of radius 300 px**, or ~700 of radius
60 px, would consume the entire 60 fps frame. Halve that for a game that also has to draw something.
Without any matter, lamps are effectively free at any count you are likely to publish.

To convert to your own numbers: one lamp of radius *r* covers `(2r)² / (screenW × screenH)`
viewports, clipped to the screen.

⚠️ Machine-dependent, obviously — re-run the bench rather than trusting these figures on other
hardware. The *shape* (fill-rate bound, ×18 for matter, count-independent) is what transfers.

The other limit is unchanged and unrelated to the GPU: publishing lights over IIO costs one message
each (~5k primitives/frame across all topics), the same wall the bulk sprite path exists to break. If
you need a light *per particle*, measure before building — see `docs/design/lighting-2d.md` §6bis.

##### Not there yet

Shadows and cone lights *are* here — this section used to say otherwise and was simply out of date.
What is genuinely missing:

- **No soft shadows.** Edges are hard (and antialiased); there is no penumbra, because there is no
  light *area* in the model. The antialiasing is edge quality, not physics — don't confuse the two.
- **Bloom is here** (`render:bloom`, see below) — this line used to say post-processing was missing.
  What is *still* missing is the rest of the chain: no tonemapping, no fades, no colour grading. They
  belong on the present pass the bloom introduced, which exists partly for them.
- **Bloom `radius` is usable up to ~96 px**, past which it degrades. The blur resolution follows the
  radius (1/4, 1/8 or 1/16 of the screen) so that a tap's footprint always covers the gap to the next
  one; without that the glow shows a scalloped hem instead of a gradient. Going further would mean
  1/32, which trades the hem for visible blocks — so it stops. (The ceiling was 24 px until the
  resolution became radius-driven, and it was SEEN on a capture; the "~60 px" written before that from
  theory was wrong.)
- **No textured or animated density.** A medium is a uniform rect or a radial disc; you cannot hand
  it a noise texture. Overlapping several volumes is the intended way to get an irregular cloud.
- **Occluders block LIGHT, not SIGHT.** Hiding what is behind a wall is a visibility system and
  belongs in game code.
- ~~No measured lamp budget~~ — it **is** measured now, see *What it costs* above.

Design + slices: `docs/design/lighting-2d.md` (entry point; it links the per-matter plans).

#### Bloom (post-processing)

Adds a glow around what is **over-exposed** — the halo bleeding out of a lamp, an engine trail
smearing, a stained-glass pane dazzling. One persistent setting, published once:

```cpp
auto bloom = std::make_unique<JsonDataNode>("bloom");
bloom->setDouble("intensity", 1.5);     // how much glow is added back. 0 = OFF (the default)
bloom->setDouble("threshold", 1.0);     // luminance above which a pixel glows (1.0 = only overbright)
bloom->setDouble("radius", 24.0);       // glow extent in SCREEN PIXELS
io->publish("render:bloom", std::move(bloom));
```

**⚠️ Bloom requires lighting to be active.** It feeds on the *composited* frame, and without
`render:ambient` there is no composite at all — the scene goes straight to the backbuffer, and a
backbuffer cannot be sampled. If you want post-processing without a lit look, publish a **white
ambient**: it leaves the scene unchanged by construction.

**Why the composited frame and not the light buffer** — this is the one architectural decision, and it
has a consequence you can use: because the source is the final image, an **additive sprite**
(`blend:"additive"`) glows too, not just lamps. That is the engine-plume shape. The alternative would
have been cheaper by one pass and would have left it dark.

| Knob | Default | What to know |
|---|---|---|
| `intensity` | **0 = off** | It is the switch as much as the value. 1–3 is the usual range. |
| `threshold` | 1.0 | Only what exceeds 1 glows — which is exactly why the targets are RGBA16F. `0` = everything glows (a veil). |
| `radius` | 16 | **Screen pixels**, so the glow keeps its thickness when the window resizes. |

The threshold has a **soft knee** (half the threshold, not a knob): without it the glow's *slope*
jumps at the threshold and the halo starts with a visible hem. The curve is plain C++ in
`grove::light::brightPassFraction` (`include/grove/light/Bloom.h`), so a game can ask "would this
colour bloom?" without a GPU readback — same contract as the light falloff.

**Cost**: one extra full-screen RGBA16F target plus four full-screen passes (composite→HDR,
extract, two blurs, present), of which the two blurs and the extract run at **quarter resolution**.
Paid only while `intensity > 0`; at 0 nothing is built and the composite writes straight to the
backbuffer, byte for byte as before.

**The HUD does not glow** — it is submitted after the present pass. A sharp interface over a dazzling
world is the intent, not an oversight.

Plan + measurements: `docs/design/lighting-bloom.md`.

#### Tonemapping (post-processing)

Compresses the HDR range into what a screen can show, so **two overbright values stay different**
instead of both clipping to white. One persistent setting, independent of bloom:

```cpp
auto t = std::make_unique<JsonDataNode>("tonemap");
t->setString("mode", "aces");       // "none" (default = OFF), "reinhard", or "aces"
t->setDouble("exposure", 1.4);      // multiplies the scene BEFORE the curve
io->publish("render:tonemap", std::move(t));
```

**⚠️ Enabling it will DARKEN your scene, and that is not a bug.** `reinhard(1) = 0.5`: a value that
used to be full white now sits at mid-grey. That is what a compression curve does — it makes room
above. Raise `exposure` (start around 1.5–2.5) until the midtones sit where you want them.

**Why it matters** — measured on a white surface under a single lamp:

| Lamp intensity | No tonemap | reinhard | aces |
|---|---|---|---|
| 2 | **255** | 170 | 233 |
| 8 | **255** | 226 | 255 |

Without a curve the two are the same white: the overbright the RGBA16F targets exist to preserve was
being thrown away on the last line of the pipeline.

**Which mode**

- **`reinhard`** — `x/(1+x)`. Gentle, predictable, and it *never reaches 1*, so it keeps separating
  values however bright they get. Pick it for extreme dynamic range.
- **`aces`** — filmic: more contrast, a shoulder that rolls into white. ⚠️ **It saturates around 6**,
  so beyond that it re-clips like before. Pick it for the look, and use `exposure` to keep your
  brightest content under its white point.

**Ordering, which you cannot change but should know**: the bloom glow is added *before* the curve, so
it participates in the exposure rather than sitting on top as a white patch. The curve runs **per
channel**, so a saturated highlight rolls toward white the way film does. And the **HUD is not
tonemapped** — it is submitted after the present pass, so your interface stays legible whatever the
scene's exposure.

Plan + measurements: `docs/design/lighting-tonemap.md`.

#### Fades (post-processing)

A full-screen fade — to black for a scene transition, to white for a flash, to red for damage:

```cpp
auto f = std::make_unique<JsonDataNode>("fade");
f->setDouble("amount", 1.0);        // 0 = off (default) .. 1 = the screen IS the colour
// f->setInt("color", 0xFF2010FF);  // optional; BLACK by default
io->publish("render:fade", std::move(f));
```

**✅ Unlike bloom and tonemapping, a fade needs NOTHING** — no `render:ambient`, no HDR target. It is
one blended quad over whatever was drawn, so it works in a game that never lights anything. If you use
exactly one thing from this whole post-processing family, it can be this.

**It covers the HUD.** The fade is drawn on its own view, submitted *last* — after the interface. That
is deliberate: a scene transition has to take the UI with it, or your menus float on a black screen.
(Bloom and tonemapping do the opposite and spare the HUD, so it stays sharp and legible.)

**Ramp `amount` yourself.** There is no duration and no easing: publish a new `amount` each frame for
as long as the transition lasts. The engine does not own that clock — a built-in duration would have to
decide whether a paused game freezes the fade, and would take any non-linear curve away from you.

```cpp
// A one-second fade to black, driven by the game.
m_fadeT = std::min(1.0f, m_fadeT + dt);          // your own easing goes here
auto f = std::make_unique<JsonDataNode>("fade");
f->setDouble("amount", m_fadeT);
io->publish("render:fade", std::move(f));
```

`amount` is clamped to [0,1]: past 1 a mix would *extrapolate* and give artefacts instead of a full
screen. The colour's alpha byte is ignored — `amount` is the alpha.

Plan + measurements: `docs/design/lighting-fade.md`.

#### Colour grading (post-processing)

Retouches the finished image — the same set can be a cold morning, a washed-out memory or a red alert
without changing a single asset:

```cpp
auto g = std::make_unique<JsonDataNode>("grade");
g->setDouble("saturation", 0.3);     // 0 = greyscale, 1 = neutral (default), >1 = garish
g->setDouble("contrast", 1.2);       // <1 flattens toward mid-grey, >1 pushes away
g->setInt("tint", 0x8090FFFF);       // white = neutral (default) — a colour, not three floats
io->publish("render:grade", std::move(g));
```

**There is no `brightness`, on purpose.** It already exists as the tonemapping's `exposure`, and it sits
on the *right* side of the curve: a gain applied after compression only clips earlier, undoing what the
tonemap just saved. If you want a brighter image, raise `exposure`.

Same reason, and worth knowing: **`tint` can only darken a channel** (a colour byte tops out at 1.0).
That is deliberate — brightening is `exposure`'s job, and a second path to it would be a trap.

**It spares the HUD**, unlike the fade. A desaturated world under an interface that keeps its colours is
the intent: the HUD is a reading surface, not part of the fiction, and greying a red warning would blind
the player exactly when it matters. (The fade does the opposite and covers everything — that is why it
has its own pass.)

**Order inside the grade is fixed**: tint → contrast → saturation, the order a real grade uses. It is
not commutative — tinting *after* desaturating would give you a sepia wash instead of a balanced,
desaturated image.

Two details that come from the maths and will save you a puzzled hour:

- **Contrast pivots on 0.5**, not 0.18, because grading runs *after* tonemapping — in display space,
  where mid-grey is 0.5. A scene-linear pivot would darken every image you added contrast to.
- **Desaturation follows perceptual luminance** (the same one the bloom threshold uses). Pure red
  desaturates to 54/255 and pure blue to 18/255, because the eye sees them at very different
  brightnesses. A naive `(r+g+b)/3` would send both to 85 and flatten your palette.

Plan + measurements: `docs/design/lighting-grade.md`.

#### Bulk Sprite Submission (high throughput)

`render:sprite` is **one IIO message per sprite**. The bus no longer deep-copies the payload per
delivery (zero-copy delivery — see *The message payload* above; a static/linked-in host publishes it
with zero json copies). But each sprite still costs a `JsonDataNode` to **build** (`make_unique` +
the `set*` calls) plus the per-message bus machinery (envelope, queue, dispatch). That per-message
overhead — node construction, not a copy — is what keeps the path in the low thousands of
sprites/frame at 60 fps: fine for UI and a few hundred entities, a wall for thousands. The GPU itself
is nowhere near saturated (10 k sprites draw in <1 ms).

For thousands of sprites, a **statically-linked host** that already holds packed instances feeds
them straight to the renderer — bypassing IIO and JSON entirely:

```cpp
// SpriteInstance is the GPU-ready POD (Frame/FramePacket.h): position, scale, rotation,
// UVs, textureId, layer, and rgba floats. The host fills a contiguous array each frame.
std::vector<grove::SpriteInstance> instances = buildMySprites();

// Call BETWEEN frames (after the previous frame, before the next engine/renderer step).
// One vector insert, ~ns/sprite — no JSON, no IIO routing.
renderer->submitSpriteBatch(instances.data(), instances.size());
```

Measured (`tests/visual/benchmark_render_savage.cpp`, D3D11): the bulk path sustains the **60 fps
sprite ceiling from ~5 k → ~100 k (≈21×)**, and at low counts a frame is ~0.5 ms (≈30× cheaper).
World-space, no per-sprite asset/clip resolution — the host hands final instances. Use `render:sprite`
for UI / a handful of dynamic entities; use `submitSpriteBatch` for crowds, bullet-hell, particles-as-
sprites, large tile-entity counts. (For huge **static** content, prefer a retained tilemap — it scales
to millions of tiles at 60 fps because it uploads once.)

#### Rendering Text

```cpp
auto text = std::make_unique<JsonDataNode>("text");
text->setDouble("x", 50.0);
text->setDouble("y", 50.0);
text->setString("text", "Score: 100");
text->setDouble("fontSize", 24.0);
text->setInt("color", 0xFFFFFFFF);
text->setInt("layer", 100);                // Text on top
io->publish("render:text", std::move(text));
```

#### Camera Control

```cpp
auto camera = std::make_unique<JsonDataNode>("camera");
camera->setDouble("x", worldLeft);          // world coord at the viewport TOP-LEFT corner
camera->setDouble("y", worldTop);           // (NOT the center — see convention below)
camera->setDouble("zoom", 1.0);             // >1 zoom-in, <1 zoom-out
camera->setInt("viewportX", 0);
camera->setInt("viewportY", 0);
camera->setInt("viewportW", 1920);
camera->setInt("viewportH", 1080);
io->publish("render:camera", std::move(camera));
```

**Convention (important):** the camera `(x,y)` is the world coordinate at the viewport's
**top-left corner** — *not* the center (unlike `render:sprite`, whose `cx,cy` is the sprite
center). The projection collapses to:

```
screen = zoom · (world − cameraTopLeft)        world = cameraTopLeft + screen / zoom
```

So zooming is anchored at the top-left. To center on a point, or zoom toward the cursor,
don't compute the corner by hand — use the camera helper.

#### Camera Helper — `grove::camera` (seamless zoom/pan)

Header-only math the engine ships so you don't re-derive the projection. Available to any
host that links `BgfxRenderer_static` (its source dir is a PUBLIC include):

```cpp
#include "Scene/Camera.h"
using namespace grove::camera;

CameraView view{0, 0, 1.0f, 1920, 1080};     // x, y, zoom, viewportW, viewportH

// Picking / "what's under the cursor":
float wx, wy;  screenToWorld(view, mouseX, mouseY, wx, wy);

// Frame a target:
view = centerOn(planetX, planetY, zoom, 1920, 1080);          // focal point at screen center
view = focusOn(x, y, zoom, 1920, 1080, anchorX, anchorY);     // focal point under a screen anchor

// Seamless zoom toward the cursor (keeps the world point under the cursor fixed):
view = zoomAt(view, newZoom, mouseX, mouseY);

// Smooth it (framerate-independent — "zoom fluide / momentum"):
view.zoom = damp(view.zoom, targetZoom, 8.0f, deltaTime);

// Cull off-screen work: skip submitting AND computing (rotation/anim) what isn't visible.
if (isVisible(view, obj.x, obj.y, obj.w, obj.h, /*margin*/64.0f)) {
    // update its transform/anim + publish its sprite
}   // off-screen: skip the presentation work (the sim keeps running elsewhere)

// Then publish view.x / view.y / view.zoom on render:camera as above.
```

`zoomAt` is the primitive behind a continuous system↔tactical zoom: ramp `newZoom` per
frame (via `damp`) and the focal point stays pinned. The renderer has **no level-load
barrier** — every frame draws whatever you submit — so a "seamless" transition is just the
game swapping what it submits while the zoom ramps. Locked by `CameraUnit` +
`SceneCollectorTest` (the latter proves the engine's matrices match these helpers).

#### Zoom strata — `grove::camera::ZoomLadder` (`Scene/ZoomLadder.h`)

`zoomAt`/`damp` give *continuous* zoom; `ZoomLadder` gives it **readable plateaus** and a
**strata model** for a galaxy↔system↔ship↔interior continuum. The engine owns the MATH; the
game owns the CONTENT (what to render/simulate per strata stays game-side, exactly like the
tilemap LOD where the engine gives the crossfade factor and the shader uses it).

```cpp
#include "Scene/ZoomLadder.h"
using namespace grove::camera;

// Plateaus = the readable zoom levels (ascending). transitionWidth = how much of each gap ramps.
ZoomLadder ladder({0.05f, 0.5f, 4.0f, 16.0f}, /*transitionWidth*/0.5f);

// Per frame: snap toward the nearest plateau so the scale "poses" and reads (not infinite mush):
float targetZoom = ladder.snap(view.zoom);
view.zoom = damp(view.zoom, targetZoom, 8.0f, deltaTime);

// Locate the zoom on the ladder to drive a SEAMLESS inter-strata transition:
ZoomBlend b = ladder.blend(view.zoom);
//  b.active        -> which strata to simulate/render (game decides the content)
//  b.lower/b.upper -> the two strata it's between
//  b.t             -> 0..1 crossfade factor between them (fade content in/out across the transition)
```

Work is in **log-zoom space** (zoom is multiplicative → equal ratios are equal steps). Pure,
header-only, no GPU. Locked by `ZoomLadderUnit` (analytical oracles). It deliberately does NOT
decide content or toggle modules — that's the game's call; the ladder only hands it the seam.

#### Nested-zones navigation — `grove::camera::ZoneNavigator` (`Scene/ZoneNavigator.h`)

For a continuum where zoom **enters things** (galaxy → system → ship → room), `ZoneNavigator` drives
the whole camera from a tree of **zones** you sync from your game. It composes the helpers above into
the full feel: zoom into the zone under the cursor, pan locked + scaled to the active zone, a soft
magnet that frames it, per-layer zoom bounds, camera roll, and a lock onto moving zones. Header-only
logic (no bgfx). Design: [docs/design/zone-navigation.md](design/zone-navigation.md).

**The deal:** the engine owns the navigation *mechanics*; the GAME owns the zone *hierarchy* (what /
where) — it syncs zones (id, parent, world bounds), feeds input, and publishes the result on
`render:camera`. An empty `parentId` marks the root.

```cpp
#include "Scene/ZoneNavigator.h"
using namespace grove::camera;

ZoneNavigator nav;
nav.configure(1280.0f, 720.0f);    // viewport; sensible defaults for the rest (see knobs below)

// Build the tree once (mirror your game hierarchy). Root has an EMPTY parent. Bounds are world AABBs.
nav.addZone("galaxy", "",       WorldBounds{0.0f, 0.0f, 8000.0f, 8000.0f});
nav.addZone("sysA",   "galaxy", WorldBounds{1000.0f, 1000.0f, 2200.0f, 2200.0f});
nav.addZone("shipA1", "sysA",   WorldBounds{1200.0f, 1200.0f, 1340.0f, 1310.0f});
nav.reset();                       // snap to frame the root

// --- per frame ---
// 1. Re-sync any zone bound to a MOVING entity (addZone is idempotent -> updates the bounds). If that
//    zone is active, the camera LOCKS onto it (rides its motion).
nav.addZone("shipA1", "sysA", boundsOf(ship));      // ship slides -> camera follows when you're in it

// 2. Feed input (all optional).
if (wheelY != 0)           nav.zoomBy(wheelY > 0 ? 1.25f : 0.8f, mouseX, mouseY);  // zoom toward cursor
if (panDx || panDy)        nav.panScreen(panDx, panDy);     // on-screen delta; rotated + scaled for you
if (rollLeft || rollRight) nav.rotateBy(rollRight ? +dr : -dr);
if (zoneRemoved)           nav.removeZone(deletedId);       // active gone -> seamless back-out

// 3. Advance + publish. update() returns the eased CameraView to drive render:camera.
CameraView v = nav.update(dt);
auto cam = std::make_unique<JsonDataNode>("camera");
cam->setDouble("x", v.x); cam->setDouble("y", v.y); cam->setDouble("zoom", v.zoom);
cam->setDouble("rotation", v.rotation);
cam->setInt("viewportW", (int)v.viewportW); cam->setInt("viewportH", (int)v.viewportH);
io->publish("render:camera", std::move(cam));
```

**API**
- `configure(vpW, vpH, margin?, magnetRate?, panMargin?, maxDetail?, snapStrength?, snapRange?, leadSeconds?)`
  — viewport + feel knobs (below).
- `addZone(id, parentId, WorldBounds)` — add OR update a zone (idempotent: re-adding updates the bounds
  and keeps children; moving the active zone locks the camera onto it). Empty `parentId` = root.
- `removeZone(id)` — drop the zone + its subtree; if the active zone vanishes, ease back to the nearest
  living ancestor (one level, or two…).
- `setActive(id)` — explicitly frame a zone (eased).
- `zoomBy(factor)` / `zoomBy(factor, screenX, screenY)` — zoom toward the centre, or toward the cursor
  (the world point under it stays pinned; you enter the zone you're pointing at).
- `panScreen(dxScreen, dyScreen)` — pan by an on-screen delta (rotated into the camera frame, scaled by
  1/zoom, clamped to the active zone + pan margin). EASED through the magnet — for keyboard/edge pan.
- `dragPan(dxScreen, dyScreen)` / `endDragPan()` — **mouse drag-to-pan** (grab): moves the LIVE view 1:1
  **immediately** (no magnet lag — a grab must track the cursor), then `endDragPan()` on release lets the
  residual velocity glide to a stop (light kinetic inertia, see `setPanInertia`). Feed it the per-frame
  cursor delta (negated = "grab", the world follows the cursor); the `grove::camera::DragPan` helper
  (`Scene/DragPan.h`) turns a button press/move/release into those deltas (button-agnostic).
- `setLeadSeconds(s)` / `setPanInertia(rate)` — runtime feel setters (velocity lead; drag-release inertia,
  0 = cut dead). Tune without re-passing the whole `configure` list.
- `rotateBy(dRadians)` / `setRotation(radians)` — camera roll.
- `update(dt) -> CameraView` — ease toward the target; the value to publish on `render:camera`.
- `reset()` — snap to frame the root (call once after building the tree).
- Getters: `activeZone()`, `view()`, `zoom()`, `rotation()`, `focusX()/focusY()`, `hasZone(id)`.

**Knobs** (`configure`)

| Param | Default | Effect |
|-------|---------|--------|
| `margin` | 0.05 | framing padding — the zone fills `1 - margin` of the view |
| `magnetRate` | 6.0 | glide snappiness (higher = `update()` eases faster toward the target) |
| `panMargin` | 0.25 | how far pan may overshoot a zone edge (fraction of the screen) for context around a POI |
| `maxDetail` | 3.0 | max zoom-in past the deepest zone's framing — the per-layer cap (anti-void) |
| `snapStrength` | 8.0 | zoom-snap ease rate (0 = off). On release after a zoom-**IN**, the zoom auto-completes to frame the zone you're entering (*focus*). Zoom-IN only, upward only — it can never zoom you out; zoom-OUT is always free |
| `snapRange` | 0.7 | how close (log-zoom) to a framing the snap engages — free beyond it (detail zoom stays free) |
| `leadSeconds` | 0.0 | velocity **lead** (0 = off): when the active zone moves, look this many seconds *ahead* of it so a fast entity isn't dragged to the screen edge by the magnet lag (you see where it's going). Bounded — the led-to point stays on screen — and decays to zero when the motion stops |

**Notes / limits**
- Zoom-in is bounded **per layer**: to the active zone's *subtree* deepest framing × `maxDetail` (a
  shallow zone caps low, a deep one plunges). Zoom-out is bounded to the root framing.
- Moving zones: the active zone is always **position-locked** (the camera rides its centre); set
  `leadSeconds > 0` to additionally **lead** ahead of its velocity (anticipation). Lead is bounded and
  self-decaying, off by default.
- Mouse pan: use `dragPan` (not `panScreen`) for a click-drag grab — it's immediate (1:1), where
  `panScreen` is magnet-eased. `setPanInertia(rate)` adds a light release glide (`0` = cut dead).
- Camera roll: `render:camera` carries `rotation` (radians; pivot = screen centre) — pan and
  cursor-zoom run in the camera frame, and the pan **clamp is rotation-aware** (it bounds the rolled
  view's world AABB, so no corner escapes the zone). Only `fitBounds` *framing* a zone while rolled stays
  approximate (an exotic case — left parked).
- `ZoomLadder` above still fits a content-less continuous zoom; `ZoneNavigator` is the richer,
  zone-driven option (zones become the plateaus). Locked by `ZoneNavUnit`.

**Full Topic Reference:** See [IIO Topics - Rendering](#rendering-topics)

---

### UIModule - User Interface

**Status:** ✅ Development Ready (Phase 7 complete) | ⚠️ Experimental

Complete UI widget system with layout, scrolling, and tooltips.

#### Available Widgets

| Widget | Purpose | Events |
|--------|---------|--------|
| **UIButton** | Clickable button | `ui:click`, `ui:action` |
| **UILabel** | Static text | - |
| **UIPanel** | Container | - |
| **UICheckbox** | Toggle checkbox | `ui:value_changed` |
| **UISlider** | Value slider | `ui:value_changed` |
| **UITextInput** | Text input field | `ui:value_changed`, `ui:text_submitted` |
| **UIProgressBar** | Progress display | - |
| **UIImage** | Image/sprite | - |
| **UIScrollPanel** | Scrollable container | `ui:scroll` |
| **UITooltip** | Hover tooltip | - |

#### Configuration

```cpp
JsonDataNode uiConfig("config");
uiConfig.setInt("windowWidth", 1920);
uiConfig.setInt("windowHeight", 1080);
uiConfig.setString("layoutFile", "./ui/main_menu.json");  // JSON layout
uiConfig.setInt("baseLayer", 1000);  // UI renders above game (layer 1000+)

uiModule->setConfiguration(uiConfig, uiIO.get(), nullptr);
```

#### Creating a Button

```cpp
// In your layout JSON file (ui/main_menu.json)
{
  "type": "UIButton",
  "id": "play_button",
  "x": 100,
  "y": 200,
  "width": 200,
  "height": 50,
  "text": "Play Game",
  "action": "start_game"
}
```

```cpp
// In your game module - subscribe to button events with callbacks (in setConfiguration)
gameIO->subscribe("ui:action", [this](const grove::Message& msg) {
    std::string action = msg.data->getString("action", "");
    std::string widgetId = msg.data->getString("widgetId", "");

    if (action == "start_game" && widgetId == "play_button") {
        startGame();
    }
});

// In process() - pull and dispatch to callbacks
while (gameIO->hasMessages() > 0) {
    gameIO->pullAndDispatch();  // Callback invoked automatically
}
```

#### Handling Input Events

UIModule automatically consumes input events from InputModule:

```cpp
// UIModule subscribes to:
// - input:mouse:move
// - input:mouse:button
// - input:mouse:wheel
// - input:keyboard:key
// - input:keyboard:text

// Your game module just subscribes to UI events:
gameIO->subscribe("ui:*");  // All UI events
```

#### UI Rendering

UIModule publishes render commands to BgfxRenderer via `UIRenderer`:

```cpp
// UIModule uses retained mode rendering (only publishes on change):
// - render:sprite:add/update/remove (for UI rectangles/images)
// - render:text:add/update/remove (for labels/buttons)

// BgfxRenderer consumes these and renders the UI
// Layer management ensures UI renders on top (layer 1000+)
```

**Retained Mode:** Widgets cache render state and only publish IIO messages when visual properties change. This reduces message traffic by 85%+ for typical UIs. See [UI Rendering Documentation](UI_RENDERING.md) for details.

**Full Topic Reference:** See [IIO Topics - UI Events](#ui-events)

---

### InputModule - Input Handling

**Status:** ✅ Development Ready (Phase 1-3 complete) | ⚠️ Gamepad Phase 2 TODO

Cross-platform input handling with SDL2 backend (mouse, keyboard).

#### Features

- Mouse (move, button, wheel)
- Keyboard (key events, text input)
- Thread-safe event buffering
- Multiple backend support (SDL2, extensible)
- Hot-reload support

#### Configuration

```cpp
JsonDataNode inputConfig("config");
inputConfig.setString("backend", "sdl");
inputConfig.setBool("enableMouse", true);
inputConfig.setBool("enableKeyboard", true);
inputConfig.setBool("enableGamepad", false);  // Phase 2

inputModule->setConfiguration(inputConfig, inputIO.get(), nullptr);
```

#### Feeding Events (SDL2)

```cpp
// In your main loop
SDL_Event event;
while (SDL_PollEvent(&event)) {
    // Feed to InputModule (thread-safe)
    inputModule->feedEvent(&event);

    // Also handle window events
    if (event.type == SDL_QUIT) {
        running = false;
    }
}

// Process InputModule (converts buffered events → IIO messages)
JsonDataNode input("input");
inputModule->process(input);
```

#### Consuming Input Events with Callbacks

```cpp
// Subscribe to input topics with callback handlers (in setConfiguration)
gameIO->subscribe("input:mouse:button", [this](const grove::Message& msg) {
    int button = msg.data->getInt("button", 0);  // 0=left, 1=middle, 2=right
    bool pressed = msg.data->getBool("pressed", false);
    double x = msg.data->getDouble("x", 0.0);
    double y = msg.data->getDouble("y", 0.0);

    if (button == 0 && pressed) {
        // Left mouse button pressed at (x, y)
        handleClick(x, y);
    }
});

gameIO->subscribe("input:keyboard:key", [this](const grove::Message& msg) {
    int scancode = msg.data->getInt("scancode", 0);  // SDL_SCANCODE_*
    bool pressed = msg.data->getBool("pressed", false);

    if (scancode == SDL_SCANCODE_SPACE && pressed) {
        playerJump();
    }
});

// In process() - pull and auto-dispatch to callbacks
while (gameIO->hasMessages() > 0) {
    gameIO->pullAndDispatch();  // Callbacks invoked automatically
}
```

**Full Topic Reference:** See [IIO Topics - Input Events](#input-events)

#### Input bindings — `grove::input::ActionMap`

Header-only helper (`modules/InputModule/ActionMap.h`) that maps **physical inputs to named
actions** instead of hardcoding key constants. Bind by **scancode** (physical key position) — this
is **layout-proof**: `SDL_SCANCODE_*` is the same physical key on QWERTY and AZERTY, whereas a
character keycode (`SDLK_MINUS`) lands on a different key per layout (the bug class this fixes).

```cpp
#include "InputModule/ActionMap.h"
using namespace grove::input;

ActionMap actions;
actions.bindKey("zoom_in", SDL_SCANCODE_PAGEUP);     // multi-bind per action allowed
actions.bindKey("zoom_in", SDL_SCANCODE_KP_PLUS);
actions.bindMouseButton("select", 0);

// per frame:
actions.beginFrame();                                 // clears justPressed/justReleased edges
// feed raw events (e.g. from SDL): actions.onKey(e.key.keysym.scancode, e.type==SDL_KEYDOWN);
//                                  actions.onMouseButton(button, pressed);
if (actions.isActive("zoom_in"))     { /* held */ }
if (actions.justPressed("select"))   { /* edge this frame */ }
```

Multi-bind (an action releases only when its LAST held key lifts), one key → many actions, OS
key-repeat is idempotent, and `clearAction` + re-`bind` remaps at runtime. Pure/std-only (no SDL
dependency — scancodes are plain ints). Live reference: `tests/visual/test_renderer_showcase.cpp`
drives all its controls through an ActionMap. Locked by `ActionMapUnit`.

> **Note (scancode vs intuition):** `SDL_SCANCODE_MINUS` is the US physical position (right of
> `0`), which on AZERTY is the `)°` key — *not* where `-` is printed. Scancodes are layout-
> *independent*, but pick **physically sensible** default keys (e.g. PgUp/PgDn for zoom) and let
> the player remap; "the minus key" is a poor default across layouts.

---

## Animation (`grove::anim`)

Pure, **header-only** 2D animation helpers in `include/grove/anim/` — no renderer, no IIO, no
SDL dependency. They **compute** transforms and UVs; they never draw. A static-link host just
`#include` and uses them (zero CMake/link). Two complementary families:

### 1. Procedural / cutout — `Hierarchy` + `Clip` + `AnimationPlayer`

"Linked objects": child sprites that move with a parent (a hull with a turret, a body with
limbs). A `Hierarchy` of `Transform2D` nodes; `update()` composes each node's WORLD transform
from its LOCAL transform and its parent. Keyframed motion comes from a `Clip` (tracks of
`Keyframe`s with `Easing` curves) played over time by an `AnimationPlayer`.

```cpp
#include "grove/anim/AnimationPlayer.h"
using namespace grove::anim;

Hierarchy rig;
int hull   = rig.addNode(-1, Transform2D{x, y});         // root
int turret = rig.addNode(hull, Transform2D{80.0f, 0.0f}); // child offset (local space)

Clip clip; clip.duration = 6.0f;
Track t; t.nodeId = turret; t.property = Property::Rotation;
t.keys = { {0.0f, 0.0f, Easing::Linear}, {6.0f, 6.2831853f, Easing::OutCubic} };
clip.tracks.push_back(t);

AnimationPlayer player;
player.play(&clip, /*loop*/ true);   // NOTE: player keeps a const Clip* — clip must outlive it

// each frame:
player.update(dt, rig);   // writes node LOCAL transforms from the clip
rig.update();             // composes every node's WORLD transform (one pass)
```

- `Property`: `TranslationX/Y`, `Rotation`, `ScaleX/Y`. `Easing`: `Step, Linear, In/Out/InOut
  Quad, In/Out/InOut Cubic` (a key's curve governs its outgoing segment).
- **Perf:** the player holds a `const Clip*` (shared, never copied) — thousands of instances
  reuse one clip's keyframes with tiny per-instance state; one `rig.update()` composes a whole
  rig. For purely code-driven motion you can skip Clip/Player and set node locals directly.

### 1bis. Named states with cross-fade — `Animator`

`AnimationPlayer` plays **one** clip. `Animator` is the layer above: named states, and a
**cross-fade** when you switch between them, so a character going from `walk` to `fall` doesn't
snap between two poses.

```cpp
#include "grove/anim/Animator.h"
using namespace grove::anim;

Animator anim;
anim.setDefaultFade(0.15f);                          // seconds; 0 (the default) = hard cut
anim.addState("idle",   &clipIdle);                  // loops
anim.addState("walk",   &clipWalk);
anim.addState("attack", &clipAttack, Once{"idle"});  // plays once, then returns to idle by itself

// each frame — call play() every frame, that is the intended usage:
anim.play(grounded ? "walk" : "fall");
anim.update(dt, rig);
rig.update();
```

**`play()` is idempotent.** Calling `play("walk")` on the state that is *already running* does
nothing — you publish a STATE, not a transition. This is what lets you call it unconditionally
every frame, which is how the calling code stays readable. A one-shot that has **finished** does
restart when you ask for it again (re-attacking works); the rule is "never rewind an animation
that is still *running*", not "never rewind the current name".

| Call | Meaning |
|---|---|
| `play("x")` | switch with the default fade |
| `play("x", 0.30f)` | switch with this fade, overriding the default |
| `play("x", 0.0f)` | force a **hard cut** even when a default fade is set |
| `setFadeEasing(Easing::InOutCubic)` | curve of the blend (linear by default) |
| `current()` / `isFading()` / `finished()` | observation |

**What the engine does NOT do here, on purpose**: there are no conditions, no predicates, no
transition rules. Your game decides *when* to call `play("fall")`; the engine only owns the
**seam**. Same line as `scene:goto {node}` in DialogueModule and the fixed behavior library in
FxModule — so there is no blend tree, no additive layers, no per-limb masks.

**Two things worth knowing before you rely on it:**

- **Rotations blend along the shortest arc.** Fading a limb from `3.0` rad to `-3.0` rad takes the
  0.28 rad path through ±π, not the 6.0 rad path through zero. A plain lerp would spin the limb
  almost a full turn the wrong way for the whole fade — and only when the two angles straddle ±π,
  which is why it is easy to miss.
- **The outgoing pose is FROZEN during a fade** (snapshot blending): the blend starts from what was
  actually on screen last frame, not from the outgoing clip still advancing. Cost: the outgoing
  animation holds still for the fade — imperceptible at the usual 0.1–0.2 s. Gain: switching state
  *during* a fade (walk → jump → fall in three frames, the normal case in a platformer) produces
  **no pop at all**, whatever the first fade had reached.

### 2. Frame-by-frame / flipbook — `SpriteSheet` + `Flipbook`

Cycling which atlas cell is shown (explosions, walk cycles, effects). `SpriteSheet` maps a
grid cell index to a UV rectangle; `Flipbook` plays a sequence of cells with per-frame
durations (or a uniform `setFps`).

```cpp
#include "grove/anim/Flipbook.h"
using namespace grove::anim;

SpriteSheet sheet; sheet.columns = 4; sheet.rows = 4;   // 16-cell atlas
Flipbook fb; fb.frames = {0,1,2,3,4,5}; fb.setFps(12.0f); fb.loop = true;
// (or per-frame timing: fb.durations = {0.3f, 0.1f, 0.1f, ...};)

float u0, v0, u1, v1;
fb.uvAt(time, sheet, u0, v0, u1, v1);   // UVs to put on a render:sprite
```

### Rendering it — the integration glue (game side)

`grove::anim` stays render-agnostic, so the game writes the small loop that turns world
transforms / flipbook UVs into `render:sprite` messages. This is intentional: the
node → `{textureId, layer, pixel size, renderId}` mapping is game-specific. Pattern (≈8 lines,
see `tests/visual/test_renderer_showcase.cpp` for a live reference):

```cpp
// procedural rig node -> sprite (render:sprite cx,cy = CENTER; rotation in radians)
const Transform2D& w = rig.world(node);
auto s = std::make_unique<JsonDataNode>("sprite");
s->setDouble("cx", w.x); s->setDouble("cy", w.y);
s->setDouble("rotation", w.rotation);
s->setDouble("scaleX", pixelSize * w.scaleX);
s->setDouble("scaleY", pixelSize * w.scaleY);
s->setInt("textureId", texId); s->setInt("layer", layer);
io->publish("render:sprite", std::move(s));

// flipbook -> set u0,v0,u1,v1 on the sprite from fb.uvAt(time, sheet, ...)
```

Locked headless by `Transform2DUnit`, `EasingUnit`, `ClipUnit`, `AnimationPlayerUnit`,
`SpriteSheetUnit`, `FlipbookUnit`.

---

## Sound (SoundManager)

Music + SFX module. `SoundManagerModule` (an `IModule`) consumes `sound:*` topics and drives an
`ISoundBackend`; the real backend is `SDLMixerBackend` (SDL2_mixer). The backend is behind the
interface, so the module's topic/bus logic is SDL-free and headless-testable.

**Topics consumed:**

| Topic | Payload | Effect |
|-------|---------|--------|
| `sound:sfx` | `{path, volume?=1, pan?=0, loop?=false, id?}` | Play an SFX. A game-supplied `id` makes it controllable (track it to stop later) |
| `sound:sfx:stop` | `{id, fadeMs?=0}` | Stop the SFX previously started with that `id` |
| `sound:sfx:stopAll` | `{fadeMs?=0}` | Stop all SFX (e.g. scene change) |
| `sound:music` | `{path, loop?=true, fadeMs?=0, volume?=1}` | Play/replace the music track |
| `sound:music:stop` | `{fadeMs?=0}` | Stop the music |
| `sound:volume` | `{bus: "master"\|"music"\|"sfx", value}` | Set a bus volume `[0,1]` |
| `sound:preload` | `{path}` | Load an SFX into cache (avoid first-play hitch) |
| `sound:unload` | `{path}` | Free a cached SFX |

Effective volume sent to the device = `clamp01(per-call volume × bus × master)`. Changing the
`master`/`music` bus re-applies live to the playing track.

### Adaptive music — `audio:*` (state-driven vertical layering, slice 1)

Beyond fire-and-forget `sound:music`, the module does **adaptive music**: a set of looping
**stems** (layers) whose gains are driven by the game's emotional **tension** (0..1), so layers
fade in/out as the mood shifts. *Hooks early, compositions late* — the game wires the spine now;
the stems/score come later. The `audio:*` namespace is **declarative** ("the mood is X"), distinct
from the imperative `sound:*` ("play this").

| Topic | Payload | Effect |
|-------|---------|--------|
| `audio:layer` | `{id, path, loop?=true, gainCalm?=1, gainPeak?=1, theme?, state?}` | Register/start a stem. Gain crossfades `gainCalm`→`gainPeak` over tension (`{1,1}`=bed, `{0,1}`=fades in, `{1,0}`=fades out). Tagging `theme`+`state` makes it a leitmotif **arrangement** (tension-exempt; see below) |
| `audio:theme` | `{id, state}` | Select a leitmotif's arrangement by state: the matching `theme`/`state` stem crossfades to its `gainPeak`, the others to 0 |
| `audio:intent` | `{tension, quantize?="now"}` | Set the emotional tension `[0,1]`; recomputes every stem's target. `quantize`: `"now"` immediately, `"bar"`/`"beat"` waits for the next measure (see below) |
| `audio:tempo` | `{bpm, beatsPerBar?=4}` | Set the musical clock for quantized transitions. `bpm=0` stops it (quantized intents then apply immediately) |
| `audio:mix` | `{id, gain}` | Low-level: set one stem's target gain explicitly (until the next `audio:intent`) |
| `audio:cue` | `{path, volume?=1, quantize?="now"}` | One-shot musical **sting** on the music bus; `quantize:"bar"/"beat"` lands it on the next measure |
| `audio:layer:stop` | `{id, fadeMs?=0}` | Stop + drop a stem |

Stem gains **ramp** smoothly toward their targets each `process()` (framerate-independent), so
tension changes fade layers instead of snapping. Adaptive stems sit on the **music bus**
(`sound:volume {bus:"music"}` scales them).

**Bar-quantized transitions (slice 2):** set a tempo with `audio:tempo`, then an
`audio:intent {tension, quantize:"bar"}` is **staged** and applied only when the beat clock crosses
the next bar — so section/mood changes land *on the measure* instead of jarringly mid-phrase
(`"beat"` snaps to the next beat; `"now"` / no tempo = immediate). The clock is a pure `BeatClock`
(`SoundManager/BeatClock.h`).

**Leitmotif by entity state (slice 3):** register several stems under one `theme` with different
`state`s (a leader's theme: `soft` / `twisted` / `triumphant` / `broken`). They are **tension-exempt**
(driven only by the selector) and start silent; `audio:theme {id:"leader", state:"broken"}` crossfades
to that arrangement and fades the others out — so the motif's *arrangement follows the entity's state*.

The mix math is a pure `AdaptiveMixer` (`SoundManager/AdaptiveMixer.h`); the game owns the tension,
the stems, and the states (content). Locked headless by `SoundManagerUnit` (`[adaptive]` cases —
32 cases / 89 assertions for the whole sound module). The adaptive-music vision (vertical layers +
bar-quantized transitions + cues + leitmotifs) is **shipped as logic**; real stems for an audible
pass are content (compositions late).

**Wiring (static-link host, e.g. Drifterra):** link `SoundManager_static`, instantiate the
module, inject the backend, drive `process()` each frame:

```cpp
#include "SoundManagerModule.h"
#include "SDLMixerBackend.h"
using namespace grove;

auto sound = std::make_unique<SoundManagerModule>();
sound->setBackend(std::make_unique<sound::SDLMixerBackend>());  // SDL_mixer behind ISoundBackend
JsonDataNode cfg("config");
sound->setConfiguration(cfg, soundIO, nullptr);     // subscribes sound:*, opens the device

// per frame: publish sound:* on a peer IIO, then
sound->process(frameInput);                          // drains the queue -> backend
```

Build with `-DGROVE_BUILD_SOUND_MODULE=ON` (needs SDL2 + SDL2_mixer). Topic/bus logic locked by
`SoundManagerUnit` (MockSoundBackend, headless); real audio verified manually via
`tests/visual/test_sound_demo.cpp`.

---

## Effects / FX Layer (`grove::fx` + FxModule)

A **data-driven layer for ephemeral, cosmetic visual effects** — explosions, debris, engine trails, muzzle
flash, warp shimmer, floating damage numbers. You compose an effect from **components** and **behaviors**;
the engine ticks the behaviors and emits the render traffic. It's Unity/Godot-flavoured (compose
components/prefabs) but **declarative** (data, no scripting language — a behavior is a fixed engine
primitive with numeric params, same discipline as the VN conditions). Reuse is the point: the behavior
library lives **engine-side** (every project inherits it) and **prefabs** let you define an effect once and
spawn it everywhere.

> ⚠️ **Scope — this is NOT for gameplay entities.** Use it for short-lived visuals with **no authoritative
> position** any gameplay system must own. For **gameplay crowds/agents** (fleets, units, projectiles that
> collide, anything at scale) do the opposite: **own your state in your own SoA and push it via
> `submitSpriteBatch`** (the flat-blob bulk path — see [Bulk Sprite Submission](#bulk-sprite-submission-high-throughput)).
> Movement/formation/AI belong in your game (or a movement product), not in `move{vx,vy}`. Routing a crowd
> through this layer's per-effect component map (AoS, one node per entity) rebuilds the per-primitive
> dispatch wall the bulk path exists to avoid.

The core is the pure header-only `grove::fx::FxWorld` (`include/grove/fx/FxWorld.h` — like `grove::anim`, no
IIO/renderer); `FxModule` (an `IModule`) wraps it onto the bus.

**The model.** An effect = a stable id + typed **components** + a list of **behaviors**:

- **`Transform`** `{cx, cy, rotation, scaleX, scaleY}` — `cx,cy` = CENTER (the [anchor convention](#anchor-convention--xy--corner--cxcy--center-read-this)).
- **`Sprite`** `{asset | textureId, color, layer}` — what it draws (omit → a logic-only effect).
- **`Text`** `{text, color, layer, fontSize}` — an optional text label (floating damage numbers, callouts). The
  string is **already localized by the consumer** (the engine is i18n-agnostic — it never translates). Sprite
  and Text are **orthogonal**: an effect may bear either or both, and each diffs into its own retained pool.
- **`Emitter`** `{prefab, count, speedMin, speedMax, spreadDeg, dirDeg, oneShot}` — a **one-shot particle burst**
  (explosions/debris/muzzle flash). See [Particle bursts](#particle-bursts-the-emitter-component) below.
- **behaviors** — from the fixed library below.

**Topics consumed:**

| Topic | Payload | Effect |
|-------|---------|--------|
| `fx:prefab` | `{name, transform?, sprite?, text?, emitter?, behaviors?}` | Register a reusable **archetype/template** (see Prefabs) |
| `fx:spawn` | `{id, archetype?, transform?, sprite?, text?, emitter?, behaviors?}` | Spawn an effect under a string `id`. With `archetype`, instantiate that prefab; the spawn's own fields then override/add on top |
| `fx:set` | `{id, transform?, sprite?, text?, emitter?}` | **Partial** update — only the fields you send change; the rest keep their value |
| `fx:destroy` | `{id}` | Remove the effect (emits its `render:sprite:remove` / `render:text:remove`) |

**Published:** for a **sprite** effect, `render:sprite:add` / `:update` / `:remove` (`cx,cy` = CENTER); for a
**text** effect, `render:text:add` / `:update` / `:remove` (`x,y` = top-left CORNER — that primitive's native
anchor, so a text effect's `Transform` position maps to the text's `x,y`). Both are keyed by the effect's
numeric id (= the renderer's `renderId`); sprite and text are **separate id spaces**, so an effect carrying
both never collides. Each `process(dt)`: drain the inbox → `tick(dt)` (advance behaviors) → `diffRender()`
(emit only what changed — the minimal retained-render traffic).

Robust by design: the JSON accessors **fail soft** to a default and never throw on a malformed payload —
an imperfect message degrades gracefully instead of crashing the engine.

### Behavior library (engine-side, reused across projects)

Behaviors are a **fixed set of primitives** the engine ticks — focused on **effect lifecycle**. Compose them
on an effect in data:

| `type` | Params | Effect |
|--------|--------|--------|
| `move` | `{vx, vy}` | Translate the center by `v·dt` each frame (constant velocity) |
| `spin` | `{degPerSec}` | Rotate (deg/s → rad) |
| `lifetime` | `{seconds}` | Destroy the effect after `seconds` (emits its Remove) |
| `fade` | `{seconds, fromAlpha, toAlpha}` | Ramp the **alpha** (AA byte) of the sprite AND/OR text color from `fromAlpha` to `toAlpha` over `seconds`, then hold. Defaults `{fromAlpha:1, toAlpha:0}` = fade-out |
| `velocity` | `{vx, vy, drag}` | Move at an initial velocity that **decelerates** by `drag` per second (debris/spark spread). `drag 0` = constant. *Explicit-Euler: tick at frame dt (~1/60 s), not big chunks — a single large dt can overshoot the decay* |

Behaviors on one effect tick in list order and compose (e.g. `move` + `lifetime` = a drifting spark, or
`velocity` + `fade` + `lifetime` = a muzzle flash that spreads, dims, and clears). Adding a reusable behavior
= one `Type` + one tick case in `FxWorld` — **every project gains it**. The library stays *effect-lifecycle*
focused — deliberately **not** `follow`/`path`/`oscillate`, which are gameplay movement (consumer-owned:
mutate components via `fx:set` / `world()` from your own loop).

**Floating damage numbers** (the canonical text effect) compose straight from the library — a `text`
component that rises (`velocity` up), fades (`fade`), and self-expires (`lifetime`):

```jsonc
// Register the archetype once. The string is a per-instance override (you pass the resolved value).
{ "name": "damage_number",
  "text": { "text": "", "color": 4294967295, "layer": 1000, "fontSize": 18 },
  "behaviors": [ {"type":"velocity","vx":0,"vy":-40,"drag":0},
                 {"type":"fade","seconds":0.6,"fromAlpha":1,"toAlpha":0},
                 {"type":"lifetime","seconds":0.6} ] }

// On a hit: spawn at the world position with the resolved damage string.
{ "id": "dmg_1", "archetype": "damage_number",
  "transform": { "cx": 300, "cy": 150 }, "text": { "text": "-25" } }
```

### Particle bursts (the `Emitter` component)

An **`Emitter`** spawns fresh particle-**prefab** instances *at the emitter's position*, each launched with a
random velocity: a direction within the cone `[dirDeg ± spreadDeg/2]` at a speed in `[speedMin, speedMax]`. The
randomness is a **deterministic PRNG seeded by the entity id** (persisted across ticks) — reproducible and
unit-testable. It has **two modes**:

- **Burst** (`oneShot:true`, the default) — spawns `count` particles on its next tick, then the (invisible)
  emitter **self-destructs**. Explosions, debris, muzzle flash.
- **Stream** (`oneShot:false`) — emits `ratePerSec` particles/second **every tick** for as long as the entity
  lives, and does **not** self-destruct. Engine trails, smoke, exhaust. Stop it by setting `ratePerSec:0` or
  destroying the entity (in-flight particles just live out their own lifetime, so a trail fades naturally). The
  steady-state particle count is **self-bounded by the particle's lifetime** (`rate × lifetime`) — keep those
  modest; there's no artificial cap.

| Field | Meaning |
|-------|---------|
| `prefab` | the particle template to instantiate per particle (its sprite + `fade`/`lifetime` live here) |
| `count` | *burst mode:* how many particles the burst spawns |
| `ratePerSec` | *stream mode:* particles emitted per second |
| `speedMin`, `speedMax` | per-particle launch speed range (px/s) |
| `spreadDeg` | full cone width in degrees (`360` = omni-directional) |
| `dirDeg` | cone centre direction (`0` = +x, `90` = +y / screen-down) |
| `oneShot` | `true` = burst (self-destructs) · `false` = continuous stream |

```jsonc
// 1) A particle template — a spark that fades out and dies over 0.4 s (the emitter adds the launch velocity).
{ "name": "spark",
  "sprite": { "asset": "fx/spark", "layer": 900 },
  "behaviors": [ {"type":"fade","seconds":0.4}, {"type":"lifetime","seconds":0.4} ] }

// 2) An explosion archetype that CARRIES the emitter — spawn it and it bursts on the next tick.
{ "name": "explosion",
  "emitter": { "prefab":"spark", "count":24, "speedMin":80, "speedMax":180, "spreadDeg":360 } }

// 3) Boom at a hit location.
{ "id": "boom_1", "archetype": "explosion", "transform": { "cx": 400, "cy": 300 } }
```

**A comet = one entity that is Sprite (head) + a stream Emitter (trail) + `move` + `lifetime`.** It flies,
drops trail particles at its moving position, and self-cleans:

```jsonc
// A dim, fast-fading trail particle.
{ "name": "trail_dust", "sprite": { "asset":"fx/dust", "layer":850 },
  "behaviors": [ {"type":"fade","seconds":0.6}, {"type":"lifetime","seconds":0.6} ] }

// The comet — a bright head that moves and continuously emits the trail behind it, then expires.
{ "id": "comet_1",
  "transform": { "cx": 100, "cy": 200 },
  "sprite":  { "asset":"fx/dot", "layer":950 },
  "emitter": { "prefab":"trail_dust", "ratePerSec":70, "oneShot":false, "speedMax":25, "spreadDeg":360 },
  "behaviors": [ {"type":"move","vx":300,"vy":40}, {"type":"lifetime","seconds":3} ] }
```
(In C++: `world().setEmitter(id, fx::streamEmitter("trail_dust", 70.f, 0.f, 25.f))` — factories
`burstEmitter` / `streamEmitter` for readable call sites.)

> ⚠️ Particles are short-lived **sprite** effects — they ride `render:sprite:*` (reusing the retained diff + the
> behavior library), **not** the renderer's `render:particle` primitive. This is sized for VFX **bursts** (tens
> of particles). For **GPU-scale** particle counts (thousands, sustained) use `submitParticleBatch` directly
> (see [Bulk Sprite Submission](#bulk-sprite-submission-high-throughput)) — routing that through here would
> rebuild the per-primitive dispatch wall the bulk path exists to avoid.

### Prefabs / archetypes (define once, spawn everywhere)

A **prefab** is a reusable effect template — the biggest reuse lever (a shared `explosion` / `muzzle_flash` /
`debris` definition). Register it once, then spawn instances with per-instance overrides:

```jsonc
// fx:prefab — a reusable archetype (no effect spawned yet)
{ "name": "explosion",
  "sprite": { "asset": "fx/blast", "layer": 900 },
  "behaviors": [ {"type":"spin","degPerSec":120}, {"type":"lifetime","seconds":0.6} ] }

// fx:spawn — instantiate it at a hit location; sprite + behaviors are inherited
{ "id": "hit_42", "archetype": "explosion", "transform": { "cx": 400, "cy": 300 } }
```

Each instance is a **deep copy** (fresh behavior state — two explosions expire independently). The spawn's
`transform`/`sprite` **merge** on top of the prefab's; its `behaviors` **add** to the prefab's. An unknown
archetype falls back to a plain empty spawn (fail soft).

**Wiring (static-link host, e.g. Drifterra):** link `FxModule_static`, then either push `fx:*` topics **or**
drive the world directly through the C++ API and call `process(dt)` each frame to tick + emit:

```cpp
#include "FxModule.h"
using namespace grove;

auto fxmod = std::make_unique<FxModule>();
JsonDataNode cfg("config");
fxmod->setConfiguration(cfg, fxIO, nullptr);          // subscribes fx:*

// Author directly in C++ (no topics needed):
auto& w = fxmod->world();
fx::EntityId spark = w.spawn();
w.setSprite(spark, {true, "fx/spark", 0, 0xFFFFFFFFu, 900});
w.setTransform(spark, {400.0f, 300.0f});              // cx,cy = CENTER
w.addBehavior(spark, fx::spin(45.0f));
w.addBehavior(spark, fx::lifetime(0.8f));             // engine ticks it, then removes it

// per frame: publish any fx:* on a peer IIO, then
JsonDataNode in("input"); in.setDouble("deltaTime", dt);
fxmod->process(in);   // drain -> tick(dt) -> emit render:sprite:*
```

Build with `-DGROVE_BUILD_FX_MODULE=ON` (SDL-free). The pure logic is locked by `FxWorldUnit`
(`[prefab]` / `[fade]` / `[velocity]` / `[text]` / `[emitter]` cases included); the module end-to-end by `IT_059`
(a spawn → `render:sprite:add` at center, a partial set → `:update`, an archetype spawn with an override,
`move`+`lifetime` driving a sprite to its `:remove`, `fade`+`velocity` ramping alpha while drifting, a
`damage_number` archetype → `render:text:*` that rises/fades/expires, an `Emitter` burst → a batch of particle
`render:sprite:add`, and a continuous stream emitter → a moving trail that keeps emitting, drops particles at
the moved position, and stops on `ratePerSec:0`). **Hot-reload** round-trips the full live world through
`getState`/`setState` (entities verbatim with their ids + internal behavior/emitter state, prefabs, and the
string-id map) so effects resume mid-flight and the renderer's sprites never orphan (`IT_059h`).

---

## Save / Load (`grove::save::SaveFile`)

Persist a whole game to disk and resume it. A save is a versioned container of `{ moduleName -> that module's
serialized state }`, built on the **same per-module `getState()`/`setState()` contract as hot-reload** — so a
module that hot-reloads correctly also saves/loads correctly. Two ways to drive it:

**Whole-engine (one call saves everything):**
```cpp
DebugEngine engine;                       // ... register your modules, run ...
engine.saveState("save1.json");           // captures every registered module's getState() -> disk
// later, in a fresh engine with the same modules registered:
engine.loadState("save1.json");           // applies each saved state via setState()
```
Call `saveState`/`loadState` **between frames** (not during `step()`) — `getState()` must not race a module's
`process()`. Covers **all hosting strategies**: SEQUENTIAL via the live module, THREADED / THREAD_POOL via a
thread-safe snapshot taken under the module's `processMutex` (so it can't race a worker). Modules absent from a
save keep their state; a saved module no longer registered is ignored (the game evolved). A corrupt saved state
that makes a module's `setState()` throw is caught + logged per module — it never aborts the whole load.

---

## Headless frame capture — assert what the player would SEE

Your game's HUD is not verified until something asserts its pixels. `BgfxRendererModule::setCaptureTarget`
redirects the renderer's **final output** into a CPU-readable target, so a test can publish topics,
step one frame, and check colours — no screen, no human eye.

```cpp
rhi::FramebufferHandle fb =
    renderer.getDevice()->createFramebuffer(W, H, rhi::TargetFormat::RGBA8);

publishMyHud();                       // render:rect / render:text with space:"screen"
renderer.setCaptureTarget(fb);
renderer.process(input);              // one frame, straight into `fb`
renderer.setCaptureTarget({});        // release — see the warning below

std::vector<uint8_t> rgba(size_t(W) * H * 4);
REQUIRE(renderer.getDevice()->readFramebuffer(fb, rgba.data(), uint32_t(rgba.size())));
// rgba is RGBA8, origin top-left: assert your panel's colour where you drew it.
```

**Why an API and not just `setViewFramebuffer(0, fb)`.** Because that is wrong, silently. The set of
views that write the screen **depends on which effects are active**: without lighting the world view
goes to the screen; with lighting it goes into the scene target and the *composite* comes out; with
post-processing the *present* pass does. Binding "view 0" therefore captures a **black world** as
soon as a game turns lighting on — while **the HUD still looks right**, so a HUD test passes and lies
about the scene. Only the module knows the answer, because it computes the submission order.

> ⚠️ **The redirection is PERSISTENT.** The module re-applies it every frame — that is what makes it
> survive a target rebuild — so it **overrides any view binding you do by hand** until you release it
> with an invalid handle. Capturing and then running another render (an export, a poster) without
> releasing sends the second render into the first target. The only symptom is a uniform image.

> ⚠️ It does not resize anything: a target smaller than the window captures a cropped image.
> `shutdown()` releases the redirection as a safety net — a view left bound to a destroyed
> framebuffer corrupts the heap at teardown rather than failing cleanly.

Locked by `FrameCaptureGpu`, which asserts the world **and** the HUD with lighting both off and on —
the discrimination matters, since the wrong implementations keep the HUD correct. That test also uses
these exact three calls, so the snippet above cannot rot silently: the API cannot change without
breaking it. (It is NOT mirrored in `DocExamples` — that guard deliberately excludes renderer APIs,
which would drag the BgfxRenderer link into a compile-only test.)

## Diagnostics — crash reports, leaks, profiling

The engine ships a diagnostics layer for finding problems in dev AND in a shipped build. All of it is
**opt-in / debug-gated** (zero cost in a lean shipping build — see *Debug vs Shipping build*).

### Crash reporter — a minidump + engine context on any crash
`DebugEngine::initialize()` installs a process-wide crash handler (flag `GROVE_CRASH_REPORTER`, ON by
default; skipped under sanitizers). On an unhandled crash it writes two files next to each other:
`<base>.dmp` (a native minidump — stack/registers) and `<base>.json` (a **CrashContext**: engine clock,
frame count, module list, and **the last 200 IIO messages** — the event trail that led to the fault, the
killer artifact for a message-bus engine).

```cpp
DebugEngine engine;
engine.setCrashOutputBase("logs/crash");   // -> logs/crash.dmp + logs/crash.json (call BEFORE initialize)
engine.initialize();                        // crash reporter installed here
// ... run. On a crash, the two files are written automatically. ...

// You can also grab the context WITHOUT crashing (e.g. for a non-fatal error report):
grove::crash::CrashContext ctx = engine.snapshotCrashContext("manual");
std::string report = grove::crash::toJson(ctx).dump(2);   // the same JSON a crash would write
```
Enable the IIO message trail by turning the replay sink on: `IntraIOManager::getInstance().enableReplaySink(200)`.

### Memory leak tracking — which grove allocation leaked, by tag
`grove::mem::Tracker` records tagged allocations and reports what's still live (i.e. leaked) grouped by
tag. It is NOT a global `operator new` override — grove tags its own hot allocations (e.g. IIO nodes as
`"iio:jsonnode"`) via `GROVE_MEM_TRACK_ALLOC/_FREE`, which are **no-ops unless you build with
`-DGROVE_MEM_TRACKING=ON`** (a QA/leak-hunt build). Drive the tracker directly for your own allocations:

```cpp
#include <grove/mem/Tracker.h>
grove::mem::Tracker t;
t.onAlloc(ptr, size, "my:pool");     // at your allocation site
t.onFree(ptr);                        // at the matching free
// ... at a checkpoint or shutdown:
auto report = t.report();             // {"grove_mem":{liveCount, liveBytes, byTag:{...}}}
auto leaks  = t.liveBytesByTag();     // e.g. {"my:pool": 4096} = 4 KB never freed under that tag
```

### Profiler zones — where per-frame time goes
`GROVE_PROFILE_ZONE("name")` times the enclosing scope into a named zone (debug-only; compiled out in a
shipping build). The engine already instruments `step()` — read `engine:step` (whole frame) and
`engine:iopump` (IIO delivery) for a breakdown; add your own zones anywhere:

```cpp
#include <grove/profile/ProfileZone.h>
void MyModule::process(const IDataNode& in) {
    GROVE_PROFILE_ZONE("mymodule:process");   // times this scope
    // ... heavy work ...
}
// each frame (or per second) read + reset the rolling view:
auto profile = grove::profile::profiler().report();   // {"grove_profile":{"engine:step":{seconds,count}, ...}}
grove::profile::profiler().reset();
```

### IIO threading contract (one owning thread per instance)
An `IntraIO` instance is **single-owning-thread**: each module owns one instance driven by one worker. Two
threads touching one instance (e.g. both `publish()`ing, or draining it from N threads) is a data race. A
**debug tripwire** in `publish()`/`pullAndDispatch()` catches a violation loudly (an error log +
`grove::detail::accessViolationCount()`) instead of letting it corrupt silently. For concurrency, give each
thread its OWN instance — distinct instances route to each other concurrently and safely.

A runnable reference exercising all of the above (prints each report): `tests/integration/test_diagnostics_demo.cpp`.

**Direct-drive (you own the module objects — e.g. a static-link host):**
```cpp
#include <grove/save/SaveFile.h>
using namespace grove;

save::SaveFile sf;
sf.captureModule("fleet",   fleetModule);       // = capture("fleet", fleetModule.getState())
sf.captureModule("economy", economyModule);
sf.save("save1.json");                          // {"grove_save":{formatVersion,savedAtUnixMs,modules:{...}}}

save::SaveFile loaded;
if (loaded.load("save1.json")) {                // fail-soft: false on missing/malformed/future-version
    loaded.restoreInto("fleet",   fleetModule); // builds a host-owned node, calls fleetModule.setState()
    loaded.restoreInto("economy", economyModule);
}
```

`SaveFile` is header-only and pure (no engine coupling), and **cross-DLL-safe**: `capture()` deep-copies the
state JSON immediately (never holding an `IDataNode` a hot-loaded module returned — its vtable lives in the
module DLL), and `restoreInto()` builds a host-owned node before `setState()`. So a save survives a module
reload/unload (the same reason `ModuleLoader::reload()` re-homes state). Locked by `SaveFileUnit` (round-trip,
restore, deep-copy-survives-source, fail-soft) + `SaveEngineE2E` (through `DebugEngine::saveState/loadState`).

---

## IIO Topics Reference

### Input Events

Published by **InputModule**, consumed by **UIModule** or **game logic**.

#### Mouse

| Topic | Payload | Description |
|-------|---------|-------------|
| `input:mouse:move` | `{x: double, y: double}` | Mouse position (screen coords) |
| `input:mouse:button` | `{button: int, pressed: bool, x: double, y: double}` | Mouse click (0=left, 1=middle, 2=right) |
| `input:mouse:wheel` | `{delta: double}` | Mouse wheel (+up, -down) |

#### Keyboard

| Topic | Payload | Description |
|-------|---------|-------------|
| `input:keyboard:key` | `{scancode: int, pressed: bool, repeat: bool, shift: bool, ctrl: bool, alt: bool}` | Key event (scancode = SDL_SCANCODE_*) |
| `input:keyboard:text` | `{text: string}` | Text input (UTF-8, for TextInput widgets) |

---

### UI Events

Published by **UIModule**, consumed by **game logic**.

| Topic | Payload | Description |
|-------|---------|-------------|
| `ui:click` | `{widgetId: string, x: double, y: double}` | Widget clicked |
| `ui:action` | `{widgetId: string, action: string}` | Button action triggered |
| `ui:value_changed` | `{widgetId: string, value: variant}` | Slider, checkbox, or text input changed |
| `ui:text_submitted` | `{widgetId: string, text: string}` | Text input submitted (Enter key) |
| `ui:hover` | `{widgetId: string, enter: bool}` | Mouse entered/left widget |
| `ui:scroll` | `{widgetId: string, scrollX: double, scrollY: double}` | Scroll panel scrolled |

---

### Rendering Topics

Consumed by **BgfxRenderer**, published by **UIModule** or **game logic**.

#### Anchor convention — `x,y` = corner · `cx,cy` = center (READ THIS)

The field **name carries the anchor** — you never guess or read `SceneCollector`:

| Field | Meaning | Used by |
|-------|---------|---------|
| `x, y` | **top-left CORNER** | `render:rect`, `render:tilemap`, `render:text`, `render:debug:*` (+ `render:camera` = world coord at the viewport top-left) |
| `cx, cy` | **CENTER** | `render:sprite` (+ `:add`/`:update`), `render:particle`, `render:sector` |

`rotation` always pivots around the box **center**, whichever anchor positioned it.

> ⚠️ **Breaking (2026-07):** `render:sprite`/`:add`/`:update` and `render:particle` used to take `x,y` as the
> center. They now require **`cx,cy`**; a legacy `x,y` (without `cx,cy`) is **rejected** — the primitive is
> dropped and a one-shot error logged, never silently shifted by half a footprint. **New draw primitives MUST
> follow this rule.** Rationale + audit: [`docs/design/render-anchor-convention.md`](design/render-anchor-convention.md).

#### Sprites

**Retained Mode (UIModule current):**

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite:add` | `{renderId, cx, cy, scaleX, scaleY, color, textureId, layer, asset?}` | Register new sprite (retained). `cx,cy` = CENTER (legacy `x,y` rejected) |
| `render:sprite:update` | `{renderId, cx, cy, scaleX, scaleY, color, textureId, layer, asset?}` | Update existing sprite. `cx,cy` = CENTER |
| `render:sprite:remove` | `{renderId}` | Unregister sprite |

**Immediate Mode (legacy, still supported):**

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite` | `{cx, cy, scaleX, scaleY, rotation, u0, v0, u1, v1, color, textureId, layer, space?, asset?}` | Render single sprite (ephemeral). `cx,cy` = CENTER (legacy `x,y` rejected — see anchor convention above). `space:"screen"` → HUD overlay (see below) |
| `render:sprite` `blend` | `"alpha"` (default) / `"additive"` | Optional. Additive makes overlapping sprites BRIGHTEN — a glowing stretched quad, which neither alpha sprites nor square particles could draw. Absent = historical behaviour bit for bit. |
| `render:rect` | `{x, y, w, h, color, layer, space?}` | Filled colored quad, top-left coords. A **layered** sprite-pass quad (honors `layer`, drawn before text) — use for HUD backgrounds. Unlike `render:debug:rect` (always-on-top, unlayered debug overlay). `space:"screen"` → HUD overlay |
| `render:sector` | `{cx, cy, r0, r1, a0, a1, color, layer, space?}` | Filled **ring-sector / pie wedge** (centre cx,cy; inner/outer radius r0/r1, r0=0 = a full pie slice; angles a0..a1 in radians, screen y-down). Drawn as coloured triangles (SectorPass). Reusable for radial menus, cooldown rings, gauges. `space:"screen"` → HUD |
| `render:sprite:batch` | `{stride?, +blob "spriteData"}` **or** `{sprites: [array]}` | Many sprites in ONE message. **Fast path = the packed float blob**: `stride` 8 (default) = `x,y,scaleX,scaleY,rotation,textureId,layer,colorBits`; **`stride` 12 adds `u0,v0,u1,v1` = an ATLAS sub-rect**. Zero node per sprite. The `{sprites:[...]}` child-node form is a FALLBACK — it still allocates one node each. Unknown stride or non-multiple byte length = rejected + logged. See *Bulk sprites* |

#### Asset streaming & runtime textures

Instead of a raw numeric `textureId`, any sprite can reference a texture by a stable **string `asset`
id** — the engine streams it on demand through the `AssetManager` (atlas-aware UVs, VRAM budget +
priority/LRU eviction). Thousands of assets can be *registered* (cheap metadata) while only a budget's
worth stay *resident*. `asset` **wins over** `textureId`/`texture` when both are present.

```cpp
auto s = std::make_unique<JsonDataNode>("sprite");
s->setString("asset", "icons/iron");     // streamed by id (atlas sub-sprite -> its UV rect)
s->setDouble("cx", 100); s->setDouble("cy", 100);   // cx,cy = CENTER (anchor convention)
s->setDouble("scaleX", 32); s->setDouble("scaleY", 32);
s->setInt("layer", 1000);
io->publish("render:sprite", std::move(s));   // also works on render:sprite:add{asset}
```

The same `asset` id is bindable from **UI widgets** (`UIButton`/`UIImage` `asset` prop, literal or
`"{{icon}}"`) — see [UI Widgets](UI_WIDGETS.md). Sprite-as-UI by streamed id is locked by `IT_052`.

**Registry / streaming topics**

| Topic | Payload | Effect |
|-------|---------|--------|
| `asset:register` | `{id, path, priority?, group?}` | register a standalone asset (metadata only — nothing loads yet) |
| `asset:preload` | `{group}` | load a whole group now (highest priority first) |
| `asset:setPriority` | `{id, priority}` | re-prioritise (affects eviction order) |
| `asset:unload` | `{id}` | drop a resident asset |
| `asset:pack` | `{sheet, sprites:[{id,path}], maxWidth?, gutter?, priority?, group?}` | runtime-pack N PNGs into one shared (pinned) sheet |

Assets can also be declared at boot via the `assetManifest` config (a JSON file with `assets` +
`atlases` sections). Config keys (renderer): `assetVramBudgetMB` (default 256), `assetManifest`,
`assetAsyncLoad` (decode off-thread → no first-touch hitch, default `false`), `assetDecodeThreads`
(default 1).

**Runtime textures / painting** — create a texture at runtime and paint colored rects into it,
addressed by the **same string id** as any asset (use it as a sprite/UI `asset`). For procedural
textures, minimaps, paint/mask layers, fog overlays:

| Topic | Payload | Effect |
|-------|---------|--------|
| `render:texture:create` | `{id, width, height, color?}` | create an RGBA8 texture filled with `color` (`0xRRGGBBAA`, default transparent), registered as a **resident** asset by `id` |
| `render:texture:paint` | `{id, x, y, w, h, color}` | fill the sub-rect `[x,y → x+w,y+h]` with `color` — a GPU region update, no full re-upload |

> Full deep-dive (cache/eviction, atlases, async state machine, the bgfx immutability gotcha):
> **[design/assets.md](design/assets.md)**.

#### Text

**Retained Mode (UIModule current):**

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:text:add` | `{renderId, x, y, text, fontSize, color, layer}` | Register new text (retained) |
| `render:text:update` | `{renderId, x, y, text, fontSize, color, layer}` | Update existing text |
| `render:text:remove` | `{renderId}` | Unregister text |

**Immediate Mode (legacy, still supported):**

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:text` | `{x, y, text, fontSize, color, layer, space?}` | Render text (ephemeral). `space:"screen"` → HUD overlay |

**Note:** See [UI Rendering Documentation](UI_RENDERING.md) for details on retained mode rendering.

#### HUD / screen-space overlay (`space:"screen"`)

By default every render command lives in **world space** — it zooms and pans with
`render:camera`. For a HUD that stays fixed in pixels while the world zooms (minimap frame,
resource bar, tactical labels), publish `render:rect` / `render:sprite` / `render:text` with
**`space: "screen"`**:

```cpp
auto bar = std::make_unique<JsonDataNode>("rect");
bar->setDouble("x", 0); bar->setDouble("y", 0);
bar->setDouble("w", 1280); bar->setDouble("h", 32);
bar->setInt("color", 0x101820C0);
bar->setInt("layer", 0);
bar->setString("space", "screen");      // <-- fixed; ignores camera zoom/pan
io->publish("render:rect", std::move(bar));
```

- Screen-space commands draw on a **second view (view 1)** with a fixed screen-space ortho
  (1px = 1 unit, top-left origin), composited **on top of** the world. Coordinates are
  literal pixels — no need to undo the camera.
- The world camera (`render:camera`) can zoom/pan freely; the HUD never moves. This is what
  makes a continuous system↔tactical zoom keep a stable HUD.
- Scope: **ephemeral** topics only (`render:rect`/`:sprite`/`:text`). Retained-mode
  (`render:*:add`) screen-space is not yet supported. `render:debug:*` is always world-space.

#### Tilemap

GPU tilemap renderer — **1 draw call per chunk** (R16UI index texture + `usampler2D` + `texelFetch`),
so cost is independent of tile count. Seamless continuous zoom (detail↔LOD crossfade driven by
screen-space derivatives), `texture2DArray` atlas (one tile type per layer, no edge bleeding), and
optional fog-of-war. Shipped and **verified headless** by `[gpu]` readback tests (pixel asserts) — not
"to verify".

Two modes:
- **Ephemeral** (`render:tilemap`) — re-sent every frame, re-uploaded every frame. Simple, for small or
  throwaway maps.
- **Retained** (`render:tilemap:add/update/remove`, keyed by a non-zero `id`) — uploaded **once**, then
  patched in place. **Use this for the game world** (a static 256×256 chunk uploads exactly once).

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:tilemap:tileset` | `{textureId, tileW, tileH, path}` **or** `{textureId, tileW, tileH, imgW, imgH, +blob "pixels"}` | **Bind a tileset** to `textureId`: slices a `tileW×tileH` grid into a texture2DArray (tile id `T` → layer `T-1`; id `0` = transparent). Source is either a **PNG on disk** (`path`) or **already-decoded RGBA8 pixels** (`pixels` blob, exactly `imgW*imgH*4` bytes, R,G,B,A per texel) — the blob spares a game that *generates* its tileset at startup from encoding a PNG and writing it to disk. Given both, **`pixels` wins** (explicit data over an indirection) and a warning is logged; a size mismatch **fails** rather than uploading a partial atlas. ⚠️ `imgW`/`imgH`, not `w`/`h` as in `render:texture:upload` — `tileW`/`tileH` already live on this topic, so bare `w`/`h` would read ambiguously. Also sets the **zoom-out colours** for free: each layer's average colour becomes this tileset's LOD table, so dezoomed tiles show the tileset's own colours instead of the built-in 8-colour palette (order-independent — binding it after the chunks re-bakes them) |
| `render:tilemap:palette` | `{textureId?, colors:<blob RGBA8>}` | **Override the zoom-out colour table** for a tileset — 4 bytes per entry (R,G,B,A), entry `i` = tile id `i+1`. For a game whose tile colours are pure data with no art to average; the tileset above is the normal path. `textureId` defaults to `0` (the procedural atlas). An id beyond the table renders **transparent** (never wrapped). Publish nothing → the built-in palette, unchanged |
| `render:tilemap` | `{x, y, width, height, tileW, tileH, textureId, tileData}` | Ephemeral chunk (re-uploaded each frame) |
| `render:tilemap:add` | `{id, x, y, width, height, tileW, tileH, textureId, tileData, fogData?, layers?}` | Retained chunk by `id` (≠0) — upload-once. `layers?` = **multi-layer** (see below) |
| `render:tilemap:update` | full: `{id, tileData, fogData?}` · partial: `{id, x, y, w, h, tileData}` | Update a retained chunk (see *Update semantics*) |
| `render:tilemap:remove` | `{id}` | Drop a retained chunk |
| `render:tilemap:fog` | `{id, x, y, w, h, fogData}` | **Partial fog-of-war reveal** — patch only the `w×h` visibility block at tile `(x,y)` into a retained chunk's fog mask. Tiles are **not** touched and the LOD is **not** re-baked — only the fog mip-0 sub-rect re-uploads. `fogData` = `w*h` bytes `0..255`, row-major (255 = visible). First fog on a chunk starts fully **visible** (255), so a patch only reveals/hides its own rect |
| `render:tilemap:anim` | `{tileId, frames, fps}` | Declare an **animated tile** (water/lava): `tileId` cycles through `frames` CONSECUTIVE atlas layers (from its base layer `id-1`) at `fps`. The index texture is unchanged — the shader offsets the layer by time, so animation costs **zero per-frame upload**. `frames ≤ 1` stops it. Up to 4 animated types. The game arranges the frames as consecutive layers in its tileset |

**Fields**
- `x, y` (double) — chunk origin in **world** coords (top-left corner). *Not* a chunk index.
- `width, height` (int) — grid size in **tiles**.
- `tileW, tileH` (int, default 16) — tile size in **pixels**.
- `textureId` (int) — tileset id (resolved to an atlas array). Tile id `N` → atlas layer `N-1`; id `0` =
  empty/transparent.
- `tileData` (string) — comma-separated tile ids, **row-major**. (Alternative: a `tiles` child node, one
  child per tile with an int `v`.)
- `fogData` (string, optional) — comma-separated per-tile visibility `0..255` (255 = visible, 0 = hidden
  → fog). Empty = no fog. Stored as an R8 mask and multiplied into the tile color. Reveal incrementally
  with `render:tilemap:fog` (above) — no tile re-upload.
- `layers` (array, optional) — **multi-layer chunk** (Strategy A). An array of `{tileData (or a `tiles`
  child), textureId?}`, read **by index** = compositing order. **Layer 0** is the opaque base terrain
  (and also drives the legacy `tiles`/`textureId`/LOD path); **layers > 0** are alpha-blended overlays/
  decals drawn back-to-front (tile id `0` = transparent, skipped). Each layer is its own index + LOD;
  the fog mask is shared. Retained chunks only.

```jsonc
// A grass base with a sparse road overlay on top.
{ "id": 1, "x": 0, "y": 0, "width": 64, "height": 64, "tileW": 16, "tileH": 16,
  "layers": [
    { "tileData": "1,1,1,...", "textureId": 10 },   // layer 0 = terrain (opaque)
    { "tileData": "0,0,5,...", "textureId": 11 }     // layer 1 = road decals (id 0 = nothing)
  ] }
```

**Update semantics** (`render:tilemap:update`)
- **Full replace** — `{id, tileData}` (+ optional `fogData`): replaces the whole grid. **Geometry is fixed**
  at `:add` time — to change dims/origin, `:remove` then `:add`.
- **Partial patch** — `{id, x, y, w, h, tileData}` with `w>0 && h>0`: writes a `w×h` block of ids at tile
  offset `(x, y)` (row-major); only that sub-rect is re-uploaded. ⚠️ Here `x, y, w, h` are in **tile units
  within the grid**, *not* world coords.

> ⚠️ **Migrating from a `chunkX / chunkY / tileSize / layer` shape:** the renderer takes `x,y` (world),
> `width/height` (tiles), and `tileW/tileH` (px) — there is **no** `chunkX`, `tileSize`, or `layer` field.
> Tilemaps draw on the world view (camera-driven, so they zoom/pan with `render:camera`); they are not
> ordered by a `layer` field.

#### Particles

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:particle` | `{cx, cy, velocityX, velocityY, color, lifetime, textureId, layer}` | Render particle. `cx,cy` = CENTER (legacy `x,y` rejected) |

#### Camera

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:camera` | `{x, y, zoom, rotation?, viewportX, viewportY, viewportW, viewportH}` | Set camera transform. `x,y` = world coord at the viewport **top-left** (not center); `screen = zoom·(world−cam)`. `rotation` (radians, default 0) rolls the view around the **screen centre** (0 = unchanged). See the [camera helper](#camera-helper--grovecamera-seamless-zoompan) (`Scene/Camera.h`) + [ZoneNavigator](#nested-zones-navigation--grovecamerazonenavigator-scenezonenavigatorh) |

#### Lighting

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:ambient` | `{color}` | Global ambient term. **Absent or 0 = lighting entirely OFF** (no offscreen targets, no composite, output byte-identical to a build without lighting). A **white** ambient leaves the scene unchanged and lets lights only ever brighten it. |
| `render:light` | `{cx, cy, radius, color, intensity?, dirDeg?, spreadDeg?}` | One light, **ephemeral** (re-publish each frame). `cx,cy` = CENTRE, `radius` in **world units**. `dirDeg`/`spreadDeg` (degrees, `grove::fx::Emitter` convention, 360 = omni **and the default**) turn it into a cone. Colour alpha ignored; `intensity` may exceed 1 (RGBA16F target keeps the overbright for bloom). Falloff `(1 − d/r)²`, exactly 0 at `radius`. |
| `render:bloom` | `{intensity, threshold?, radius?}` | Post-processing bloom, **PERSISTENT** (a setting, like the ambient — published once, honoured every frame). `intensity` is the switch: **0 = off and that is the default**, so nothing is built and the composite writes straight to the backbuffer. `threshold` (default 1.0) is the luminance above which a pixel glows — only overbright, which is why the targets are RGBA16F; 0 = everything glows. `radius` (default 16) is the glow extent in **screen pixels**. Soft knee at half the threshold (not a knob). ⚠️ **Requires lighting active** (it feeds on the COMPOSITED frame, so a white ambient is the neutral way to get post-processing without a lit look). Additive sprites glow too, not just lamps. The HUD does not glow. |

| `render:occluder` | `{x, y, w, h}` | Opaque rectangle light does not pass through, **ephemeral**. `x,y` = top-left CORNER (a rect's anchor), unlike a light's `cx,cy`. A non-positive extent is dropped. |
| `render:occluder:add` / `:update` / `:remove` | `{renderId, x?, y?, w?, h?}` | **Retained** occluder — the right form for static level geometry. `:update` merges, so a moving wall need not restate its extent. |
| `render:filter` | `{x, y, w, h, color, opacity?}` | Rectangle that **tints** the light crossing it instead of blocking it, **ephemeral**. `x,y` = top-left CORNER. `color` = the tint after ONE perpendicular crossing of `min(w,h)`; its alpha byte is ignored (`opacity`, default 1, is the knob). Overlapping filters compose by product, in any order. A non-positive extent is dropped. |
| `render:filter:add` / `:update` / `:remove` | `{renderId, x?, y?, w?, h?, color?, opacity?}` | **Retained** filter — for static stained glass. `:update` merges, and **re-derives the tint** if the pane is resized. |
| `render:fog` | `{x, y, w, h, density, color?, scatter?}` | Absorbing (and optionally scattering) volume, **ephemeral**. `x,y` = top-left CORNER. `density` is the Beer-Lambert **α**, unbounded and per-unit — NOT an opacity. `color` (white = neutral) makes absorption selective. `scatter` (0..1, default 0) re-emits crossing light **additively**, which is what makes a beam visible in the void. |
| `render:fog:add` / `:update` / `:remove` | `{renderId, x?, y?, w?, h?, density?, color?, scatter?}` | **Retained** rectangular medium. `:update` merges and re-derives. |
| `render:nebula` | `{cx, cy, radius, density, color?, scatter?}` | Soft RADIAL medium, **ephemeral**. `cx,cy` = CENTRE (a disc, not a rect). Density peaks at the core and reaches exactly zero at the rim, so the bounding quad is invisible — overlap several for an organic cloud. Same `density` units as `render:fog`. |

See [2D lighting](#2d-lighting--ambient--radial-lights) for the full guide, and
`include/grove/light/Light.h` for the same falloff as plain C++ (gameplay "is this point lit?").

#### Clear

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:clear` | `{color: int}` | Set clear color (RGBA) |

#### Debug

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:debug:line` | `{x1, y1, x2, y2, color}` | Draw debug line |
| `render:debug:rect` | `{x, y, w, h, color, filled}` | Draw debug rectangle |

### FX / Effects Topics

Consumed by **FxModule** (ephemeral VFX only — NOT gameplay crowds; those use `submitSpriteBatch`), which
turns them into `render:sprite:*`. Full guide + the behavior library + prefabs:
[Effects / FX Layer](#effects--fx-layer-grovefx--fxmodule).

| Topic | Payload | Description |
|-------|---------|-------------|
| `fx:prefab` | `{name, transform?, sprite?, behaviors?}` | Register a reusable archetype/template |
| `fx:spawn` | `{id, archetype?, transform?, sprite?, behaviors?}` | Spawn an effect (optionally from a prefab, with overrides). `transform.cx,cy` = CENTER |
| `fx:set` | `{id, transform?, sprite?}` | Partial update — omitted fields keep their value |
| `fx:destroy` | `{id}` | Remove the effect |

Behaviors: `{"type":"move","vx","vy"}` · `{"type":"spin","degPerSec"}` · `{"type":"lifetime","seconds"}`.

---

## Complete Application Example

### Directory Structure

```
MyGame/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   └── modules/
│       ├── GameLogic.h
│       └── GameLogic.cpp
├── assets/
│   ├── ui/
│   │   └── main_menu.json
│   └── sprites/
│       └── player.png
└── external/
    └── GroveEngine/  # Git submodule
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyGame VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GroveEngine + Modules
add_subdirectory(external/GroveEngine)
set(GROVE_BUILD_BGFX_RENDERER ON CACHE BOOL "" FORCE)
set(GROVE_BUILD_UI_MODULE ON CACHE BOOL "" FORCE)
set(GROVE_BUILD_INPUT_MODULE ON CACHE BOOL "" FORCE)

# Main executable
add_executable(mygame src/main.cpp)
target_link_libraries(mygame PRIVATE
    GroveEngine::impl
    SDL2::SDL2
    spdlog::spdlog
)

# Game logic module
add_library(GameLogic SHARED
    src/modules/GameLogic.cpp
)
target_link_libraries(GameLogic PRIVATE
    GroveEngine::impl
    spdlog::spdlog
)
set_target_properties(GameLogic PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
)
```

### main.cpp

```cpp
#include <grove/ModuleLoader.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>
#include <SDL2/SDL.h>
#include <iostream>

int main(int argc, char* argv[]) {
    // Initialize SDL
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    SDL_Window* window = SDL_CreateWindow("MyGame", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1920, 1080, SDL_WINDOW_SHOWN);

    // Get native window handle
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    void* nativeHandle = nullptr;
#ifdef _WIN32
    nativeHandle = wmInfo.info.win.window;  // HWND
#elif __linux__
    nativeHandle = (void*)(uintptr_t)wmInfo.info.x11.window;
#endif

    // Create IIO instances
    auto& ioManager = grove::IntraIOManager::getInstance();
    auto rendererIO = ioManager.createInstance("renderer");
    auto uiIO = ioManager.createInstance("ui");
    auto inputIO = ioManager.createInstance("input");
    auto gameIO = ioManager.createInstance("game");

    // Load modules
    grove::ModuleLoader rendererLoader, uiLoader, inputLoader, gameLoader;

    auto renderer = rendererLoader.load("./modules/BgfxRenderer.dll", "renderer");
    auto uiModule = uiLoader.load("./modules/UIModule.dll", "ui");
    auto inputModule = inputLoader.load("./modules/InputModule.dll", "input");
    auto gameModule = gameLoader.load("./modules/GameLogic.dll", "game");

    // Configure BgfxRenderer
    grove::JsonDataNode rendererConfig("config");
    rendererConfig.setInt("windowWidth", 1920);
    rendererConfig.setInt("windowHeight", 1080);
    rendererConfig.setString("backend", "auto");
    rendererConfig.setInt("nativeWindowHandle", (int)(intptr_t)nativeHandle);
    renderer->setConfiguration(rendererConfig, rendererIO.get(), nullptr);

    // Configure UIModule
    grove::JsonDataNode uiConfig("config");
    uiConfig.setInt("windowWidth", 1920);
    uiConfig.setInt("windowHeight", 1080);
    uiConfig.setString("layoutFile", "./assets/ui/main_menu.json");
    uiConfig.setInt("baseLayer", 1000);
    uiModule->setConfiguration(uiConfig, uiIO.get(), nullptr);

    // Configure InputModule
    grove::JsonDataNode inputConfig("config");
    inputConfig.setString("backend", "sdl");
    inputModule->setConfiguration(inputConfig, inputIO.get(), nullptr);

    // Configure GameLogic
    grove::JsonDataNode gameConfig("config");
    gameModule->setConfiguration(gameConfig, gameIO.get(), nullptr);

    // Main loop
    bool running = true;
    Uint64 lastTime = SDL_GetPerformanceCounter();

    while (running) {
        // 1. Handle SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            inputModule->feedEvent(&event);  // Feed to InputModule
        }

        // 2. Calculate deltaTime
        Uint64 now = SDL_GetPerformanceCounter();
        double deltaTime = (now - lastTime) / (double)SDL_GetPerformanceFrequency();
        lastTime = now;

        // 3. Process all modules
        grove::JsonDataNode input("input");
        input.setDouble("deltaTime", deltaTime);

        inputModule->process(input);   // Input → IIO messages
        uiModule->process(input);      // UI → IIO messages
        gameModule->process(input);    // Game logic
        renderer->process(input);      // Render frame

        // 4. Optional: Hot-reload check
        // (file watcher code here)
    }

    // Cleanup
    renderer->shutdown();
    uiModule->shutdown();
    inputModule->shutdown();
    gameModule->shutdown();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
```

### GameLogic.cpp

```cpp
#include <grove/IModule.h>
#include <grove/JsonDataNode.h>
#include <grove/IIO.h>
#include <spdlog/spdlog.h>

class GameLogic : public grove::IModule {
public:
    GameLogic() {
        m_logger = spdlog::stdout_color_mt("GameLogic");
    }

    void setConfiguration(const grove::IDataNode& config,
                          grove::IIO* io,
                          grove::ITaskScheduler* scheduler) override {
        m_io = io;

        // Subscribe to UI events with callback handlers
        m_io->subscribe("ui:action", [this](const grove::Message& msg) {
            std::string action = msg.data->getString("action", "");
            if (action == "start_game") {
                startGame();
            }
        });

        m_io->subscribe("ui:click", [this](const grove::Message& msg) {
            std::string widgetId = msg.data->getString("widgetId", "");
            double x = msg.data->getDouble("x", 0.0);
            double y = msg.data->getDouble("y", 0.0);
            handleClick(widgetId, x, y);
        });
    }

    void process(const grove::IDataNode& input) override {
        double deltaTime = input.getDouble("deltaTime", 0.016);

        // Process UI events - pull and auto-dispatch to callbacks
        while (m_io->hasMessages() > 0) {
            m_io->pullAndDispatch();  // Callbacks invoked automatically
        }

        // Update game logic
        if (m_gameStarted) {
            updatePlayer(deltaTime);
            renderPlayer();
        }
    }

    // ... other IModule methods ...

private:
    void startGame() {
        m_gameStarted = true;
        m_playerX = 960.0;
        m_playerY = 540.0;
        m_logger->info("Game started!");
    }

    void updatePlayer(double deltaTime) {
        // Update player position, etc.
    }

    void renderPlayer() {
        // Publish sprite to renderer
        auto sprite = std::make_unique<grove::JsonDataNode>("sprite");
        sprite->setDouble("cx", m_playerX);   // cx,cy = CENTER (anchor convention)
        sprite->setDouble("cy", m_playerY);
        sprite->setInt("textureId", 0);  // Player texture
        sprite->setInt("layer", 10);
        m_io->publish("render:sprite", std::move(sprite));
    }

    std::shared_ptr<spdlog::logger> m_logger;
    grove::IIO* m_io = nullptr;
    bool m_gameStarted = false;
    double m_playerX = 0.0;
    double m_playerY = 0.0;
};

extern "C" {
    grove::IModule* createModule() { return new GameLogic(); }
    void destroyModule(grove::IModule* m) { delete m; }
}
```

---

## Interactive Demo - Try It First!

**Before reading further**, try the full stack interactive demo to see everything in action:

```bash
# Windows
run_full_stack_demo.bat

# Linux
./build/tests/test_full_stack_interactive
```

**What it demonstrates:**
- ✅ BgfxRenderer rendering sprites and text
- ✅ UIModule with buttons, sliders, panels
- ✅ InputModule capturing mouse and keyboard
- ✅ Complete IIO message flow (input → UI → game → render)
- ✅ Hit testing and click detection (raycasting 2D)
- ✅ Game logic responding to UI events

**Interactive controls:**
- Click buttons to spawn/clear sprites
- Drag slider to change speed
- Press SPACE to spawn from keyboard
- Press ESC to exit

**See:** [tests/visual/README_FULL_STACK.md](../tests/visual/README_FULL_STACK.md) for details.

---

## Building Your First Game

### Step-by-Step Tutorial

#### 1. Create Project Structure

```bash
mkdir MyGame && cd MyGame
git init
git submodule add <grove-engine-repo> external/GroveEngine
mkdir -p src/modules assets/ui
```

#### 2. Create CMakeLists.txt

(See [Complete Application Example](#complete-application-example))

#### 3. Create UI Layout

`assets/ui/main_menu.json`:
```json
{
  "widgets": [
    {
      "type": "UIPanel",
      "id": "main_panel",
      "x": 0,
      "y": 0,
      "width": 1920,
      "height": 1080,
      "color": 2155905279
    },
    {
      "type": "UIButton",
      "id": "play_button",
      "x": 860,
      "y": 500,
      "width": 200,
      "height": 60,
      "text": "Play",
      "action": "start_game"
    },
    {
      "type": "UIButton",
      "id": "quit_button",
      "x": 860,
      "y": 580,
      "width": 200,
      "height": 60,
      "text": "Quit",
      "action": "quit_game"
    }
  ]
}
```

#### 4. Build and Run

```bash
cmake -B build
cmake --build build -j4
./build/mygame
```

---

## Advanced Topics

### Hot-Reload Workflow

```bash
# Terminal 1: Run game
./build/mygame

# Terminal 2: Edit and rebuild module
vim src/modules/GameLogic.cpp
cmake --build build --target GameLogic

# Game automatically reloads GameLogic with state preserved!
```

### Performance Optimization

#### Sprite Batching

```cpp
// Instead of publishing 100 individual sprites:
for (auto& enemy : enemies) {
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", enemy.x);
    // ...
    io->publish("render:sprite", std::move(sprite));  // 100 IIO messages
}

// Publish as batch:
auto batch = std::make_unique<JsonDataNode>("batch");
auto sprites = std::make_unique<JsonDataNode>("sprites");
for (auto& enemy : enemies) {
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("cx", enemy.x);
    // ...
    sprites->setChild(enemy.id, std::move(sprite));
}
batch->setChild("sprites", std::move(sprites));
io->publish("render:sprite:batch", std::move(batch));  // 1 IIO message
```

> ⚠️ **The form above is the FALLBACK, not the fast path.** It saves the per-sprite IIO *message* but
> still builds one `JsonDataNode` per sprite — and that allocation is the actual cost. For real
> throughput use the **packed blob** below.

##### The packed blob — the actual fast path

One binary blob, **zero node per sprite**, an `O(N)` `memcpy` into the frame. Floats, tightly packed,
`stride` values per sprite:

| `stride` | Layout |
|---|---|
| **8** (default, omit the field) | `x, y, scaleX, scaleY, rotation, textureId, layer, colorBits` |
| **12** | the eight above **+ `u0, v0, u1, v1`** — an **atlas sub-rect** |

```cpp
std::vector<float> packed;                       // stride 12: sprite i at packed[i*12]
for (const auto& piece : pieces) {
    uint32_t rgba = piece.color;                 // 0xRRGGBBAA
    float colorBits; std::memcpy(&colorBits, &rgba, sizeof(float));   // BITS, not a numeric cast
    packed.insert(packed.end(), {
        piece.cx, piece.cy, piece.w, piece.h, piece.rotation,
        static_cast<float>(piece.textureId), static_cast<float>(piece.layer), colorBits,
        piece.u0, piece.v0, piece.u1, piece.v1                        // the atlas sub-rect
    });
}
auto n = std::make_unique<JsonDataNode>("b");
n->setInt("stride", 12);                         // omit ⇒ 8 ⇒ full-quad UVs (historical behaviour)
n->setBlob("spriteData", reinterpret_cast<const uint8_t*>(packed.data()),
           packed.size() * sizeof(float));
io->publish("render:sprite:batch", std::move(n));
```

- **`cx, cy` = CENTRE**, like every sprite topic. `colorBits` is the `uint32` **reinterpreted** as a
  float — a numeric cast silently mangles it.
- **A flip needs no field**: a flip *is* a UV swap, so publish `u0 > u1` yourself. It composes with
  `rotation` exactly like `flipX` does on the single-sprite path.
- **An unknown `stride`, or a blob whose byte length isn't a multiple of it, is REJECTED** (one-shot
  log, batch dropped). Reading a stride-12 blob as stride 8 does not yield "fewer sprites" — it
  yields *garbage quads at random positions*, which is far harder to diagnose than nothing at all.
- ⚠️ **No `asset` id here.** A blob can't carry strings, so streamed assets must be resolved to a
  numeric `textureId` + explicit UVs by the game. If you need the asset path at bulk scale, say so —
  it isn't built because nobody has asked.
- Even faster, but **static-link only**: `BgfxRendererModule::submitSpriteBatch(SpriteInstance*, n)`
  bypasses IIO entirely (~100k sprites/frame at 60 fps). The blob path is what a module gets without
  linking the renderer.

#### Low-Frequency Subscriptions

```cpp
// For non-critical analytics/logging
grove::SubscriptionConfig config;
config.batchInterval = 1000;  // Batch messages for 1 second
io->subscribeLowFreq("analytics:*", config);
```

### Multi-Module Communication Patterns

#### Request-Response Pattern

```cpp
// Module A: Subscribe to response first (in setConfiguration)
moduleA_io->subscribe("pathfinding:response", [this](const grove::Message& msg) {
    std::string requestId = msg.data->getString("requestId", "");
    // ... apply path result ...
});

// Module A: Request pathfinding (in process)
auto request = std::make_unique<JsonDataNode>("request");
request->setString("requestId", "path_123");
request->setDouble("startX", 10.0);
request->setDouble("startY", 20.0);
moduleA_io->publish("pathfinding:request", std::move(request));

// Module B: Subscribe to request (in setConfiguration)
moduleB_io->subscribe("pathfinding:request", [this](const grove::Message& msg) {
    std::string requestId = msg.data->getString("requestId", "");
    // ... compute path ...

    auto response = std::make_unique<JsonDataNode>("response");
    response->setString("requestId", requestId);
    // ... add path data ...
    m_io->publish("pathfinding:response", std::move(response));
});

// Module A/B: In process() - pull and dispatch
while (io->hasMessages() > 0) {
    io->pullAndDispatch();  // Callbacks invoked automatically
}
```

#### Event Aggregation

```cpp
// Multiple modules publish events
io->publish("combat:damage", damageData);
io->publish("combat:kill", killData);
io->publish("combat:levelup", levelupData);

// Analytics module aggregates all combat events (in setConfiguration)
analyticsIO->subscribe("combat:*", [this](const grove::Message& msg) {
    aggregateCombatEvent(msg);
});

// In process()
while (analyticsIO->hasMessages() > 0) {
    analyticsIO->pullAndDispatch();  // Callback invoked for each event
}
```

### Testing Strategies

#### Headless Testing

```cpp
// Configure renderer in headless mode
JsonDataNode config("config");
config.setString("backend", "noop");  // No actual rendering
config.setBool("vsync", false);
renderer->setConfiguration(config, io, nullptr);

// Run tests without window
for (int i = 0; i < 1000; i++) {
    // Simulate game logic
    renderer->process(input);
}
```

#### Integration Tests

See `tests/integration/IT_014_ui_module_integration.cpp` for complete example.

---

## Troubleshooting

### Common Issues

#### Module not loading

```bash
# Check module exports
nm -D build/modules/GameLogic.so | grep createModule
# Should show: createModule and destroyModule

# Check dependencies
ldd build/modules/GameLogic.so
```

#### IIO messages not received

```cpp
// Verify subscription with callback BEFORE publishing (in setConfiguration)
io->subscribe("render:sprite", [this](const grove::Message& msg) {
    handleSprite(msg);
});

// Check topic patterns
io->subscribe("render:*", [this](const grove::Message& msg) {
    // Matches render:sprite, render:text, etc.
});

io->subscribe("render:sprite:*", [this](const grove::Message& msg) {
    // Only matches render:sprite:batch, render:sprite:add, etc.
});

// Remember to pullAndDispatch in process()
while (io->hasMessages() > 0) {
    io->pullAndDispatch();
}
```

#### Hot-reload state loss

```cpp
// Ensure ALL state is serialized in getState()
std::unique_ptr<IDataNode> MyModule::getState() {
    auto state = std::make_unique<JsonDataNode>("state");

    // DON'T FORGET any member variables!
    state->setInt("score", m_score);
    state->setDouble("playerX", m_playerX);
    state->setDouble("playerY", m_playerY);
    // ...

    return state;
}
```

---

## Additional Resources

- **[USER_GUIDE.md](USER_GUIDE.md)** - Core module system documentation
- **[BgfxRenderer README](../modules/BgfxRenderer/README.md)** - Renderer details
- **[InputModule README](../modules/InputModule/README.md)** - Input details
- **[CLAUDE.md](../CLAUDE.md)** - Development context for Claude Code
- **Integration Tests** - `tests/integration/IT_014_*.cpp`, `IT_015_*.cpp`

---

**GroveEngine - Build modular, hot-reloadable games with ease** 🌳
