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
| **ThreadedModuleSystem** | ✅ Phase 2 Complete | One thread per module (parallel execution) | 2-8 modules, ≤30 FPS |
| **ThreadPoolModuleSystem** | 🚧 Planned (Phase 3) | Shared worker pool, work stealing | High performance (>30 FPS) |
| **ClusterModuleSystem** | 🚧 Planned (Phase 4) | Distributed across machines | MMO scale |

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
- **IIO Topics**: `render:sprite`, `render:rect` (filled colored quad, layered — for HUD), `render:text` (UTF-8 + French accents), `render:tilemap` (+`:add`/`:update`/`:remove` retained, `:anim {tileId,frames,fps}` = animated tiles, layer cycles by time, zero per-frame upload), `render:particle`, `render:camera`, `render:clear`, `render:debug/*`

### UIModule
- **UIRenderer**: Publishes render commands to BgfxRenderer via IIO (layer 1000+)
- **Widgets**: UIButton, UIPanel, UILabel, UICheckbox, UISlider, UITextInput, UIProgressBar, UIImage, UIScrollPanel, UITooltip
- **IIO Topics**: Consumes `input:*`, `ui:set_text`, `ui:set_visible`; publishes `ui:click`, `ui:action`, `ui:value_changed`, etc.
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
- **Topics**: `sound:sfx {path,volume?,pan?,loop?,id?}`, `sound:sfx:stop {id,fadeMs?}`, `sound:sfx:stopAll`, `sound:music {path,loop?,fadeMs?,volume?}`, `sound:music:stop`, `sound:volume {bus,value}`, `sound:preload`/`sound:unload {path}`. Buses master/music/sfx; eff = clamp01(call·bus·master).
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
