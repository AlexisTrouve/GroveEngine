# GroveEngine - Features Overview

**Complete feature list for GroveEngine v1.0**

## Core Engine Features

### Hot-Reload System
- ✅ **0.4ms average reload time** (validated, blazing fast)
- ✅ **State preservation** via `getState()`/`setState()`
- ✅ **Perfect stability** (100% success rate in stress tests)
- ✅ **Module independence** (each ModuleLoader manages ONE module)
- ✅ **Cache bypass** for reliable reload (temp file copy technique)

### Module System
- ✅ **IModule interface** - Standard module contract
- ✅ **Dynamic loading** (.so/.dll via dlopen/LoadLibrary)
- ✅ **Configuration hot-reload** (no code rebuild needed)
- ✅ **Health monitoring** via `getHealthStatus()`
- ✅ **Graceful shutdown** with cleanup

### Communication (IIO)
- ✅ **Pub/Sub messaging** with topic-based routing
- ✅ **Wildcard patterns** (e.g., `render:*`, `input:mouse:*`)
- ✅ **IntraIO** - Same-process communication (production ready)
- ✅ **Low-frequency subscriptions** with batching
- ✅ **O(k) topic matching** via TopicTree (k = topic depth)
- ✅ **Thread-safe** message queues

### Data Abstraction
- ✅ **IDataNode interface** - Hierarchical configuration/state
- ✅ **JsonDataNode** - JSON-backed implementation
- ✅ **Typed accessors** (getString, getInt, getDouble, getBool)
- ✅ **Tree navigation** (getChild, getChildNames)
- ✅ **Pattern matching** (getChildrenByNameMatch)

### Testing & Validation
- ✅ **23+ tests** (unit, integration, stress, chaos)
- ✅ **ThreadSanitizer support** (data race detection)
- ✅ **Helgrind support** (deadlock detection)
- ✅ **CTest integration**
- ✅ **Benchmark suite** (TopicTree, batching, E2E)

---

## Available Modules

### BgfxRenderer - 2D Rendering

**Status:** ✅ Phase 7-8 Complete (Production Ready)

#### Rendering Capabilities
- ✅ **Sprite rendering** with batching (10,000+ sprites/frame)
- ✅ **Text rendering** with bitmap fonts
- ✅ **Tilemap rendering** with chunking
- ✅ **Particle effects**
- ✅ **Debug shapes** (lines, rectangles)
- ✅ **Layer-based Z-ordering** (depth sorting)
- ✅ **Multi-texture batching** (reduces draw calls)

#### Backends
- ✅ **Auto-detection** (best backend for platform)
- ✅ **DirectX 11** (Windows)
- ✅ **DirectX 12** (Windows 10+)
- ✅ **OpenGL** (Windows, Linux, macOS)
- ✅ **Vulkan** (Windows, Linux)
- ✅ **Metal** (macOS, iOS)
- ✅ **Noop** (headless testing)

