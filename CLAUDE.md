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

## Available Modules

| Module | Status | Description | Build Flag |
|--------|--------|-------------|------------|
| **BgfxRenderer** | ✅ Phase 7-8 Complete | 2D rendering (sprites, text, tilemap, particles) | `-DGROVE_BUILD_BGFX_RENDERER=ON` |
| **UIModule** | ✅ Phase 7 Complete | UI widgets (buttons, panels, scrolling, tooltips) | `-DGROVE_BUILD_UI_MODULE=ON` |
| **InputModule** | ✅ Production Ready | Input handling (mouse, keyboard, SDL backend) | `-DGROVE_BUILD_INPUT_MODULE=ON` |

**Integration:** All modules communicate via IIO topics. See [DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md) for complete IIO topics reference.

## Build & Test
```bash
# Build core only
cmake -B build && cmake --build build -j4

# Build with all modules
cmake -B build -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_INPUT_MODULE=ON
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
- **IIO Topics**: `render:sprite`, `render:text`, `render:tilemap`, `render:particle`, `render:camera`, `render:clear`, `render:debug/*`

### UIModule
- **UIRenderer**: Publishes render commands to BgfxRenderer via IIO (layer 1000+)
- **Widgets**: UIButton, UIPanel, UILabel, UICheckbox, UISlider, UITextInput, UIProgressBar, UIImage, UIScrollPanel, UITooltip
- **IIO Topics**: Consumes `input:*`, `ui:set_text`, `ui:set_visible`; publishes `ui:click`, `ui:action`, `ui:value_changed`, etc.
- **⚠️ Before modifying UI code:** Read [UI Architecture](docs/UI_ARCHITECTURE.md) for threading model, [UI Widgets](docs/UI_WIDGETS.md) for widget properties, [UI Topics](docs/UI_TOPICS.md) for IIO patterns

### InputModule
- **Backends**: SDLBackend (mouse, keyboard, gamepad Phase 2)
- **Thread-safe**: Event buffering with lock-free design
- **IIO Topics**: `input:mouse:*`, `input:keyboard:*`, `input:gamepad:*`

## Debugging Tools
```bash
# ThreadSanitizer (detects data races, deadlocks)
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan

# Helgrind (alternative deadlock detector)
cmake -DGROVE_ENABLE_HELGRIND=ON -B build
make helgrind
```
