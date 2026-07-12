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
| **DialogueModule** | ✅ Slice 7 MVP | VN/cutscene runtime — data-driven node/choice/branch script via `scene:*`, binding-driven (pushes `ui:data`) | `-DGROVE_BUILD_DIALOGUE_MODULE=ON` (SDL-free) |
| **VideoModule** | ✅ Slice 6c | Video playback + A/V sync (`video:*`); **real MP4/H.264/AAC** via `FfmpegCliBackend` (ffmpeg CLI subprocess, no libav linking); raw-pixel `render:texture:upload` | `-DGROVE_BUILD_VIDEO_MODULE=ON` (SDL-free; needs ffmpeg on PATH for real MP4) |
| **FxModule** | ✅ Slice E1-E3 | Ephemeral **VFX/effects** layer — spawn short-lived cosmetic effects (explosions, debris, trails, damage numbers) as data (components+behaviors+prefabs), the engine ticks them → `render:sprite:*`. `fx:*` topics + C++ `world()` API. **NOT for gameplay crowds** (own your SoA state + `submitSpriteBatch`) | `-DGROVE_BUILD_FX_MODULE=ON` (SDL-free) |

**Header-only helpers** (no module/build flag — `#include` and go): `grove::camera` (zoom/pan/cull, `Scene/Camera.h`), `grove::anim` (2D animation, `include/grove/anim/`), `grove::input::ActionMap` (scancode bindings, `modules/InputModule/ActionMap.h`), **`grove::save::SaveFile`** (whole-game save/load, `include/grove/save/SaveFile.h`). See the quick-reference + DEVELOPER_GUIDE.

**Save / Load** (`grove::save::SaveFile` + `DebugEngine::saveState/loadState`): persist the whole game to a versioned JSON file, built on the per-module `getState()`/`setState()` contract (same as hot-reload → a module that hot-reloads also saves/loads). `engine.saveState(path)`/`loadState(path)` captures/restores every registered module in one call (SEQUENTIAL-hosted; threaded/pool skipped w/ warning — call between frames); or drive `SaveFile` directly (`captureModule`/`save`/`load`/`restoreInto`) if you own the module objects. **Cross-DLL-safe** (`capture()` deep-copies immediately, `restoreInto()` builds a host-owned node — a save survives a module reload/unload, the LimitsTest lesson). Fail-soft (missing/malformed/future-version → false; absent module skipped). Locked by `SaveFileUnit` + `SaveEngineE2E`. Docs: DEVELOPER_GUIDE "Save / Load".

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

