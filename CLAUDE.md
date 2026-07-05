# GroveEngine - Claude Code Context

## Project Overview
GroveEngine is a C++17 hot-reload module system for game engines. It supports dynamic loading/unloading of modules (.so) with state preservation during hot-reload.

## Documentation

**For developers using GroveEngine:**
- **[DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md)** - Complete guide to building applications with GroveEngine (modules, IIO topics, examples)
- **[USER_GUIDE.md](docs/USER_GUIDE.md)** - Core module system, hot-reload, IIO communication basics

**Module-specific:**
- **[BgfxRenderer README](modules/BgfxRenderer/README.md)** - 2D rendering module (sprites, text, tilemap, particles)
- **[InputModule README](modules/InputModule/README.md)** - Input handling (mouse, keyboard, gamepad)
- **[UIModule README](modules/UIModule/README.md)** - User interface system overview

**UIModule Documentation (⚠️ READ BEFORE WORKING ON UI):**
- **[UI Widgets](docs/UI_WIDGETS.md)** - Widget properties, JSON configuration, custom widgets
- **[UI Topics](docs/UI_TOPICS.md)** - IIO topics reference and usage patterns
- **[UI Architecture](docs/UI_ARCHITECTURE.md)** - Threading model, limitations, design principles
- **[UI Rendering](docs/UI_RENDERING.md)** - Retained mode rendering architecture

## Module Systems

| System | Status | Description | Use Case |
|--------|--------|-------------|----------|
| **SequentialModuleSystem** | ✅ Production Ready | Single-threaded, one module at a time | Debug, testing |
| **ThreadedModuleSystem** | ✅ Phase 2 Complete | One thread per module (parallel execution) | Few HEAVY modules (≲ cores) |
| **ThreadPoolModuleSystem** | ✅ Phase 3 Complete | Shared worker pool + work-stealing | MANY lightweight modules (N ≫ cores) |
| **ClusterModuleSystem** | 🚧 Planned (Phase 4) | Distributed across machines | MMO scale |

