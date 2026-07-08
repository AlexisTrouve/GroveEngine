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

## Scene / Entity Layer (`grove::entity` + EntityModule)

A **data-driven authoring surface** for game entities — Unity/Godot-style GameObject + Component, but
**declarative** (data, not code). You compose an entity from **components** and **behaviors**; the engine
ticks the behaviors and emits the render traffic. There's **no scripting language** — a behavior is a fixed
engine primitive with numeric params (same discipline as the [VN conditions](#dialoguemodule) and UI
binding). The point is reuse: the behavior library lives **engine-side**, so every project inherits it, and
**prefabs** let you define an entity template once and spawn it everywhere.

The core is the pure header-only `grove::entity::EntityWorld` (`include/grove/entity/EntityWorld.h` — like
`grove::anim`, no IIO/renderer); `EntityModule` (an `IModule`) wraps it onto the bus.

**The model.** An entity = a stable id + typed **components** + a list of **behaviors**:

- **`Transform`** `{cx, cy, rotation, scaleX, scaleY}` — `cx,cy` = CENTER (the [anchor convention](#anchor-convention--xy--corner--cxcy--center-read-this)).
- **`Sprite`** `{asset | textureId, color, layer}` — what it draws (omit → a logic/transform-only entity).
- **behaviors** — from the fixed library below.

**Topics consumed:**

| Topic | Payload | Effect |
|-------|---------|--------|
| `entity:prefab` | `{name, transform?, sprite?, behaviors?}` | Register a reusable **archetype/template** (see Prefabs) |
| `entity:spawn` | `{id, archetype?, transform?, sprite?, behaviors?}` | Spawn an entity under a string `id`. With `archetype`, instantiate that prefab; the spawn's own fields then override/add on top |
| `entity:set` | `{id, transform?, sprite?}` | **Partial** update — only the fields you send change; the rest keep their value |
| `entity:destroy` | `{id}` | Remove the entity (emits its `render:sprite:remove`) |

**Published:** `render:sprite:add` / `:update` / `:remove` — keyed by the entity's numeric id (= the
renderer's `renderId`), `cx,cy` = CENTER. Each `process(dt)`: drain the inbox → `tick(dt)` (advance
behaviors) → `diffRender()` (emit only what changed — the minimal retained-render traffic).

Robust by design: the JSON accessors **fail soft** to a default and never throw on a malformed payload —
an imperfect message degrades gracefully instead of crashing the engine.

### Behavior library (engine-side, reused across projects)

Behaviors are a **fixed set of primitives** the engine ticks. Compose them on an entity in data:

| `type` | Params | Effect |
|--------|--------|--------|
| `move` | `{vx, vy}` | Translate the center by `v·dt` each frame |
| `spin` | `{degPerSec}` | Rotate (deg/s → rad) |
| `lifetime` | `{seconds}` | Destroy the entity after `seconds` (emits its Remove) |

Behaviors on one entity tick in list order and compose (e.g. `move` + `lifetime` = a bullet). Adding a
reusable behavior = one `Type` + one tick case in `EntityWorld` — **every project gains it** (that's the
multi-project lever). Game-specific logic that isn't reusable stays **consumer-side**: mutate components via
`entity:set` / `world()` from your own loop. (Follow-ons: `follow` / `oscillate` / `path`.)

### Prefabs / archetypes (define once, spawn everywhere)

A **prefab** is a reusable entity template — the biggest reuse lever (a shared `bullet` / `pickup` / `enemy`
definition). Register it once, then spawn instances with per-instance overrides:

```jsonc
// entity:prefab — a reusable archetype (no entity spawned yet)
{ "name": "bullet",
  "sprite": { "asset": "bullet", "layer": 5 },
  "behaviors": [ {"type":"move","vx":300,"vy":0}, {"type":"lifetime","seconds":1.5} ] }

// entity:spawn — instantiate it, overriding only the position; sprite + behaviors are inherited
{ "id": "b1", "archetype": "bullet", "transform": { "cx": 400, "cy": 300 } }
```

Each instance is a **deep copy** (fresh behavior state — two bullets expire independently). The spawn's
`transform`/`sprite` **merge** on top of the prefab's; its `behaviors` **add** to the prefab's. An unknown
archetype falls back to a plain empty spawn (fail soft).

**Wiring (static-link host, e.g. Drifterra):** link `EntityModule_static`, then either push `entity:*`
topics **or** drive the world directly through the C++ API and call `process(dt)` each frame to tick + emit:

```cpp
#include "EntityModule.h"
using namespace grove;

auto entities = std::make_unique<EntityModule>();
JsonDataNode cfg("config");
entities->setConfiguration(cfg, entityIO, nullptr);   // subscribes entity:*

// Author directly in C++ (no topics needed):
auto& w = entities->world();
entity::EntityId ship = w.spawn();
w.setSprite(ship, {true, "ship/hull", 0, 0xFFFFFFFFu, 10});
w.setTransform(ship, {400.0f, 300.0f});               // cx,cy = CENTER
w.addBehavior(ship, entity::spin(45.0f));             // engine ticks it

// per frame: publish any entity:* on a peer IIO, then
JsonDataNode in("input"); in.setDouble("deltaTime", dt);
entities->process(in);   // drain -> tick(dt) -> emit render:sprite:*
```

Build with `-DGROVE_BUILD_ENTITY_MODULE=ON` (SDL-free). The pure logic is locked by `EntityWorldUnit`
(`[prefab]` cases included); the module end-to-end by `IT_059` (a spawn → `render:sprite:add` at center, a
partial set → `:update`, an archetype spawn with an override, and `move`+`lifetime` driving a sprite to its
`:remove`). Hot-reload full-world serialization is a follow-on (`getState` is minimal for now).

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
| `render:rect` | `{x, y, w, h, color, layer, space?}` | Filled colored quad, top-left coords. A **layered** sprite-pass quad (honors `layer`, drawn before text) — use for HUD backgrounds. Unlike `render:debug:rect` (always-on-top, unlayered debug overlay). `space:"screen"` → HUD overlay |
| `render:sector` | `{cx, cy, r0, r1, a0, a1, color, layer, space?}` | Filled **ring-sector / pie wedge** (centre cx,cy; inner/outer radius r0/r1, r0=0 = a full pie slice; angles a0..a1 in radians, screen y-down). Drawn as coloured triangles (SectorPass). Reusable for radial menus, cooldown rings, gauges. `space:"screen"` → HUD |
| `render:sprite:batch` | `{sprites: [array]}` | Render sprite batch (optimized) |

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
| `render:tilemap:tileset` | `{textureId, path, tileW, tileH}` | **Bind a PNG tileset** to `textureId`: loads the image and slices its `tileW×tileH` grid into a texture2DArray (tile id `T` → layer `T-1`; id `0` = transparent). Load **once** before the chunks that reference this `textureId` |
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

#### Clear

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:clear` | `{color: int}` | Set clear color (RGBA) |

#### Debug

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:debug:line` | `{x1, y1, x2, y2, color}` | Draw debug line |
| `render:debug:rect` | `{x, y, w, h, color, filled}` | Draw debug rectangle |

### Entity Topics

Consumed by **EntityModule**, which turns them into `render:sprite:*`. Full guide + the behavior library +
prefabs: [Scene / Entity Layer](#scene--entity-layer-groveentity--entitymodule).

| Topic | Payload | Description |
|-------|---------|-------------|
| `entity:prefab` | `{name, transform?, sprite?, behaviors?}` | Register a reusable archetype/template |
| `entity:spawn` | `{id, archetype?, transform?, sprite?, behaviors?}` | Spawn an entity (optionally from a prefab, with overrides). `transform.cx,cy` = CENTER |
| `entity:set` | `{id, transform?, sprite?}` | Partial update — omitted fields keep their value |
| `entity:destroy` | `{id}` | Remove the entity |

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
