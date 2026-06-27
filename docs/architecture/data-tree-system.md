# IDataTree System Architecture

## Overview

The IDataTree system is a unified hierarchical data management system for configuration, persistent data, and runtime state. It provides a flexible, abstract interface that can be backed by multiple storage formats (JSON, Binary, Database, etc.).

## Core Concepts

### Three Data Domains

The system manages three separate data trees with different characteristics:

```
root/
├── config/    (Read-only, hot-reload enabled, distributed)
├── data/      (Read-write, saved to disk, local per Engine)
└── runtime/   (Read-write, temporary, never saved)
```

#### config/ - Shared Configuration
- **Purpose**: Game configuration, unit stats, modding
- **Access**: Read-only for modules
- **Source**: Loaded from files, synchronized from Coordinator
- **Hot-reload**: Automatic detection and reload of changed files
- **Distribution**: Synchronized across all Engines via Coordinator

#### data/ - Persistent Data
- **Purpose**: Player saves, campaign progress, unlocks, statistics
- **Access**: Read-write for modules (via IIO publish)
- **Source**: Local files per Engine
- **Persistence**: Saved to disk on request
- **Isolation**: Each Engine maintains its own data/

#### runtime/ - Temporary State
- **Purpose**: Current game state, caches, temporary calculations
- **Access**: Read-write for modules
- **Lifecycle**: Exists only in memory, never saved
- **Scope**: Strictly local to each Engine instance

## Architecture Layers

### Layer 1: Interfaces (Abstract)

```cpp
IDataValue    // Abstract data value (type-safe access)
IDataNode     // Tree node (navigation, search, modification)
IDataTree     // Root container (config/data/runtime management)
```

**Key principle**: Modules and systems work ONLY with interfaces, never concrete implementations.

### Layer 2: Concrete Implementations

```cpp
JsonDataValue     // nlohmann::json backed value
JsonDataNode      // JSON tree node with full feature set
JsonDataTree      // File-based JSON storage

// Future implementations:
BinaryDataValue   // Custom binary format
DatabaseDataNode  // SQL backend
NetworkDataTree   // Remote data server
```

**Pluggable backends**: Change implementation via `DataTreeFactory::create(type, path)`

### Layer 3: Module System Integration

```cpp
IModuleSystem
├── Owns IDataTree instance
├── Provides config to modules via setConfiguration()
├── Listens to IIO for save requests
└── Persists data to Tree
```

**Responsibilities**:
- Initialize Tree (local or synchronized)
- Extract and provide config nodes to modules
- Handle save/load requests from modules via IIO
- Trigger hot-reload notifications

### Layer 4: Distributed Coordination

```cpp
CoordinationModule (Master)
├── Master config tree
├── Hot-reload detection
└── Broadcast updates via IIO

DebugEngine (Workers)
├── Local tree replica
├── Subscribe to config updates
└── Apply synchronized config
```

## Data Flow Patterns

### Pattern 1: Module Reads Configuration

```cpp
// Engine startup
auto tankConfig = tree->getConfigRoot()->getChild("tanks")->getChild("heavy");
tankModule->setConfiguration(*tankConfig, io, scheduler);

// Module uses config
void TankModule::setConfiguration(const IDataNode& config, ...) {
    m_armor = config.getInt("armor");
    m_speed = config.getDouble("speed");
    m_weaponType = config.getString("weapon_type");
}
```

### Pattern 2: Module Saves State

```cpp
// Module publishes save request via IIO
void TankModule::onDestroy() {
    auto state = createDataNode({
        {"position", {x, y}},
        {"health", currentHealth},
        {"ammo", ammoCount}
    });

    m_io->publish("save:tank:state:123", std::move(state));
}

// Engine listens and persists
void Engine::handleSaveRequests() {
    while (m_io->hasMessages()) {
        auto msg = m_io->pullMessage();

        if (msg.topic.starts_with("save:")) {
            std::string path = extractPath(msg.topic);
            // msg.data is a SHARED, const payload (zero-copy bus) — copy its json into an OWNED
            // node to hand to setChild (which takes a unique_ptr); you can't move a shared/const node.
            m_tree->getDataRoot()->setChild(path,
                std::make_unique<JsonDataNode>(path,
                    dynamic_cast<const JsonDataNode&>(*msg.data).getJsonData()));
            m_tree->saveNode("data/" + path);
        }
    }
}
```

### Pattern 3: Config Hot-Reload (Distributed)

