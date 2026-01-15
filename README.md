# GroveEngine 🌳

**Modular C++ Engine Architecture for Rapid Development with Hot-Reload**

GroveEngine is a lightweight, modular engine architecture designed for blazing-fast development iteration (0.4ms hot-reload validated) and optimized for Claude Code workflows.

## Key Features

- 🔥 **Hot-Reload 0.4ms** - Validated blazing-fast module reloading
- 🧩 **Modular Architecture** - Clean separation via interfaces (IEngine, IModule, IIO, IModuleSystem)
- 🚀 **Development Velocity** - Edit → Build → Hot-reload < 1 second total
- 🤖 **Claude Code Optimized** - 200-300 line modules for AI-friendly development
- 📦 **Autonomous Builds** - Each module builds independently (`cmake .`)
- 🔌 **Progressive Scaling** - Debug → Production → Cloud without rewriting

## Architecture Overview

```
grove::IEngine (Orchestration)
├── grove::IModuleSystem (Execution strategy)
│   ├── SequentialModuleSystem (Debug/test - 1 module at a time)
│   ├── ThreadedModuleSystem (Each module in thread - TODO)
│   └── MultithreadedModuleSystem (Thread pool - TODO)
├── grove::IModule (Business logic - 200-300 lines)
│   └── Your modules (.so/.dll hot-reloadable)
└── grove::IIO (Communication)
    ├── IntraIO (Same process - validated)
    ├── LocalIO (Same machine - TODO)
    └── NetworkIO (Distributed - TODO)
```

## Current Status

### ✅ Implemented & Validated
- **Core Interfaces** (13): IEngine, IModule, IModuleSystem, IIO, ICoordinationModule, ITaskScheduler, IDataTree, IDataNode, IUI, ISerializable
- **Debug Implementations** (Phase 2 - Pre-IDataTree):
  - `DebugEngine` - Comprehensive logging and health monitoring
  - `SequentialModuleSystem` - Ultra-lightweight execution
  - `IntraIO` + `IntraIOManager` - Sub-millisecond pub/sub with pattern matching
  - `ModuleFactory` - Dynamic .so/.dll loading system
  - `EngineFactory`, `ModuleSystemFactory`, `IOFactory` - Factory patterns
- **Hot-Reload System** - 0.4ms average, 0.055ms best performance, perfect state preservation
- **UI System** - ImGuiUI implementation with hybrid sizing

### ⚠️ Compatibility Note
Current implementations use **pre-IDataTree API** (`json` config). The architecture evolved to use `IDataNode` for configuration. Implementations need adaptation or recreation for full IDataTree compatibility.

### 🚧 TODO
- Adapt implementations to use IDataTree/IDataNode instead of json
- Implement ThreadedModuleSystem and MultithreadedModuleSystem
- Implement LocalIO and NetworkIO
- Create concrete IDataTree implementations (JSONDataTree, etc.)

## Quick Start

### Try the Interactive Demo

**See it in action first!** Run the full stack demo to see BgfxRenderer + UIModule + InputModule working together:

```bash
# Windows
run_full_stack_demo.bat

# Linux
./build/tests/test_full_stack_interactive
```

**Features:**
- Click buttons, drag sliders, interact with UI
- Spawn bouncing sprites with physics
- Complete input → UI → game → render flow
- All IIO topics demonstrated

See [tests/visual/README_FULL_STACK.md](tests/visual/README_FULL_STACK.md) for details.

### Directory Structure
```
GroveEngine/
├── include/grove/          # 27 headers
│   ├── IEngine.h          # Core interfaces
│   ├── IModule.h
│   ├── IModuleSystem.h
│   ├── IIO.h
│   ├── IDataTree.h        # Configuration system
│   ├── IDataNode.h
│   └── ...
├── src/                    # 10 implementations
│   ├── DebugEngine.cpp
│   ├── SequentialModuleSystem.cpp
│   ├── IntraIO.cpp
│   ├── ModuleFactory.cpp
│   └── ...
├── docs/                   # Documentation
│   ├── architecture/
│   │   ├── architecture-modulaire.md
│   │   └── claude-code-integration.md
│   └── implementation/
│       └── CLAUDE-HOT-RELOAD-GUIDE.md
├── modules/                # Your application modules
├── tests/                  # Tests
└── CMakeLists.txt         # Build system
```

### Build

```bash
cd GroveEngine
mkdir build && cd build
cmake ..
make

# Or use the root CMakeLists.txt directly
cmake .
make
```

### Create a Module

```cpp
// MyModule.h
#include <grove/IModule.h>

class MyModule : public grove::IModule {
public:
    json process(const json& input) override {
        // Your logic here (200-300 lines max)
        return {"result": "processed"};
    }

    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override {
        // Configuration setup
    }

    // ... other interface methods
};
```

## Documentation

### For Developers Using GroveEngine

- **[DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md)** - 📘 **START HERE** - Complete guide with modules, IIO topics, and full examples
- **[USER_GUIDE.md](docs/USER_GUIDE.md)** - Module system basics, hot-reload, IIO communication

### Module Documentation

- **[BgfxRenderer](modules/BgfxRenderer/README.md)** - 2D rendering (sprites, text, tilemap, particles)
- **[UIModule](modules/UIModule/README.md)** - User interface (10 widget types, layout, scrolling)
- **[InputModule](modules/InputModule/README.md)** - Input handling (mouse, keyboard, gamepad)

### Architecture & Internals

- **[Architecture Modulaire](docs/architecture/architecture-modulaire.md)** - Core interface architecture
- **[Claude Code Integration](docs/architecture/claude-code-integration.md)** - AI-optimized development workflow
- **[Hot-Reload Guide](docs/implementation/CLAUDE-HOT-RELOAD-GUIDE.md)** - 0.4ms hot-reload system

## Philosophy

### Micro-Context Development
- **Small modules** (200-300 lines) for AI-friendly development
- **Autonomous builds** - Zero parent dependencies
- **Hot-swappable infrastructure** - Change performance without touching business logic

### Progressive Evolution
```cpp
// Start simple (MVP)
DebugEngine + SequentialModuleSystem + IntraIO

// Scale transparently (same module code)
HighPerfEngine + MultithreadedModuleSystem + NetworkIO
```

### Complexity Through Simplicity
Complex behavior emerges from the interaction of simple, well-defined modules.

## Performance

**Hot-Reload Benchmarks** (Validated):
- Average: **0.4ms**
- Best: **0.055ms**
- 5-cycle test: **2ms total**
- State persistence: **100% success rate**
- Classification: **🚀 BLAZING** (Theoretical maximum achieved)

## Projects Using GroveEngine

- **AISSIA** - AI Smart Schedule & Interactive Assistant (in development)
- **WarFactory** (original architecture source)

## License

MIT License - see [LICENSE](LICENSE) file for details

## Contributing

This engine uses an architecture optimized for Claude Code development. Each module is autonomous and can be developed independently.

**Constraints:**
- ✅ Modules 200-300 lines maximum
- ✅ Autonomous build: `cmake .` from module directory
- ✅ JSON-only communication between modules
- ✅ Zero dependencies up (no `#include "../"`)
- ❌ Never `cmake ..`

---

*GroveEngine - Where modules grow like trees in a grove 🌳*