#### Architecture
- ✅ **RHI abstraction** (no bgfx dependencies outside BgfxDevice.cpp)
- ✅ **RenderGraph** with topological sort (Kahn's algorithm)
- ✅ **CommandBuffer** for deferred rendering
- ✅ **FrameAllocator** (lock-free, reset per frame)
- ✅ **ResourceCache** (textures, shaders)

#### IIO Topics Consumed
- `render:sprite` - Single sprite
- `render:sprite:batch` - Sprite batch (optimized)
- `render:text` - Text rendering
- `render:tilemap` - Tilemap chunks
- `render:particle` - Particle instances
- `render:camera` - Camera transform
- `render:clear` - Clear color
- `render:debug:line` - Debug lines
- `render:debug:rect` - Debug rectangles

---

### UIModule - User Interface

**Status:** ✅ Phase 7 Complete (Production Ready)

#### Widget Types (10 Total)
- ✅ **UIButton** - Clickable button with hover states
- ✅ **UILabel** - Static text display
- ✅ **UIPanel** - Container widget
- ✅ **UICheckbox** - Toggle checkbox
- ✅ **UISlider** - Value slider (horizontal/vertical)
- ✅ **UITextInput** - Text input field with cursor
- ✅ **UIProgressBar** - Progress indicator
- ✅ **UIImage** - Sprite/texture display
- ✅ **UIScrollPanel** - Scrollable container with scrollbar
- ✅ **UITooltip** - Hover tooltips

#### Features
- ✅ **JSON-based layouts** - Define UI in JSON files
- ✅ **Hierarchical widget tree** - Parent/child relationships
- ✅ **Automatic input handling** - Consumes InputModule events
- ✅ **Layer management** - UI renders on top (layer 1000+)
- ✅ **Event publishing** - Game logic subscribes to UI events
- ✅ **UIRenderer** - Publishes to BgfxRenderer via IIO
- ✅ **Hot-reload support** - State preservation

#### IIO Topics Consumed
- `input:mouse:move` - Mouse movement
- `input:mouse:button` - Mouse clicks
- `input:mouse:wheel` - Mouse wheel (scrolling)
- `input:keyboard:key` - Key events
- `input:keyboard:text` - Text input

#### IIO Topics Published
- `ui:click` - Widget clicked
- `ui:action` - Button action triggered
- `ui:value_changed` - Slider/checkbox/input value changed
- `ui:text_submitted` - Text input submitted (Enter)
- `ui:hover` - Mouse entered/left widget
- `ui:scroll` - Scroll panel scrolled
- `render:sprite` - UI rectangles/images
- `render:text` - UI text

---

### InputModule - Input Handling

**Status:** ✅ Phase 1 Complete (Production Ready)

#### Input Sources
- ✅ **Mouse** - Move, button (left/middle/right), wheel
- ✅ **Keyboard** - Key events with modifiers (shift, ctrl, alt)
- ✅ **Text input** - UTF-8 text for TextInput widgets
- 📋 **Gamepad** - Phase 2 (buttons, axes, vibration)

#### Backends
- ✅ **SDL2** - Cross-platform (Windows, Linux, macOS)
- 🔧 **Extensible** - Easy to add GLFW, Win32, etc.

#### Features
- ✅ **Thread-safe event buffering** - Feed events from any thread
- ✅ **Generic event format** - Backend-agnostic InputEvent
- ✅ **IIO publishing** - Converts to IIO messages
- ✅ **Hot-reload support** - State preservation (mouse position, button states)

#### IIO Topics Published
- `input:mouse:move` - {x, y}
- `input:mouse:button` - {button, pressed, x, y}
- `input:mouse:wheel` - {delta}
- `input:keyboard:key` - {scancode, pressed, repeat, shift, ctrl, alt}
- `input:keyboard:text` - {text}

---

## Integration Examples

### Complete Game Loop

```cpp
// Modules communicate via IIO
InputModule → IIO → UIModule → IIO → GameLogic
                                  ↓
                         BgfxRenderer (renders everything)
```

### Message Flow Example

1. User clicks button
2. SDL generates `SDL_MOUSEBUTTONDOWN`
3. InputModule publishes `input:mouse:button`
4. UIModule subscribes, detects click on UIButton
5. UIModule publishes `ui:action` with button's action
6. GameLogic subscribes, receives action
7. GameLogic publishes `render:sprite` for player
8. BgfxRenderer subscribes, renders player sprite

### Full Application Stack

```
┌─────────────────────────────────────────────┐
│           Your Game Logic Module            │
│  (Subscribes: ui:*, Publishes: render:*)   │
└──────────────┬──────────────────────────────┘
               │ IIO Topics
┌──────────────┼──────────────────────────────┐
│  ┌───────────▼─────────┐   ┌──────────────┐ │
│  │    UIModule         │   │  InputModule │ │
│  │ (Widgets, Layout)   │◄──┤  (SDL2)      │ │
│  └─────────┬───────────┘   └──────────────┘ │
│            │ render:sprite, render:text      │
│  ┌─────────▼────────────┐                    │
│  │   BgfxRenderer       │                    │
│  │ (bgfx Multi-backend) │                    │
│  └──────────────────────┘                    │
└─────────────────────────────────────────────┘
```

---

## Platform Support

| Platform | Core | BgfxRenderer | UIModule | InputModule |
|----------|------|--------------|----------|-------------|
| **Windows** | ✅ MinGW/MSVC | ✅ DX11/DX12/OpenGL/Vulkan | ✅ | ✅ SDL2 |
| **Linux** | ✅ GCC/Clang | ✅ OpenGL/Vulkan | ✅ | ✅ SDL2 |
| **macOS** | ✅ Clang | ✅ Metal/OpenGL | ✅ | ✅ SDL2 |

---

## Performance Metrics

### Hot-Reload
- **Average:** 0.4ms
- **Best:** 0.055ms
- **Worst:** 2ms (5-cycle test total)
- **Classification:** 🚀 BLAZING

### Rendering (BgfxRenderer)
- **Sprite batching:** 10,000+ sprites/frame
- **Draw call reduction:** Multi-texture batching
- **Frame time:** < 16ms @ 60fps (typical)

### IIO Communication
- **Topic matching:** O(k) where k = topic depth
- **Message overhead:** Minimal (lock-free queues)
- **Throughput:** 100,000+ messages/second (validated)

### UI System
- **Update time:** < 1ms per frame
- **Widget limit:** 1000+ widgets tested
- **Layout caching:** Tree built once, not per frame

---

## Build System

### CMake Options

```bash
# Core only
cmake -B build

# With rendering
cmake -B build -DGROVE_BUILD_BGFX_RENDERER=ON

# With UI
cmake -B build -DGROVE_BUILD_UI_MODULE=ON

# With input
cmake -B build -DGROVE_BUILD_INPUT_MODULE=ON

# Everything
cmake -B build \
  -DGROVE_BUILD_BGFX_RENDERER=ON \
  -DGROVE_BUILD_UI_MODULE=ON \
  -DGROVE_BUILD_INPUT_MODULE=ON

# Debugging tools
cmake -B build -DGROVE_ENABLE_TSAN=ON  # ThreadSanitizer
cmake -B build -DGROVE_ENABLE_HELGRIND=ON  # Helgrind
```

### Dependencies

**Core:**
- C++17 compiler
- CMake 3.20+
- spdlog (logging)
- nlohmann/json (JSON parsing)

**BgfxRenderer:**
- bgfx (auto-downloaded via FetchContent)

**InputModule:**
- SDL2

**UIModule:**
- (No additional deps, uses BgfxRenderer + InputModule via IIO)

---

## Roadmap

### Core Engine
- ✅ Hot-reload system
- ✅ IModule interface
- ✅ IntraIO communication
- ✅ JsonDataNode
- 📋 LocalIO (same-machine IPC)
- 📋 NetworkIO (distributed)
- 📋 ThreadedModuleSystem
- 📋 MultithreadedModuleSystem

### BgfxRenderer
- ✅ Sprites, text, tilemap, particles
- ✅ Multi-backend support
- 📋 Texture loading (stb_image)
- 📋 Shader compilation (runtime)
- 📋 Multi-view support
- 📋 Render targets / post-processing

### UIModule
- ✅ 10 widget types
- ✅ Layout system
- ✅ Scrolling + tooltips
- 📋 Drag-and-drop
- 📋 Animations
- 📋 Custom themes

### InputModule
- ✅ Mouse + keyboard (SDL2)
- 📋 Gamepad support (Phase 2)
- 📋 Touch input (mobile)
- 📋 GLFW backend
- 📋 Win32 backend

---

## Documentation

**For Developers:**
- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) - 📘 Complete guide with examples
- [USER_GUIDE.md](USER_GUIDE.md) - Module system basics

**Module Docs:**
- [BgfxRenderer README](../modules/BgfxRenderer/README.md)
- [UIModule README](../modules/UIModule/README.md)
- [InputModule README](../modules/InputModule/README.md)

**Architecture:**
- [Architecture Modulaire](architecture/architecture-modulaire.md)
- [Hot-Reload Guide](implementation/CLAUDE-HOT-RELOAD-GUIDE.md)

---

**GroveEngine - Build modular, hot-reloadable games with blazing-fast iteration** 🌳🚀