```cpp
// COORDINATEUR: Master config with hot-reload
void CoordinationModule::tick() {
    if (m_masterTree->reloadIfChanged()) {
        auto config = m_masterTree->getConfigRoot();
        m_networkIO->publish("config:reload", std::move(config));
    }
}

// ENGINE: Receives and applies config
void DebugEngine::processNetworkMessages() {
    auto msg = m_networkIO->pullMessage();

    if (msg.topic == "config:reload") {
        auto configRoot = m_tree->getNode("config");
        configRoot->clearChildren();
        // Shared/const payload → copy its json into an owned node (setChild takes a unique_ptr).
        configRoot->setChild("updated",
            std::make_unique<JsonDataNode>("updated",
                dynamic_cast<const JsonDataNode&>(*msg.data).getJsonData()));

        // Notify all modules
        for (auto& module : m_modules) {
            auto moduleConfig = configRoot->getChild(module->getType());
            module->setConfiguration(*moduleConfig, m_io, m_scheduler);
        }
    }
}
```

## Advanced Features

### Pattern Matching Search

```cpp
// Find all heavy units
auto heavyUnits = configRoot->getChildrenByNameMatch("*_heavy_*");

// Find all tank variants
auto tanks = configRoot->getChildrenByNameMatch("tank_*");
```

### Property-Based Queries

```cpp
// Find all units with armor > 150
auto heavyArmored = configRoot->queryByProperty("armor",
    [](const IDataValue& val) {
        return val.isNumber() && val.asInt() > 150;
    });
```

### Hash-Based Validation

```cpp
// Check if config changed
std::string currentHash = configNode->getTreeHash();
if (currentHash != lastKnownHash) {
    // Config changed, refresh caches
    rebuildLookupTables();
    lastKnownHash = currentHash;
}
```

## Implementation Guidelines

### For Module Developers

**DO:**
- Read config via `getInt()`, `getString()`, `getBool()`, etc.
- Publish save requests via IIO: `m_io->publish("save:module:state", data)`
- Scope each module to one subsystem (size by responsibility, not line count)
- Use IDataNode interface only, never concrete types

**DON'T:**
- Directly access IDataTree
- Create nodes manually (use factory or IIO)
- Assume JSON format (work with IDataValue interface)
- Cache config pointers (config can be reloaded)

### For Engine Implementers

**DO:**
- Create one IDataTree per Engine instance
- Subscribe to "save:*" pattern for save requests
- Call `reloadIfChanged()` periodically (or use callbacks)
- Provide isolated config subtrees to modules

**DON'T:**
- Share IDataTree between Engines (use synchronization instead)
- Allow modules to access root directly
- Block on save operations (make async if needed)

### For System Architects

**DO:**
- Use Coordinator pattern for config distribution
- Keep data/ isolated per Engine
- Use IIO for all cross-component communication
- Consider multiple IDataTree implementations for different needs

**DON'T:**
- Synchronize data/ across Engines (defeats isolation)
- Put business logic in IDataTree implementations
- Create tight coupling between Tree and Modules

## File Structure Example

```
gamedata/
├── config/
│   ├── tanks.json
│   ├── weapons.json
│   ├── buildings.json
│   └── mods/
│       └── super_mod/
│           ├── new_tanks.json
│           └── new_weapons.json
├── data/
│   ├── campaign_progress.json
│   ├── unlocked_tech.json
│   └── player_stats.json
└── runtime/
    (in-memory only, not on disk)
```

## Benefits

### For Development
- **Modular**: Swap backends without changing module code
- **Testable**: Mock IDataTree for unit tests
- **Hot-reload**: Instant feedback during development
- **Type-safe**: Compile-time checks with IDataNode interface

### For Operations
- **Distributed**: Config synchronized across cluster
- **Isolated**: Each Engine manages its own saves
- **Auditable**: Hash validation for data integrity
- **Flexible**: JSON for dev, Binary for production

### For Modding
- **Accessible**: JSON files are human-readable
- **Safe**: Read-only access prevents corruption
- **Extensible**: Mods add files without conflicts
- **Hot-loadable**: Changes apply without restart

## Future Enhancements

### Planned Features
- Binary format implementation (BinaryDataTree)
- Database backend (DatabaseDataTree)
- Differential updates (send only changed config)
- Compression for network sync
- Versioning and migration system

### Potential Extensions
- Encryption for sensitive data
- Cloud storage backend (S3DataTree)
- Real-time collaboration (multiple writers)
- Conflict resolution for distributed writes
- Schema validation and type checking

---

**Status**: Production-ready ✅
**Version**: 1.0.0
**Last Updated**: 2025-10-28
