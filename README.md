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
│   ├── SequentialModuleSystem (✅ Implemented - 1 module at a time)
│   ├── ThreadedModuleSystem (🚧 TODO - Each module in thread)
│   └── MultithreadedModuleSystem (🚧 TODO - Thread pool)
├── grove::IModule (Business logic - 200-300 lines)
│   └── Your modules (.so/.dll hot-reloadable)
└── grove::IIO (Communication)
    ├── IntraIO (✅ Implemented - Same process pub/sub)
    ├── LocalIO (🚧 TODO - Same machine IPC)
    └── NetworkIO (🚧 TODO - Distributed messaging)
```

## Current Status

### ✅ Production-Ready Components
- **Core Engine**:
  - `DebugEngine` - Comprehensive logging and health monitoring
  - `SequentialModuleSystem` - Single-threaded module execution
  - `IntraIO` + `IntraIOManager` - Sub-millisecond pub/sub with pattern matching
  - `ModuleLoader` - Hot-reload system (0.4ms average, 0.055ms best)

- **Rendering Stack** (BgfxRenderer):
  - Sprite rendering with automatic batching
  - Tilemap rendering with instancing
  - Particle effects system
  - Debug text overlay (8x8 bitmap font)
  - RHI abstraction over bgfx

- **UI System** (UIModule):
  - 10 widget types (button, panel, label, checkbox, slider, text input, progress bar, image, scroll panel, tooltip)
  - JSON layout loading
  - Retained mode rendering (85%+ IIO reduction)
  - Thread-safe input handling

- **Input System** (InputModule):
  - Mouse (movement, buttons, wheel)
  - Keyboard (keys, text input)
  - SDL2 backend

- **Test Suite**: 20+ integration tests + visual demos

### 🚧 Roadmap
- **Module Systems**: ThreadedModuleSystem, MultithreadedModuleSystem
- **IO Systems**: LocalIO (IPC), NetworkIO (distributed)
- **Input**: Gamepad support (Phase 2)
- **Renderer**: Advanced text rendering, post-processing effects

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

- **[BgfxRenderer](modules/BgfxRenderer/README.md)** - 2D rendering (sprites, tilemap, particles, debug text)
- **[UIModule](modules/UIModule/README.md)** - User interface (10 widget types, layout, scrolling)
- **[InputModule](modules/InputModule/README.md)** - Input handling (mouse, keyboard via SDL)

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
// Current (Production-Ready)
DebugEngine + SequentialModuleSystem + IntraIO

// Future Vision (Roadmap)
HighPerfEngine + MultithreadedModuleSystem + NetworkIO
// Same module code - just swap the infrastructure
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

GroveEngine is **dual-licensed** - you choose the license that fits your project:

### 📜 **GPL v3** (Open Source - Free)
Use GroveEngine in open-source projects under the [GNU GPL v3](LICENSE-GPL).
- ✅ **100% Free** - No costs, no royalties
- ✅ **Full engine access** - Modify and use freely
- ❗ **Your game must be GPL** - Source code must be published
- 👥 **Community support**

### 💼 **Commercial License** (Proprietary - Royalty-Based)
Use GroveEngine in closed-source commercial games under the [Commercial License](LICENSE-COMMERCIAL).
- ✅ **FREE up to €100,000** revenue per project
- ✅ **1% royalty** on revenue above €100,000
- ✅ **Keep your code private** - Proprietary games allowed
- ✅ **Email support** - 72h response time
- ✅ **Priority bug fixes**

**🎮 Best for indie developers:** Most favorable royalty model in the industry!

---

### 📊 License Comparison

| Feature                | GPL v3 (Free)          | Commercial                |
|------------------------|------------------------|---------------------------|
| **Cost**               | Free                   | Free up to €100k revenue  |
| **Royalties**          | None                   | 1% above €100k            |
| **Your game license**  | Must be GPL (open)     | Proprietary allowed       |
| **Engine modifications** | Share modifications  | Keep private              |
| **Support**            | Community              | Email (72h) + priority    |
| **Updates**            | Yes                    | Yes + priority fixes      |
| **Attribution**        | Required               | Required ("Powered by")   |
| **Number of projects** | Unlimited              | Unlimited                 |

---

### ❓ FAQ - Which License Should I Choose?

**Q: I'm making a commercial indie game. Which license?**
A: **Commercial License** - It's FREE until €100k, then only 1% royalties. Much better than Unreal (5% above $1M).

**Q: I'm making an open-source game. Which license?**
A: **GPL v3** - Perfect for open-source projects, 100% free forever.

**Q: How do I declare my revenue?**
A: Annual email with your project revenue. Simple and trust-based. Audits possible but rare.

**Q: Can I modify the engine?**
A: **Yes!** Both licenses allow modifications. GPL requires sharing them, Commercial lets you keep them private.

**Q: Is GroveEngine cheaper than Unreal Engine?**
A: **Yes!** We charge 1% above €100k vs Unreal's 5% above $1M USD. For a €500k game, you'd pay €4,000 with GroveEngine vs €0 with Unreal (under threshold). For a €1.5M game: €14,000 vs ~€25,000 with Unreal.

**Q: What if my game makes €80,000?**
A: **€0 royalties!** You're within the free tier. No payment required.

**Q: Is support included?**
A: GPL = community support. Commercial = email support (72h response) + priority bug fixes.

**Q: How do I get the Commercial License?**
A: Email **alexistrouve.pro@gmail.com** with subject "GroveEngine Commercial License Request". No upfront payment - royalties only after €100k!

---

### 🏆 Industry Comparison

| Engine        | Free Tier     | Royalty      | Notes                          |
|---------------|---------------|--------------|--------------------------------|
| **GroveEngine** | €0 - €100k  | **1%** > €100k | Best for EU indie devs       |
| Unreal Engine | $0 - $1M USD  | 5% > $1M     | Higher %, higher threshold     |
| Unity         | Subscription  | None         | Monthly fees (~€2k/year Pro)   |
| Godot         | 100% Free     | None         | MIT, but minimal official support |

**GroveEngine = Best value for games earning €100k - €500k** 🎯

---

**📧 License Questions?** Contact **alexistrouve.pro@gmail.com**

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
