# GroveEngine Documentation

## Overview

GroveEngine is a modular game engine architecture designed for distributed systems and hot-reload development. It provides a clean separation between business logic (modules) and infrastructure (engine, IO, scheduling).

## Architecture Documents

### Core Systems

- **[Data Tree System](architecture/data-tree-system.md)** - Unified config/data/runtime management
  - IDataNode/IDataValue/IDataTree interfaces
  - JSON backend implementation
  - Hot-reload and persistence
  - Distributed configuration synchronization

- **[Modular Architecture](architecture/architecture-modulaire.md)** - Module system design
  - IModule interface and constraints
  - IModuleSystem execution strategies
  - Hot-reload workflow
  - Claude Code optimization

- **[Claude Code Integration](architecture/claude-code-integration.md)** - AI development workflow
  - Micro-context development
  - Hot-reload for rapid iteration
  - Module development best practices

## Quick Start

### Creating a Module

```cpp
#include <grove/IModule.h>
#include <grove/IDataNode.h>

class TankModule : public IModule {
private:
    IIO* m_io;
    int m_armor;
    double m_speed;

public:
    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override {
        m_io = io;
        m_armor = config.getInt("armor", 100);
        m_speed = config.getDouble("speed", 5.0);
    }

    void process(const IDataNode& input) override {
        // Game logic here

        // Save state via IIO
        auto state = createDataNode({
            {"armor", m_armor},
            {"speed", m_speed}
        });
        m_io->publish("save:tank:state", std::move(state));
    }

    std::unique_ptr<IDataNode> getState() override {
        return createDataNode({
            {"armor", m_armor},
            {"speed", m_speed}
        });
    }

    void setState(const IDataNode& state) override {
        m_armor = state.getInt("armor", 100);
        m_speed = state.getDouble("speed", 5.0);
    }

    std::string getType() const override { return "tank"; }
};
```

### Using the Data Tree

```cpp
#include <grove/DataTreeFactory.h>

// Create tree
auto tree = DataTreeFactory::create("json", "./gamedata");

// Access configuration (read-only)
auto configRoot = tree->getConfigRoot();
auto tankConfig = configRoot->getChild("tanks")->getChild("heavy");
int armor = tankConfig->getInt("armor");

// Access persistent data (read-write)
auto dataRoot = tree->getDataRoot();
auto progress = dataRoot->getChild("campaign")->getChild("progress");
progress->setData(createDataNode({{"level", 5}}));
tree->saveData();

// Hot-reload config
if (tree->reloadIfChanged()) {
    // Config changed, refresh modules
}
```

## Key Concepts

### Module System
- **Modules**: 200-300 line business logic units
- **IModuleSystem**: Execution strategy (Sequential, Threaded, Distributed)
- **Hot-reload**: Replace modules without restarting
- **State preservation**: getState/setState for seamless updates

### Data Management
- **config/**: Read-only game configuration (hot-reload, distributed)
- **data/**: Persistent player data (local saves)
- **runtime/**: Temporary state (never saved)

### Communication
- **IIO**: Pub/sub messaging between modules
- **ITaskScheduler**: Delegate heavy computation
- **Save pattern**: Modules publish "save:*" messages, Engine persists

### Distribution
- **Coordinator**: Master config with hot-reload
- **Engines**: Local replicas, synchronized config
- **Isolation**: Each Engine has independent data/

## Design Principles

1. **Interface-based**: Work with abstractions (IDataNode, not JsonDataNode)
2. **Backend-agnostic**: Swap implementations without code changes
3. **Minimal coupling**: Modules communicate only via IIO
4. **Hot-reload first**: Development optimized for instant feedback
5. **Distribution-ready**: Config sync, data isolation built-in

## Project Status

### Implemented ✅
- Complete IDataNode/IDataTree system
- JSON backend (JsonDataValue, JsonDataNode, JsonDataTree)
- Hot-reload for config files
- Save/load for persistent data
- Pattern matching and property queries
- SHA256 hashing for validation

### In Progress 🚧
- Coordinator synchronization implementation
- Module system integration with DataTree
- Example modules and tests

### Planned 📋
- Binary format backend
- Database backend
- Network synchronization protocol
- Schema validation
- Migration system

## Contributing

When adding new features:
1. Start with interface definition (.h file)
2. Add documentation to this folder
3. Implement concrete class
4. Update architecture docs
5. Write usage examples

## Further Reading

- [Hot-Reload Guide](implementation/CLAUDE-HOT-RELOAD-GUIDE.md)
- [Module Architecture](architecture/architecture-modulaire.md)
- [Data Tree System](architecture/data-tree-system.md)

---

**Last Updated**: 2025-10-28
**Engine Version**: 1.0.0