# Shipping (lean) build — strips the debug skin (introspection + verbose per-frame logging)
cmake -B build-shipping -DGROVE_DEBUG=OFF
cmake --build build-shipping -j4
```

### Debug vs Shipping build (`GROVE_DEBUG`)
**Debug and prod are ONE engine, two builds — NOT two engine classes.** `DebugEngine` IS the engine (its threaded/pool hosting, authoritative clock, asset streaming and save/load are the prod core). The `GROVE_DEBUG` CMake flag (default **ON**; `include/grove/BuildConfig.h` → `GROVE_DEBUG` macro + `constexpr grove::kDebugBuild` + `GROVE_DEBUG_ONLY(...)`) gates only the **debug skin**, compiled OUT of a shipping build (`-DGROVE_DEBUG=OFF`):
- **Stripped in shipping**: `step()`'s per-frame logging + frame-timing; `getDetailedStatus()` (→ minimal marker node); `dumpModuleState`/`dumpAllModulesState`/`stepSingleFrame` (→ no-op, symbols kept).
- **KEPT in shipping**: the whole prod core + error logging (`catch`) + `pauseExecution`/`resumeExecution`/`isPaused` (engine CONTROL, not introspection) + `saveState`/`loadState`.
- Health monitoring is debug-only *for now* (the helpers only LOG, no action yet); when health→action lands it must live outside the gate.

In a debug build the gates re-expand to the original code (byte-identical, no behavior change). Locked by `BuildConfigUnit` (the flag/macro/strip mechanism) + `DebugGateE2E` (step() logging + `getDetailedStatus` on the real engine) — both compiled under BOTH configs (default CTest run = ON; a `-DGROVE_DEBUG=OFF` build = the shipping contract). The `EngineType::PRODUCTION`/`HIGH_PERFORMANCE` stubs stay unimplemented (`EngineFactory` throws). Plan: **[docs/design/engine-debug-prod-plan.md](docs/design/engine-debug-prod-plan.md)**.

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
- **Anchor convention** (⚠️ AI-contract — the field NAME carries the anchor, don't guess): **`x,y` = top-left CORNER** (`render:rect`/`tilemap`/`text`/`debug:*`; `render:camera` = world coord at viewport top-left) · **`cx,cy` = CENTER** (`render:sprite`(+`:add`/`:update`)/`particle`/`sector`). `rotation` pivots at the box **centre** whichever anchor positioned it. Legacy `x,y` on sprite/particle is **rejected** (primitive dropped + one-shot log, never silently shifted). **New draw primitives MUST follow this.** Locked by `SceneCollectorTest [anchor]`/`[reject]`. Docs: `docs/design/render-anchor-convention.md`
- **Asset system** (`Assets/`, `AssetManager.h`): stream textures by a stable **string `assetId`** — thousands *available* (registry, metadata), a budget's worth *resident* (cache + **priority/LRU eviction** under `assetVramBudgetMB`). Pure `AssetManager` + `ITextureProvider` (bgfx behind it). **Atlases**: pre-baked (manifest) **or** runtime-packed (`packAtlas`/`ShelfPacker`) — N sprites share one sheet. A sprite refs `asset`:"id" (resolved → texId + UV). Fed by manifest (`assetManifest`) + `asset:*` topics. **Async load** (phase 3, opt-in `assetAsyncLoad`): `ThreadedDecoder` decodes off-thread, `pumpAsync` uploads on the render thread → no first-touch hitch (placeholder while decoding, failure-latched). Locked by `AssetManagerUnit`/`ShelfPackerUnit`/`AssetAsyncUnit` + `[gpu]` AssetProvider/AssetSprite/AssetTopics/AtlasPacker/AssetAsync/AssetAsyncModule (E2E through the module). **Docs: `docs/design/assets.md`**
- **IIO Topics**: `render:sprite` (also `asset`:"id" → texture streamed by the AssetManager, with atlas UVs), `render:rect` (filled colored quad, layered — for HUD), `render:sector` (filled ring-sector / pie wedge `{cx,cy,r0,r1,a0,a1,color,layer,space?}` via SectorPass — reusable for radial menus, cooldown rings, gauges), `render:text` (UTF-8 + French accents), `render:tilemap` (+`:add`/`:update`/`:remove` retained — `:add` also takes **`layers:[{tileData,textureId?}]`** = **multi-layer** chunks (Strategy A: layer 0 opaque terrain + layers >0 alpha-blended overlays/decals, drawn back-to-front; tile id 0 = transparent. Locked by `TilemapLodGpu` + `SceneCollectorTest`), `:anim {tileId,frames,fps}` = animated tiles, layer cycles by time, zero per-frame upload, **`:fog {id,x,y,w,h,fogData}`** = **partial fog-of-war reveal** — patches just the R8 mask sub-rect (mip 0 region update), no tile re-upload/LOD re-bake; fog is non-mipped+mutable so the patch applies. Locked by `TilemapLodGpu` + `SceneCollectorTest`), `render:particle`, `render:camera`, `render:clear`, `render:debug/*`, `asset:register`/`preload`/`setPriority`/`unload`/`pack` (streaming texture assets — see `docs/design/assets.md`), `render:texture:create {id,w,h,color?}`/`:paint {id,x,y,w,h,color}`/**`:upload {id,w,h,+blob "pixels"}`** (**runtime textures/painting** — `create` a mutable texture by string id, `paint` a solid-color sub-rect, **`upload` the WHOLE texture's raw RGBA8 pixels from a blob** (arbitrary-pixel upload — video frames, procedural images; the video path, slice 6c-0c); usable by `render:sprite{asset}`; created empty+filled because bgfx makes with-data textures immutable; locked by `RuntimeTextureGpu`). **Note**: 2D views 0 (world) + 1 (HUD) are **Sequential** (submit order = draw order) — passes layer by graph order + per-pass layer sort
- **Bulk sprites (high throughput)**: `BgfxRendererModule::submitSpriteBatch(SpriteInstance*, n)` feeds GPU-ready instances straight to the renderer, **bypassing IIO+JSON**. `render:sprite` is one IIO message/sprite; the bus delivers a **shared immutable payload (zero-copy** — no per-delivery json copy; static/core publishers share with 0 copies, hot-loaded `.so` re-home once), but each sprite still costs a `JsonDataNode` to **build** + the per-message IIO machinery → caps the path in the low thousands/frame@60fps (the GPU is idle: 10k draw in <1ms). The bulk path is ~ns/sprite → **~100k sprites@60fps (≈21×), melts ~400k** (benchmarked: `tests/visual/benchmark_render_savage.cpp`, wall-clock, not a ctest). Locked headless by `SceneCollectorTest [bulk]` (`SceneCollector::addSpritesBulk`). Use `render:sprite` for UI/few entities, `submitSpriteBatch` for crowds; for huge **static** content prefer a retained tilemap (millions of tiles@60fps). `vsync` config is now honored (was hardcoded ON). **Same bulk path now for particles + text**: `submitParticleBatch(ParticleInstance*, n)` / `submitTextBatch(TextCommand*, n)` (text copies each label's string into the frame) — the JSON-per-primitive wall was the same ~5k/frame@60fps; measured **particles 4.9k→364k (74×)**, **text 4.9k→50.8k labels (10×)**. For a swarm's per-agent trails/labels. Locked by `SceneCollectorTest [bulk]` (`addParticlesBulk`/`addTextsBulk`).

### UIModule
- **UIRenderer**: Publishes render commands to BgfxRenderer via IIO (layer 1000+)
- **Widgets**: UIButton, UIPanel, UILabel, UICheckbox, UISlider, UITextInput, UIProgressBar, UIImage, **UIFlipbook** (animated sprite-sheet panel via `grove::anim` — slice 6a; the retained renderer now carries animated UVs, locked by `UIFlipbookE2E`/IT_054), UIScrollPanel, UITooltip
- **IIO Topics**: Consumes `input:*`, `ui:set_text`, `ui:set_visible`, `ui:set_position`, `ui:data`/`:merge`, `ui:list:set_items`/`set_groups`/**`set_tree`** (flat / one-level wings / **N-level tree** — `UIList` projects all three onto one flat row sequence; slice 5d, locked by `UIListTreeE2E`/IT_056), `ui:drawer:*`; publishes `ui:click`, `ui:action`, `ui:value_changed`, `ui:list:selected`/`group:toggled`, etc.
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

### DialogueModule (`modules/DialogueModule/`, `-DGROVE_BUILD_DIALOGUE_MODULE=ON`) — VN/cutscene runtime (slice 7)
- `DialogueModule` (IModule) wraps the **pure header-only `dialogue::DialogueRuntime`** — a node/choice/branch state machine (parse `{start, nodes:{id:{speaker?,text,background?,voice?, goto? | choices:[{text,goto}]}}}`, `start`/`advance`/`choose(i)`/`goToNode(id)`/`isEnd`; no IIO/render coupling, like `grove::anim`). SDL-free, dependency-free (nlohmann only).
- **Binding-driven** (rides the whole binding engine): on a node it publishes **`ui:data:merge {scene:{id,speaker,text,background,isEnd,choices:[{text,goto}]}}`** → a game-authored VN screen renders it (bound labels + a **choice repeater**; choice buttons fire **`scene:goto {node}`** — id-based, a string that survives declarative events). Voice via `sound:sfx`; emits `scene:node`/`scene:end`.
- **Topics consumed**: `scene:load {start,nodes}`, `scene:advance`, `scene:choose {index}` (int OR string), `scene:goto {node}`, `scene:set {var,value}`. Engine = the runtime + machinery; **script + VN screen = game** (demo: `assets/dialogue/demo_script.json` + `assets/ui/demo_vn_screen.json`).
- **Conditions (declarative — NOT an expression language)**: a node `set:{var:value}` assigns vars on entry; a `choice.when:[{var,op?,value?}]` gates whether it's OFFERED (a LIST = AND); a node `branch:[{when?,goto}]` picks the first gated linear edge on `advance`; `vars` seeds the store. Ops: truthy/falsy/eq/ne/gt/ge/lt/le. No OR/nesting/parsing — compound logic = restructure the graph or set a derived flag game-side. `scene:set` re-presents so an unlocked choice appears live. Locked by `DialogueRuntimeUnit [conditions]` + `IT_057c`.
- Static-link host (Drifterra): link **`DialogueModule_static`**, drive `process()`, push `scene:*`. Locked by `DialogueRuntimeUnit` (oracle) + `IT_057` (E2E through the module). Follow-ons: conditions/flags (no expression language), auto-advance/typewriter, mid-scene save.

### VideoModule (`modules/VideoModule/`, `-DGROVE_BUILD_VIDEO_MODULE=ON`) — video playback (slice 6c)
- `VideoModule` (IModule) plays a video onto a runtime texture, **A/V-synced to an audio master clock**. Pure `video::VideoSync` (header-only, like BeatClock) does the sync math (master clock → frame index + changed/**dropped**/ended); the decoder is behind **`IVideoBackend`** (like `ISoundBackend`) → headless-testable via `MockVideoBackend`.
- **AUDIO IS THE MASTER CLOCK**: the module follows `sound:music:position` (slice 6b); the picture holds/advances/**drops** frames to stay locked (a silent clip uses a dt clock). Per changed frame it publishes **`render:texture:upload {id,w,h,+blob pixels}`** (raw-RGBA GPU upload) so the video sprite shows it.
- **Topics consumed**: `video:play {path,audio?,textureId?,x?,y?,w?,h?,layer?}`, `video:stop`, `video:pause {paused}`, `sound:music:position`. **Published**: `render:texture:create`/`:upload` + `render:sprite:add` + `video:frame {index}` / `video:ended` (+ `sound:music` for the audio track).
- **Real MP4**: `FfmpegCliBackend` decodes MP4/H.264/AAC by driving the **ffmpeg CLI as a subprocess** (`popen`; ffprobe metadata + a raw-RGBA frame pipe + audio extracted to a temp OGG) — **no libav linking** (ffmpeg.exe must be on PATH / bundled to ship). Forward-only (seek = follow-on).
- Static-link host (Drifterra): link **`VideoModule_static`**, `module.setBackend(make_unique<video::FfmpegCliBackend>())`. Locked by `VideoSyncUnit` + `IT_058` (module, mock backend) + `RuntimeTextureGpu` (`[gpu]` pixel upload) + `FfmpegBackendReal` (real ffmpeg decode, gated). Follow-on: a by-eye windowed demo (MP4 + synced audio) + seek.

### FxModule (`modules/FxModule/`, `-DGROVE_BUILD_FX_MODULE=ON`) — ephemeral VFX/effects layer (slices E1-E3 + F1-F4)
- **⚠️ SCOPE — this is for EPHEMERAL COSMETIC EFFECTS, NOT gameplay entities.** Its sweet-spot is short-lived fire-and-forget visuals: explosions, debris, engine trails, muzzle flash, warp shimmer, floating damage numbers — things with no "real" position any gameplay system must own. **Do NOT route gameplay crowds/agents through it**: a per-effect component map is AoS (one node per entity) and would rebuild the per-primitive dispatch wall the bulk path demolished. Gameplay entities → **own your state (SoA) + `submitSpriteBatch`** (see Bulk sprites); movement/formation/AI stay consumer-side (that's Daedalium/CombatCore's job, not `move{vx,vy}`).
- **The authoring surface**: an effect = a stable id + typed **components** (`Transform{cx,cy,rotation,scaleX,scaleY}`, `Sprite{asset|textureId,color,layer}`, **`Text{text,color,layer,fontSize}`** — an optional label; sprite & text are orthogonal, an entity may bear either/both, each diffs into its own retained pool; **`Emitter{prefab,count,ratePerSec,speedMin/Max,spreadDeg,dirDeg,oneShot}`** — a particle emitter (burst OR stream), see below) + a list of **behaviors** from a **FIXED engine library**. Pure header-only core `grove::fx::FxWorld` (`include/grove/fx/FxWorld.h`, like `grove::anim`/`DialogueRuntime`) does the tick + the retained-render diff; `FxModule` (IModule) wraps it onto IIO.
- **Behaviors are engine-side → every project inherits them for free**, WITHOUT a scripting language (an enum of primitives with numeric params, like the VN conditions). Library: **`move{vx,vy}` · `spin{degPerSec}` · `lifetime{seconds}` · `fade{seconds,fromAlpha,toAlpha}`** (ramps the AA byte of sprite AND/OR text color; default = fade-out 1→0) **· `velocity{vx,vy,drag}`** (initial velocity that decelerates by `drag`/s — debris/spark spread; drag 0 = constant; explicit-Euler so tick at frame dt, not big chunks). Add one = a `Type` + tick case in `FxWorld`. Deliberately NO `follow`/`path`/`oscillate` — that's gameplay-movement, consumer-owned.
- **Prefabs/archetypes (E3)**: `fx:prefab {name, transform?, sprite?, text?, behaviors?}` registers a reusable template once; `fx:spawn {id, archetype:"explosion", ...}` instantiates it, the spawn's transform/sprite/text MERGE on top + its behaviors ADD to the prefab's. Deep copy (fresh behavior state). Unknown archetype = fail-soft plain spawn. Locked by `FxWorldUnit [prefab]` + `IT_059c`.
- **Floating damage numbers (F2)**: the drifterra-priority archetype = a `text` component + `velocity{0,-vy}` (rises) + `fade` + `lifetime`, registered once as `fx:prefab {name:"damage_number", text:{...}, behaviors:[...]}` then `fx:spawn {archetype:"damage_number", transform:{cx,cy}, text:{text:"-25"}}` (string is per-instance, **already localized by the consumer — the engine is i18n-agnostic**). Locked by `FxWorldUnit [text]` + `IT_059e`.
- **Particle emitter (F3 burst + F4 stream)**: an `Emitter` component spawns fresh `prefab`-instance particles AT the emitter, each launched with a **deterministic-random** velocity in the cone `[dirDeg ± spreadDeg/2]` at speed `[speedMin,speedMax]` (PRNG **seeded by entity id**, persisted across ticks → reproducible + testable). Particle look+fade+lifetime live in the prefab (author once). **Two modes**: `oneShot:true` (F3) = a **burst** of `count` particles on the next tick, then the (invisible) emitter **self-destructs** (explosions/debris/muzzle-flash; explosion archetype = a prefab carrying the emitter); `oneShot:false` (F4) = a **continuous stream** of `ratePerSec` particles/sec **every tick** while the entity lives, no self-destruct (engine trails/smoke/exhaust — stop via `ratePerSec:0` or destroy; steady-state count is self-bounded by particle lifetime). A moving emitter (a `move` behavior or `fx:set` transform per frame) drops particles along its path = a **trail**; one entity can be Sprite(head)+Emitter(trail)+move = a **comet**. ⚠️ **Particles are short-lived SPRITE entities** (ride `render:sprite:*` + F1 behaviors), **NOT** the renderer's `render:particle` primitive — VFX-sized (tens), not GPU masses (→ `submitParticleBatch`). Locked by `FxWorldUnit [emitter]`/`[stream]` + `IT_059f`/`IT_059g` + the `test_fx_demo` comet.
- **Topics consumed**: `fx:prefab {name,...}`, `fx:spawn {id, archetype?, transform?, sprite?, text?, emitter?, behaviors?:[{type,...}]}`, `fx:set {id, transform?, sprite?, text?, emitter?}` (partial-merge; robust JSON accessors fail-soft, never throw on a bad payload), `fx:destroy {id}`. **Published**: `render:sprite:add`/`:update`/`:remove` (`cx,cy` = CENTER) for sprite entities (incl. emitter particles) + **`render:text:add`/`:update`/`:remove` (`x,y` = top-left CORNER — that primitive's native anchor, so a text entity's Transform position maps to the text's x,y) for text entities** (`renderId` = the effect id; sprite & text are separate id spaces, no collision). Each `process(dt)`: drain → `tick(dt)` → `diffRender()` → emit the minimal retained traffic.
- **Hot-reload full-world serialization**: `getState`/`setState` round-trip the WHOLE live world across a module reload — entities dumped **verbatim (ids = renderIds preserved)** with their internal behavior/emitter state (fade age, decayed velocity, emitter accumulator/rngState, `present` flags), plus the prefab library, the string-id→EntityId name map, and the id counter. Render snapshots are NOT serialized — the next `diffRender()` re-Adds every entity, which the renderer applies **idempotently** (a `render:*:add` on a held renderId just overwrites). Preserving the ids is what avoids the real hazard: **orphaned renderer sprites** a state-less reload could never remove. Locked by `FxWorldUnit [serialize]` + `IT_059h [hotreload]` (round-trip + resume-mid-flight + anti-orphan destroy-by-string-id).
- Static-link host (Drifterra): link **`FxModule_static`**, drive `module.world()` directly (spawn/spawnFromPrefab/setSprite/setText/setEmitter/addBehavior) then `process(dt)` each frame — or push `fx:*` topics. Locked by `FxWorldUnit` (pure oracle) + `IT_059` (E2E, prove-it-bites). **Positioning (drifterra alignment):** the engine offers this for VFX; gameplay entities stay consumer-owned (SoA + batch) — the module name carries the scope so it isn't mistaken for a gameplay-entity system.

## Debugging Tools
```bash
# ThreadSanitizer (detects data races, deadlocks)
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan

# Helgrind (alternative deadlock detector)
cmake -DGROVE_ENABLE_HELGRIND=ON -B build
make helgrind
```