**Hosting both via the engine**: `registerStaticModule(name, module, ModuleSystemType::THREADED|THREAD_POOL, config)` — one shared system per strategy, archi-A (the worker drains the module's IIO inbox AFTER `process()`). Pick by the benchmark: THREADED for a handful of heavy modules, THREAD_POOL for many light ones. Resume/open tasks (incl. the one remaining rigour gap — TSan on the engine→pool path): **[docs/design/threaded-pool-handoff.md](docs/design/threaded-pool-handoff.md)**.

## Available Modules

| Module | Status | Description | Build Flag |
|--------|--------|-------------|------------|
| **BgfxRenderer** | ✅ Phase 7-8 Complete | 2D rendering (sprites, text, tilemap, particles) | `-DGROVE_BUILD_BGFX_RENDERER=ON` |
| **UIModule** | ✅ Phase 7 Complete | UI widgets (buttons, panels, scrolling, tooltips) | `-DGROVE_BUILD_UI_MODULE=ON` |
| **InputModule** | ✅ Production Ready | Input handling (mouse, keyboard, SDL backend) | `-DGROVE_BUILD_INPUT_MODULE=ON` |
| **SoundManager** | ✅ Slices 1-3 | Music + SFX via `sound:*` (SDL_mixer behind `ISoundBackend`) | `-DGROVE_BUILD_SOUND_MODULE=ON` (needs SDL2_mixer) |

**Header-only helpers** (no module/build flag — `#include` and go): `grove::camera` (zoom/pan/cull, `Scene/Camera.h`), `grove::anim` (2D animation, `include/grove/anim/`), `grove::input::ActionMap` (scancode bindings, `modules/InputModule/ActionMap.h`). See the quick-reference + DEVELOPER_GUIDE.

**Integration:** All modules communicate via IIO topics. See [DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md) for complete IIO topics reference.

## Build & Test
```bash
# Build core only
cmake -B build && cmake --build build -j4

# Build with all modules
cmake -B build -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_INPUT_MODULE=ON -DGROVE_BUILD_SOUND_MODULE=ON
# (SoundManager needs SDL2_mixer on the toolchain; omit -DGROVE_BUILD_SOUND_MODULE if not installed)
cmake --build build -j4

# Run all tests (23+ tests)
cd build && ctest --output-on-failure

# Run visual tests (IMPORTANT: always run from project root for correct asset paths)
./build/tests/test_ui_showcase      # UI showcase with all widgets
./build/tests/test_renderer_showcase # Renderer showcase (sprites, text, particles)

# Build with ThreadSanitizer
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan
cmake --build build-tsan -j4
```

## Architecture

### Key Components
- **ModuleLoader**: Handles dlopen/dlclose of .so modules with hot-reload support
- **SequentialModuleSystem**: Single-threaded module execution for testing
- **IntraIOManager**: Inter-module communication with pub/sub routing
- **TopicTree**: O(k) topic matching with wildcard support

### Thread Safety Patterns
See `docs/coding_guidelines.md` for detailed synchronization guidelines.

**DO:**
```cpp
// Multiple mutexes - use scoped_lock (deadlock-free)
std::scoped_lock lock(mutex1, mutex2);

// Read-heavy data - use shared_mutex
std::shared_lock readLock(sharedMutex);   // concurrent reads
std::unique_lock writeLock(sharedMutex);  // exclusive write
```

**DON'T:**
```cpp
// NEVER nest lock_guard - causes deadlock
std::lock_guard lock1(mutex1);
std::lock_guard lock2(mutex2);  // DEADLOCK RISK
```

### ModuleLoader Usage
- Each `ModuleLoader` instance manages ONE module lifecycle
- Don't reuse loader for multiple independent modules (causes SEGFAULT)
- `reload()` safely handles state extraction and library reload
- `load(path, name, isReload=true)` for hot-reload with cache bypass

## Known Issues
- **TankModule.h linter bug**: A linter sometimes merges lines 35-36. If build fails with "logger not declared", check this file.

## Test Categories
| Test # | Name | Duration | Description |
|--------|------|----------|-------------|
| 1-10 | scenario_* | ~0.01s | Basic scenarios |
| 11 | ProductionHotReload | ~12s | Full hot-reload cycle |
| 12 | ChaosMonkey | ~41s | Stress testing |
| 13 | StressTest | ~180s | Heavy load |
| 15 | MemoryLeakHunter | ~135s | 200 reload cycles |
| 19 | CrossSystemIntegration | ~4s | Multi-system test |

## Module Architecture Quick Reference

### BgfxRenderer
- **RHI Layer**: Abstracts bgfx calls (`RHIDevice.h`, `BgfxDevice.cpp`)
- **RenderGraph**: Topological sort with Kahn's algorithm for pass ordering
- **CommandBuffer**: Records commands, executed by device at frame end
- **Camera helper**: `Scene/Camera.h` (`grove::camera`, header-only) — screen↔world, centerOn/focusOn, `zoomAt` (seamless zoom toward cursor), `damp`, **`isVisible`/`visibleWorldBounds` (cull off-screen)**, `fitBounds`/`clampPanToBounds`/`worldPanForScreen`. Camera `(x,y)` = world coord at viewport **top-left**; `screen = zoom·(world−cam)`. **`render:camera` also takes `rotation`** (radians, pivot = screen centre, 0 = unchanged) — camera roll. Locked by `CameraUnit` + `SceneCollectorTest`
- **ZoneNavigator** (`Scene/ZoneNavigator.h`, header-only, `grove::camera`): nested-zones navigation — the game syncs a tree of zones (id/parent/bounds), the engine drives the camera: zoom-to-cursor *enters* a zone, pan locked+scaled+camera-frame, soft magnet, **per-layer zoom bounds**, seamless deletion back-out, **lock onto a moving zone** (+ optional **velocity lead** to anticipate ahead of it), zoom snap, camera roll, **mouse drag-pan** (`dragPan`/`endDragPan` = grab 1:1 + light release inertia via `setPanInertia`; reusable `grove::camera::DragPan` in `Scene/DragPan.h`). `configure` knobs: margin/magnetRate/panMargin/maxDetail/snapStrength/snapRange/leadSeconds. Per frame: feed input → `update(dt)` → publish `render:camera`. Locked by `ZoneNavUnit`. Docs: DEVELOPER_GUIDE + `docs/design/zone-navigation.md`. Also `ZoomLadder` (`Scene/ZoomLadder.h`) for content-less plateau zoom
- **HUD overlay**: `render:rect`/`render:sprite`/`render:text` accept `space:"screen"` → drawn on a fixed screen-space view (bgfx view 1, overlay, no zoom/pan) so the HUD doesn't move with the world camera. Ephemeral only. Locked by `HudViewUnit` + `SceneCollectorTest`
- **Asset system** (`Assets/`, `AssetManager.h`): stream textures by a stable **string `assetId`** — thousands *available* (registry, metadata), a budget's worth *resident* (cache + **priority/LRU eviction** under `assetVramBudgetMB`). Pure `AssetManager` + `ITextureProvider` (bgfx behind it). **Atlases**: pre-baked (manifest) **or** runtime-packed (`packAtlas`/`ShelfPacker`) — N sprites share one sheet. A sprite refs `asset`:"id" (resolved → texId + UV). Fed by manifest (`assetManifest`) + `asset:*` topics. **Async load** (phase 3, opt-in `assetAsyncLoad`): `ThreadedDecoder` decodes off-thread, `pumpAsync` uploads on the render thread → no first-touch hitch (placeholder while decoding, failure-latched). Locked by `AssetManagerUnit`/`ShelfPackerUnit`/`AssetAsyncUnit` + `[gpu]` AssetProvider/AssetSprite/AssetTopics/AtlasPacker/AssetAsync/AssetAsyncModule (E2E through the module). **Docs: `docs/design/assets.md`**
- **IIO Topics**: `render:sprite` (also `asset`:"id" → texture streamed by the AssetManager, with atlas UVs), `render:rect` (filled colored quad, layered — for HUD), `render:sector` (filled ring-sector / pie wedge `{cx,cy,r0,r1,a0,a1,color,layer,space?}` via SectorPass — reusable for radial menus, cooldown rings, gauges), `render:text` (UTF-8 + French accents), `render:tilemap` (+`:add`/`:update`/`:remove` retained — `:add` also takes **`layers:[{tileData,textureId?}]`** = **multi-layer** chunks (Strategy A: layer 0 opaque terrain + layers >0 alpha-blended overlays/decals, drawn back-to-front; tile id 0 = transparent. Locked by `TilemapLodGpu` + `SceneCollectorTest`), `:anim {tileId,frames,fps}` = animated tiles, layer cycles by time, zero per-frame upload, **`:fog {id,x,y,w,h,fogData}`** = **partial fog-of-war reveal** — patches just the R8 mask sub-rect (mip 0 region update), no tile re-upload/LOD re-bake; fog is non-mipped+mutable so the patch applies. Locked by `TilemapLodGpu` + `SceneCollectorTest`), `render:particle`, `render:camera`, `render:clear`, `render:debug/*`, `asset:register`/`preload`/`setPriority`/`unload`/`pack` (streaming texture assets — see `docs/design/assets.md`), `render:texture:create {id,w,h,color?}`/`:paint {id,x,y,w,h,color}` (**runtime textures/painting** — paint colored sub-rects into a texture addressed by string id, usable by `render:sprite{asset}`; created empty+filled because bgfx makes with-data textures immutable; locked by `RuntimeTextureGpu`). **Note**: 2D views 0 (world) + 1 (HUD) are **Sequential** (submit order = draw order) — passes layer by graph order + per-pass layer sort
- **Bulk sprites (high throughput)**: `BgfxRendererModule::submitSpriteBatch(SpriteInstance*, n)` feeds GPU-ready instances straight to the renderer, **bypassing IIO+JSON**. `render:sprite` is one IIO message/sprite; the bus delivers a **shared immutable payload (zero-copy** — no per-delivery json copy; static/core publishers share with 0 copies, hot-loaded `.so` re-home once), but each sprite still costs a `JsonDataNode` to **build** + the per-message IIO machinery → caps the path in the low thousands/frame@60fps (the GPU is idle: 10k draw in <1ms). The bulk path is ~ns/sprite → **~100k sprites@60fps (≈21×), melts ~400k** (benchmarked: `tests/visual/benchmark_render_savage.cpp`, wall-clock, not a ctest). Locked headless by `SceneCollectorTest [bulk]` (`SceneCollector::addSpritesBulk`). Use `render:sprite` for UI/few entities, `submitSpriteBatch` for crowds; for huge **static** content prefer a retained tilemap (millions of tiles@60fps). `vsync` config is now honored (was hardcoded ON).

### UIModule
- **UIRenderer**: Publishes render commands to BgfxRenderer via IIO (layer 1000+)
- **Widgets**: UIButton, UIPanel, UILabel, UICheckbox, UISlider, UITextInput, UIProgressBar, UIImage, **UIFlipbook** (animated sprite-sheet panel via `grove::anim` — slice 6a; the retained renderer now carries animated UVs, locked by `UIFlipbookE2E`/IT_054), UIScrollPanel, UITooltip
- **IIO Topics**: Consumes `input:*`, `ui:set_text`, `ui:set_visible`, `ui:set_position`, `ui:data`/`:merge`, `ui:list:set_items`/`set_groups`, `ui:drawer:*`; publishes `ui:click`, `ui:action`, `ui:value_changed`, `ui:list:selected`/`group:toggled`, etc.
- **Interaction**: clicks carry the **button index** (0=left, 1=right) → a widget's `on` block fires `click` (left) or **`rightClick`** (right). Hover + tooltips track the widget by **pointer** (not id — repeater items share an empty id). `UITooltipManager` switches text immediately when sweeping between tooltipped items.
- **UIButton**: renders a **border frame** (`borderColor`/`borderWidth`, inset bg) — used for hover/selection highlight. Bindable props via `{{...}}`: `text`, `color`/`bgColor`, `texture` (numeric id), **`asset`** (streamed asset id string), `borderColor`, `tooltip` (+ base x/y/width/height/visible). **Sprite-as-UI by streamed asset id**: a part/icon button (or `UIImage`) with an `asset` prop publishes `render:sprite:add{asset:"id"}` → the renderer streams it via the AssetManager (atlas-aware UV, budget eviction). `asset` wins over `texture`. Locked by `UIAssetSpriteE2E` (IT_052).
- **Vessel screen** (`assets/ui/demo_vessel_screen.json`, `tests/visual/test_vessel_screen_demo.cpp`): fleet menu (left-click select / right-click open) + inspector window (clickable parts + scrollable icon+count **inventory grid**). E2E: IT_044–IT_051.
- **⚠️ Before modifying UI code:** Read [UI Architecture](docs/UI_ARCHITECTURE.md) for threading model, [UI Widgets](docs/UI_WIDGETS.md) for widget properties, [UI Topics](docs/UI_TOPICS.md) for IIO patterns

### InputModule
- **Backends**: SDLBackend (mouse, keyboard, gamepad Phase 2)
- **Thread-safe**: Event buffering with lock-free design
- **IIO Topics**: `input:mouse:*`, `input:keyboard:*`, `input:gamepad:*`
- **ActionMap helper**: `modules/InputModule/ActionMap.h` (`grove::input`, header-only) — semantic bindings by **scancode** (layout-proof, AZERTY-safe), multi-bind, remap. Locked by `ActionMapUnit`

### Animation (`grove::anim`, header-only, `include/grove/anim/`)
- Pure 2D animation math — **no renderer/IIO/SDL coupling** (computes transforms/UVs, never draws). Static-link → `#include` and go.
- **Procedural/cutout**: `Transform2D` + `Hierarchy` (compose parent∘local) + `Clip`/`Track`/`Keyframe` + `Easing` (8 curves) + `AnimationPlayer` (holds `const Clip*` shared; loop/speed/dt). The game publishes sprites from `rig.world(node)`.
- **Flipbook**: `SpriteSheet` (grid→UV) + `Flipbook` (frames + per-frame durations / `setFps`, `uvAt`).
- Integration pattern + usage in [DEVELOPER_GUIDE](docs/DEVELOPER_GUIDE.md#animation-grove_anim); live ref in `tests/visual/test_renderer_showcase.cpp`. Locked by `Transform2DUnit`/`EasingUnit`/`ClipUnit`/`AnimationPlayerUnit`/`SpriteSheetUnit`/`FlipbookUnit`

### SoundManager (`modules/SoundManager/`, `-DGROVE_BUILD_SOUND_MODULE=ON`)
- `SoundManagerModule` (IModule) consumes `sound:*` → drives `ISoundBackend`. Real backend `SDLMixerBackend` (SDL2_mixer) is **behind the interface** → module is SDL-free + headless-testable (MockSoundBackend).
- **Topics**: `sound:sfx {path,volume?,pan?,loop?,id?}`, `sound:sfx:stop {id,fadeMs?}`, `sound:sfx:stopAll`, `sound:music {path,loop?,fadeMs?,volume?}`, `sound:music:stop`, `sound:volume {bus,value}`, `sound:preload`/`sound:unload {path}`. Buses master/music/sfx; eff = clamp01(call·bus·master). **Publishes** `sound:music:position {path,elapsed,duration}` (slice 6b — real progress bar; throttled ~15 Hz while music plays; the backend owns the clock — SDL_mixer 2.6+ / Mock; `-1` duration = unknown/looping). Locked by `SoundManagerUnit` `[position]` + the radio-player UI `UIRadioE2E`/IT_055.
- **Adaptive music (slices 1-3)**: `audio:*` (declarative mood) — `audio:layer {id,path,gainCalm?,gainPeak?,loop?,theme?,state?}` (stem; gain crossfades calm→peak by tension; `theme`+`state` = a leitmotif arrangement, tension-exempt), `audio:intent {tension, quantize?}` (0..1; `quantize` "now"/"bar"/"beat"), `audio:tempo {bpm,beatsPerBar?}`, `audio:mix {id,gain}`, `audio:cue {path,volume?,quantize?}` (one-shot sting, quantizable), `audio:theme {id,state}` (select a leitmotif arrangement), `audio:layer:stop {id,fadeMs?}`. Pure `AdaptiveMixer` ramps stem gains (music bus) via `ISoundBackend::setSoundVolume`; pure `BeatClock` quantizes to the measure. Locked by `SoundManagerUnit` `[adaptive]` (32 cases). Adaptive vision shipped as LOGIC; real stems = content (compos late).
- Static-link host (Drifterra): link **`SoundManager_static`**, `module.setBackend(make_unique<sound::SDLMixerBackend>())`. Locked by `SoundManagerUnit`; real audio via `tests/visual/test_sound_demo.cpp`. **SDL2_mixer** required (devel package on the toolchain).

## Debugging Tools
```bash
# ThreadSanitizer (detects data races, deadlocks)
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan

# Helgrind (alternative deadlock detector)
cmake -DGROVE_ENABLE_HELGRIND=ON -B build
make helgrind
```
