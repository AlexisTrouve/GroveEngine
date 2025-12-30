# GroveEngine - Developer Guide

**Comprehensive guide for building applications with GroveEngine**

## Table of Contents

1. [Getting Started](#getting-started)
2. [Core System](#core-system)
3. [Available Modules](#available-modules)
   - [BgfxRenderer - 2D Rendering](#bgfxrenderer---2d-rendering)
   - [UIModule - User Interface](#uimodule---user-interface)
   - [InputModule - Input Handling](#inputmodule---input-handling)
4. [IIO Topics Reference](#iio-topics-reference)
5. [Complete Application Example](#complete-application-example)
6. [Building Your First Game](#building-your-first-game)
7. [Advanced Topics](#advanced-topics)

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
| **IIO** | Pub/Sub messaging | [USER_GUIDE.md](USER_GUIDE.md#iio) |
| **IDataNode** | Configuration & data | [USER_GUIDE.md](USER_GUIDE.md#idatanode) |
| **ModuleLoader** | Hot-reload system | [USER_GUIDE.md](USER_GUIDE.md#moduleloader) |

---

## Available Modules

### BgfxRenderer - 2D Rendering

**Status:** ✅ Production Ready (Phase 7-8 complete)

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
sprite->setDouble("x", 100.0);
sprite->setDouble("y", 200.0);
sprite->setDouble("scaleX", 1.0);
sprite->setDouble("scaleY", 1.0);
sprite->setDouble("rotation", 0.0);        // Radians
sprite->setInt("color", 0xFFFFFFFF);       // RGBA
sprite->setInt("textureId", playerTexture);
sprite->setInt("layer", 10);               // Z-order (higher = front)
io->publish("render:sprite", std::move(sprite));
```

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
camera->setDouble("x", playerX);           // Center camera on player
camera->setDouble("y", playerY);
camera->setDouble("zoom", 1.0);
camera->setInt("viewportX", 0);
camera->setInt("viewportY", 0);
camera->setInt("viewportW", 1920);
camera->setInt("viewportH", 1080);
io->publish("render:camera", std::move(camera));
```

**Full Topic Reference:** See [IIO Topics - Rendering](#rendering-topics)

---

### UIModule - User Interface

**Status:** ✅ Production Ready (Phase 7 complete)

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
// In your game module - subscribe to button events
gameIO->subscribe("ui:click");
gameIO->subscribe("ui:action");

// In process()
while (gameIO->hasMessages() > 0) {
    auto msg = gameIO->pullMessage();

    if (msg.topic == "ui:action") {
        std::string action = msg.data->getString("action", "");
        std::string widgetId = msg.data->getString("widgetId", "");

        if (action == "start_game" && widgetId == "play_button") {
            startGame();
        }
    }
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
// UIModule automatically publishes:
// - render:sprite (for UI rectangles/images)
// - render:text (for labels/buttons)

// BgfxRenderer consumes these and renders the UI
// Layer management ensures UI renders on top (layer 1000+)
```

**Full Topic Reference:** See [IIO Topics - UI Events](#ui-events)

---

### InputModule - Input Handling

**Status:** ✅ Production Ready (Phase 1 complete)

Cross-platform input handling with SDL2 backend (mouse, keyboard, gamepad).

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

#### Consuming Input Events

```cpp
// Subscribe to input topics
gameIO->subscribe("input:mouse:button");
gameIO->subscribe("input:keyboard:key");

// In process()
while (gameIO->hasMessages() > 0) {
    auto msg = gameIO->pullMessage();

    if (msg.topic == "input:mouse:button") {
        int button = msg.data->getInt("button", 0);  // 0=left, 1=middle, 2=right
        bool pressed = msg.data->getBool("pressed", false);
        double x = msg.data->getDouble("x", 0.0);
        double y = msg.data->getDouble("y", 0.0);

        if (button == 0 && pressed) {
            // Left mouse button pressed at (x, y)
            handleClick(x, y);
        }
    }

    if (msg.topic == "input:keyboard:key") {
        int scancode = msg.data->getInt("scancode", 0);  // SDL_SCANCODE_*
        bool pressed = msg.data->getBool("pressed", false);

        if (scancode == SDL_SCANCODE_SPACE && pressed) {
            playerJump();
        }
    }
}
```

**Full Topic Reference:** See [IIO Topics - Input Events](#input-events)

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

#### Sprites

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:sprite` | `{x, y, scaleX, scaleY, rotation, u0, v0, u1, v1, color, textureId, layer}` | Render single sprite |
| `render:sprite:batch` | `{sprites: [array]}` | Render sprite batch (optimized) |

#### Text

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:text` | `{x, y, text, fontSize, color, layer}` | Render text |

#### Tilemap

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:tilemap` | `{chunkX, chunkY, tiles: [array], tileSize, textureId, layer}` | Render tilemap chunk |

#### Particles

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:particle` | `{x, y, velocityX, velocityY, color, lifetime, textureId, layer}` | Render particle |

#### Camera

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:camera` | `{x, y, zoom, viewportX, viewportY, viewportW, viewportH}` | Set camera transform |

#### Clear

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:clear` | `{color: int}` | Set clear color (RGBA) |

#### Debug

| Topic | Payload | Description |
|-------|---------|-------------|
| `render:debug:line` | `{x1, y1, x2, y2, color}` | Draw debug line |
| `render:debug:rect` | `{x, y, w, h, color, filled}` | Draw debug rectangle |

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

        // Subscribe to UI events
        m_io->subscribe("ui:action");
        m_io->subscribe("ui:click");
    }

    void process(const grove::IDataNode& input) override {
        double deltaTime = input.getDouble("deltaTime", 0.016);

        // Process UI events
        while (m_io->hasMessages() > 0) {
            auto msg = m_io->pullMessage();

            if (msg.topic == "ui:action") {
                std::string action = msg.data->getString("action", "");

                if (action == "start_game") {
                    startGame();
                }
            }
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
        sprite->setDouble("x", m_playerX);
        sprite->setDouble("y", m_playerY);
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
    sprite->setDouble("x", enemy.x);
    // ...
    io->publish("render:sprite", std::move(sprite));  // 100 IIO messages
}

// Publish as batch:
auto batch = std::make_unique<JsonDataNode>("batch");
auto sprites = std::make_unique<JsonDataNode>("sprites");
for (auto& enemy : enemies) {
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("x", enemy.x);
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
// Module A: Request pathfinding
auto request = std::make_unique<JsonDataNode>("request");
request->setString("requestId", "path_123");
request->setDouble("startX", 10.0);
request->setDouble("startY", 20.0);
io->publish("pathfinding:request", std::move(request));

// Module B: Respond with path
moduleB_io->subscribe("pathfinding:request");
// ... compute path ...
auto response = std::make_unique<JsonDataNode>("response");
response->setString("requestId", "path_123");
// ... add path data ...
moduleB_io->publish("pathfinding:response", std::move(response));

// Module A: Receive response
moduleA_io->subscribe("pathfinding:response");
```

#### Event Aggregation

```cpp
// Multiple modules publish events
io->publish("combat:damage", damageData);
io->publish("combat:kill", killData);
io->publish("combat:levelup", levelupData);

// Analytics module aggregates all combat events
analyticsIO->subscribe("combat:*");
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
// Verify subscription BEFORE publishing
io->subscribe("render:sprite");  // Must be before publish

// Check topic patterns
io->subscribe("render:*");  // Matches render:sprite, render:text
io->subscribe("render:sprite:*");  // Only matches render:sprite:batch
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
