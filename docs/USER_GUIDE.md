# GroveEngine User Guide

GroveEngine is a C++17 hot-reload module system designed for building modular applications with runtime code replacement capabilities.

## Table of Contents

1. [Overview](#overview)
2. [Core Concepts](#core-concepts)
3. [Project Setup](#project-setup)
4. [Creating Modules](#creating-modules)
5. [Module Lifecycle](#module-lifecycle)
6. [Inter-Module Communication](#inter-module-communication)
7. [Hot-Reload](#hot-reload)
8. [Configuration Management](#configuration-management)
9. [Task Scheduling](#task-scheduling)
10. [API Reference](#api-reference)

---

## Overview

GroveEngine provides:

- **Hot-Reload**: Replace module code at runtime without losing state
- **Modular Architecture**: Self-contained modules with clear interfaces
- **Pub/Sub Communication**: Decoupled inter-module messaging via topics
- **State Preservation**: Automatic state serialization across reloads
- **Configuration Hot-Reload**: Update module configuration without code changes

### Design Philosophy

- Modules contain pure business logic (200-300 lines recommended)
- No infrastructure code in modules (threading, networking, persistence)
- All data via `IDataNode` abstraction (backend agnostic)
- Pull-based message processing (modules control when they read messages)

---

## Core Concepts

### IModule

The base interface all modules implement. Defines the contract for:
- Processing logic (`process()`)
- Configuration (`setConfiguration()`, `getConfiguration()`)
- State management (`getState()`, `setState()`)
- Lifecycle (`shutdown()`)

### IDataNode

Hierarchical data structure for configuration, state, and messages. Supports:
- Typed accessors (`getString()`, `getInt()`, `getDouble()`, `getBool()`)
- Tree navigation (`getChild()`, `getChildNames()`)
- Pattern matching (`getChildrenByNameMatch()`)

### IIO

Pub/Sub communication interface:
- `publish()`: Send messages to topics
- `subscribe()`: Listen to topic patterns
- `pullMessage()`: Consume received messages

### ModuleLoader

Handles dynamic loading of `.so` files:
- `load()`: Load a module from shared library
- `reload()`: Hot-reload with state preservation
- `unload()`: Clean unload

---

## Project Setup

### Directory Structure

```
MyProject/
├── CMakeLists.txt
├── external/
│   └── GroveEngine/          # GroveEngine (submodule or symlink)
├── src/
│   ├── main.cpp
│   └── modules/
│       ├── MyModule.h
│       └── MyModule.cpp
└── config/
    └── mymodule.json
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyProject VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# GroveEngine Integration
# ============================================================================
set(GROVE_BUILD_TESTS OFF CACHE BOOL "Disable GroveEngine tests" FORCE)
add_subdirectory(external/GroveEngine)

# ============================================================================
# Main Executable
# ============================================================================
add_executable(myapp src/main.cpp)

target_link_libraries(myapp PRIVATE
    GroveEngine::impl
    spdlog::spdlog
)

# ============================================================================
# Hot-Reloadable Modules (.so)
# ============================================================================
add_library(MyModule SHARED
    src/modules/MyModule.cpp
)

target_link_libraries(MyModule PRIVATE
    GroveEngine::impl
    spdlog::spdlog
)

set_target_properties(MyModule PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
)

# ============================================================================
# Copy config files to build directory
# ============================================================================
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config/
     DESTINATION ${CMAKE_BINARY_DIR}/config)

# ============================================================================
# Convenience targets
# ============================================================================
add_custom_target(modules
    DEPENDS MyModule
    COMMENT "Building hot-reloadable modules only"
)
```

### Linking GroveEngine

Option 1: Git submodule
```bash
git submodule add <grove-engine-repo> external/GroveEngine
```

Option 2: Symlink (local development)
```bash
ln -s /path/to/GroveEngine external/GroveEngine
```

---

## Creating Modules

### Module Header

```cpp
// src/modules/MyModule.h
#pragma once

#include <grove/IModule.h>
#include <grove/IDataNode.h>
#include <grove/IIO.h>
#include <spdlog/spdlog.h>
#include <memory>

namespace myapp {

class MyModule : public grove::IModule {
public:
    MyModule();

    // Required IModule interface
    void process(const grove::IDataNode& input) override;
    void setConfiguration(const grove::IDataNode& configNode,
                          grove::IIO* io,
                          grove::ITaskScheduler* scheduler) override;
    const grove::IDataNode& getConfiguration() override;
    std::unique_ptr<grove::IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<grove::IDataNode> getState() override;
    void setState(const grove::IDataNode& state) override;
    std::string getType() const override { return "mymodule"; }
    bool isIdle() const override { return true; }

private:
    std::shared_ptr<spdlog::logger> m_logger;
    std::unique_ptr<grove::IDataNode> m_config;
    grove::IIO* m_io = nullptr;

    // Module state (will be preserved across hot-reloads)
    int m_counter = 0;
    std::string m_status;
};

} // namespace myapp

// Required C exports for dynamic loading
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
```

### Module Implementation

```cpp
// src/modules/MyModule.cpp
#include "MyModule.h"
#include <grove/JsonDataNode.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace myapp {

MyModule::MyModule() {
    m_logger = spdlog::get("MyModule");
    if (!m_logger) {
        m_logger = spdlog::stdout_color_mt("MyModule");
    }
    m_config = std::make_unique<grove::JsonDataNode>("config");
}

void MyModule::setConfiguration(const grove::IDataNode& configNode,
                                 grove::IIO* io,
                                 grove::ITaskScheduler* scheduler) {
    m_io = io;
    m_config = std::make_unique<grove::JsonDataNode>("config");

    // Read configuration values with defaults
    m_status = configNode.getString("initialStatus", "ready");
    int startCount = configNode.getInt("startCount", 0);

    m_logger->info("MyModule configured: status={}, startCount={}",
                   m_status, startCount);
}

const grove::IDataNode& MyModule::getConfiguration() {
    return *m_config;
}

void MyModule::process(const grove::IDataNode& input) {
    // Get frame timing from input
    double deltaTime = input.getDouble("deltaTime", 0.016);
    int frameCount = input.getInt("frameCount", 0);

    // Your processing logic here
    m_counter++;

    // Process incoming messages
    while (m_io && m_io->hasMessages() > 0) {
        auto msg = m_io->pullMessage();
        m_logger->debug("Received message on topic: {}", msg.topic);
        // Handle message...
    }

    // Publish events if needed
    if (m_counter % 100 == 0) {
        auto event = std::make_unique<grove::JsonDataNode>("event");
        event->setInt("counter", m_counter);
        m_io->publish("mymodule:milestone", std::move(event));
    }
}

std::unique_ptr<grove::IDataNode> MyModule::getHealthStatus() {
    auto status = std::make_unique<grove::JsonDataNode>("health");
    status->setString("status", "running");
    status->setInt("counter", m_counter);
    return status;
}

void MyModule::shutdown() {
    m_logger->info("MyModule shutting down, counter={}", m_counter);
}

// ============================================================================
// State Serialization (Critical for Hot-Reload)
// ============================================================================

std::unique_ptr<grove::IDataNode> MyModule::getState() {
    auto state = std::make_unique<grove::JsonDataNode>("state");

    // Serialize all state that must survive hot-reload
    state->setInt("counter", m_counter);
    state->setString("status", m_status);

    m_logger->debug("State saved: counter={}", m_counter);
    return state;
}

void MyModule::setState(const grove::IDataNode& state) {
    // Restore state after hot-reload
    m_counter = state.getInt("counter", 0);
    m_status = state.getString("status", "ready");

    m_logger->info("State restored: counter={}", m_counter);
}

} // namespace myapp

// ============================================================================
// C Export Functions (Required for dlopen)
// ============================================================================

extern "C" {

grove::IModule* createModule() {
    return new myapp::MyModule();
}

void destroyModule(grove::IModule* module) {
    delete module;
}

}
```

---

## Module Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│                        Module Lifecycle                          │
└─────────────────────────────────────────────────────────────────┘

1. LOAD
   ModuleLoader::load(path, name)
        │
        ▼
   dlopen() → createModule()
        │
        ▼
   setConfiguration(config, io, scheduler)
        │
        ▼
   Module Ready

2. PROCESS (Main Loop)
   ┌──────────────────────┐
   │  process(input)      │◄────┐
   │    - Read deltaTime  │     │
   │    - Update state    │     │
   │    - Pull messages   │     │
   │    - Publish events  │     │
   └──────────────────────┘     │
            │                    │
            └────────────────────┘

3. HOT-RELOAD
   File change detected
        │
        ▼
   getState() → Save state
        │
        ▼
   unload() → dlclose()
        │
        ▼
   load(path, name, isReload=true)
        │
        ▼
   setConfiguration(config, io, scheduler)
        │
        ▼
   setState(savedState)
        │
        ▼
   Module continues with preserved state

4. SHUTDOWN
   shutdown()
        │
        ▼
   destroyModule()
        │
        ▼
   dlclose()
```

---

## Inter-Module Communication

### IIO Pub/Sub System

Modules communicate via topics using publish/subscribe pattern.

#### Publishing Messages

```cpp
void MyModule::process(const grove::IDataNode& input) {
    // Create message data
    auto data = std::make_unique<grove::JsonDataNode>("data");
    data->setString("event", "player_moved");
    data->setDouble("x", 100.5);
    data->setDouble("y", 200.3);

    // Publish to topic
    m_io->publish("game:player:position", std::move(data));
}
```

#### Subscribing to Topics

```cpp
void MyModule::setConfiguration(const grove::IDataNode& configNode,
                                 grove::IIO* io,
                                 grove::ITaskScheduler* scheduler) {
    m_io = io;

    // Subscribe to specific topic
    m_io->subscribe("game:player:*");

    // Subscribe with low-frequency batching (for non-critical updates)
    grove::SubscriptionConfig config;
    config.batchInterval = 1000;  // 1 second batches
    m_io->subscribeLowFreq("analytics:*", config);
}
```

#### Processing Messages

```cpp
void MyModule::process(const grove::IDataNode& input) {
    // Pull-based: module controls when to process messages
    while (m_io->hasMessages() > 0) {
        grove::Message msg = m_io->pullMessage();

        if (msg.topic == "game:player:position") {
            double x = msg.data->getDouble("x", 0.0);
            double y = msg.data->getDouble("y", 0.0);
            // Handle position update...
        }
    }
}
```

### Topic Patterns

Topics support wildcard matching:

| Pattern | Matches |
|---------|---------|
| `game:player:*` | `game:player:position`, `game:player:health` |
| `economy:*` | `economy:prices`, `economy:trade` |
| `*:error` | `network:error`, `database:error` |

---

## Hot-Reload

### How It Works

1. **File Watcher** detects `.so` modification
2. **State Extraction**: `getState()` serializes module state
3. **Unload**: Old library closed with `dlclose()`
4. **Load**: New library loaded with `dlopen()` (cache bypass via temp copy)
5. **Configure**: `setConfiguration()` called with same config
6. **Restore**: `setState()` restores serialized state

### Implementing Hot-Reload Support

```cpp
// Critical: Serialize ALL state that must survive reload
std::unique_ptr<grove::IDataNode> MyModule::getState() {
    auto state = std::make_unique<grove::JsonDataNode>("state");

    // Primitives
    state->setInt("counter", m_counter);
    state->setDouble("health", m_health);
    state->setString("name", m_name);
    state->setBool("active", m_active);

    // Complex state: serialize to JSON child nodes
    auto entitiesNode = std::make_unique<grove::JsonDataNode>("entities");
    for (const auto& entity : m_entities) {
        auto entityNode = std::make_unique<grove::JsonDataNode>(entity.id);
        entityNode->setDouble("x", entity.x);
        entityNode->setDouble("y", entity.y);
        entitiesNode->setChild(entity.id, std::move(entityNode));
    }
    state->setChild("entities", std::move(entitiesNode));

    return state;
}

void MyModule::setState(const grove::IDataNode& state) {
    // Restore primitives
    m_counter = state.getInt("counter", 0);
    m_health = state.getDouble("health", 100.0);
    m_name = state.getString("name", "default");
    m_active = state.getBool("active", true);

    // Restore complex state
    m_entities.clear();
    auto* entitiesNode = state.getChildReadOnly("entities");
    if (entitiesNode) {
        for (const auto& name : entitiesNode->getChildNames()) {
            auto* entityNode = entitiesNode->getChildReadOnly(name);
            if (entityNode) {
                Entity e;
                e.id = name;
                e.x = entityNode->getDouble("x", 0.0);
                e.y = entityNode->getDouble("y", 0.0);
                m_entities.push_back(e);
            }
        }
    }
}
```

### File Watcher Example

```cpp
class FileWatcher {
public:
    void watch(const std::string& path) {
        if (fs::exists(path)) {
            m_lastModified[path] = fs::last_write_time(path);
        }
    }

    bool hasChanged(const std::string& path) {
        if (!fs::exists(path)) return false;

        auto currentTime = fs::last_write_time(path);
        auto it = m_lastModified.find(path);

        if (it == m_lastModified.end()) {
            m_lastModified[path] = currentTime;
            return false;
        }

        if (currentTime != it->second) {
            it->second = currentTime;
            return true;
        }
        return false;
    }

private:
    std::unordered_map<std::string, fs::file_time_type> m_lastModified;
};
```

---

## Configuration Management

### JSON Configuration Files

```json
// config/mymodule.json
{
    "initialStatus": "ready",
    "startCount": 0,
    "maxItems": 100,
    "debugMode": false,
    "updateRate": 0.016
}
```

### Loading Configuration

```cpp
std::unique_ptr<grove::JsonDataNode> loadConfig(const std::string& path) {
    if (fs::exists(path)) {
        std::ifstream file(path);
        nlohmann::json j;
        file >> j;
        return std::make_unique<grove::JsonDataNode>("config", j);
    }
    // Return empty config with defaults
    return std::make_unique<grove::JsonDataNode>("config");
}
```

### Runtime Config Updates

Modules can optionally support runtime configuration changes:

```cpp
bool MyModule::updateConfig(const grove::IDataNode& newConfig) {
    // Validate new config
    int maxItems = newConfig.getInt("maxItems", 100);
    if (maxItems < 1 || maxItems > 10000) {
        m_logger->warn("Invalid maxItems: {}", maxItems);
        return false;
    }

    // Apply changes
    m_maxItems = maxItems;
    m_debugMode = newConfig.getBool("debugMode", false);

    m_logger->info("Config updated: maxItems={}", m_maxItems);
    return true;
}
```

---

## Task Scheduling

For computationally expensive operations, delegate to `ITaskScheduler`:

```cpp
void MyModule::setConfiguration(const grove::IDataNode& configNode,
                                 grove::IIO* io,
                                 grove::ITaskScheduler* scheduler) {
    m_scheduler = scheduler;  // Store reference
}

void MyModule::process(const grove::IDataNode& input) {
    // Delegate expensive pathfinding calculation
    if (needsPathfinding) {
        auto taskData = std::make_unique<grove::JsonDataNode>("task");
        taskData->setDouble("startX", unit.x);
        taskData->setDouble("startY", unit.y);
        taskData->setDouble("targetX", target.x);
        taskData->setDouble("targetY", target.y);
        taskData->setString("unitId", unit.id);

        m_scheduler->scheduleTask("pathfinding", std::move(taskData));
    }

    // Check for completed tasks
    while (m_scheduler->hasCompletedTasks() > 0) {
        auto result = m_scheduler->getCompletedTask();
        std::string unitId = result->getString("unitId", "");
        // Apply pathfinding result...
    }
}
```

---

## API Reference

### IDataNode

| Method | Description |
|--------|-------------|
| `getString(name, default)` | Get string property |
| `getInt(name, default)` | Get integer property |
| `getDouble(name, default)` | Get double property |
| `getBool(name, default)` | Get boolean property |
| `setString(name, value)` | Set string property |
| `setInt(name, value)` | Set integer property |
| `setDouble(name, value)` | Set double property |
| `setBool(name, value)` | Set boolean property |
| `hasProperty(name)` | Check if property exists |
| `getChild(name)` | Get child node (transfers ownership) |
| `getChildReadOnly(name)` | Get child node (no ownership transfer) |
| `setChild(name, node)` | Add/replace child node |
| `getChildNames()` | Get names of all children |

### IIO

| Method | Description |
|--------|-------------|
| `publish(topic, data)` | Publish message to topic |
| `subscribe(pattern, config)` | Subscribe to topic pattern |
| `subscribeLowFreq(pattern, config)` | Subscribe with batching |
| `hasMessages()` | Count of pending messages |
| `pullMessage()` | Consume one message |
| `getHealth()` | Get IO health metrics |

### IModule

| Method | Description |
|--------|-------------|
| `process(input)` | Main processing method |
| `setConfiguration(config, io, scheduler)` | Initialize module |
| `getConfiguration()` | Get current config |
| `getState()` | Serialize state for hot-reload |
| `setState(state)` | Restore state after hot-reload |
| `getHealthStatus()` | Get module health report |
| `shutdown()` | Clean shutdown |
| `isIdle()` | Check if safe to hot-reload |
| `getType()` | Get module type identifier |

### ModuleLoader

| Method | Description |
|--------|-------------|
| `load(path, name, isReload)` | Load module from .so |
| `reload(module)` | Hot-reload with state preservation |
| `unload()` | Unload current module |
| `isLoaded()` | Check if module is loaded |
| `getLoadedPath()` | Get path of loaded module |

### IOFactory

| Method | Description |
|--------|-------------|
| `create(type, instanceId)` | Create IO by type string |
| `create(IOType, instanceId)` | Create IO by enum |
| `createFromConfig(config, instanceId)` | Create IO from config |

**IO Types:**
- `"intra"` / `IOType::INTRA` - Same-process (development)
- `"local"` / `IOType::LOCAL` - Same-machine (production single-server)
- `"network"` / `IOType::NETWORK` - Distributed (MMO scale)

---

## Building and Running

```bash
# Configure
cmake -B build

# Build everything
cmake --build build -j4

# Build modules only (for hot-reload workflow)
cmake --build build --target modules

# Run
./build/myapp
```

### Hot-Reload Workflow

1. Start application: `./build/myapp`
2. Edit module source: `src/modules/MyModule.cpp`
3. Rebuild module: `cmake --build build --target modules`
4. Application detects change and hot-reloads automatically

---

## Best Practices

1. **Keep modules small**: 200-300 lines of pure business logic
2. **No infrastructure code**: Let GroveEngine handle threading, persistence
3. **Serialize all state**: Everything in `getState()` survives hot-reload
4. **Use typed accessors**: `getInt()`, `getString()` with sensible defaults
5. **Pull-based messaging**: Process messages in `process()`, not callbacks
6. **Validate config**: Check configuration values in `setConfiguration()`
7. **Log appropriately**: Debug for development, Info for production events
