# DataNode System Architecture Analysis

## System Overview

The DataNode system is a hierarchical data management framework for the GroveEngine, providing unified access to configuration, persistent data, and runtime state. It's a complete abstraction layer separating data concerns from business logic.

---

## 1. Core Architecture

### Three-Tier System

```
IDataTree (Root Container)
    ├── config/  (Read-only, hot-reload enabled)
    ├── data/    (Read-write, persistent)
    └── runtime/ (Read-write, temporary)
```

### Architectural Layers

```
Layer 1: Interfaces (Abstract)
├── IDataValue  - Type-safe value wrapper
├── IDataNode   - Tree node with navigation and modification
└── IDataTree   - Root container with save/reload operations

Layer 2: Concrete Implementations
├── JsonDataValue   - nlohmann::json backed value
├── JsonDataNode    - JSON tree node with full features
└── JsonDataTree    - File-based JSON storage

Layer 3: Module System Integration
└── IModuleSystem   - Owns IDataTree, manages save/reload

Layer 4: Distributed Coordination
├── CoordinationModule (Master) - Hot-reload detection
└── DebugEngine (Workers)       - Config synchronization
```

---

## 2. Key Classes and Responsibilities

### IDataValue Interface
**Location**: `/mnt/c/Users/alexi/Documents/projects/groveengine/include/grove/IDataValue.h`

**Responsibility**: Abstract data value with type-safe access

**Key Methods**:
- Type checking: `isNull()`, `isBool()`, `isNumber()`, `isString()`, `isArray()`, `isObject()`
- Conversion: `asBool()`, `asInt()`, `asDouble()`, `asString()`
- Access: `get(index)`, `get(key)`, `has(key)`, `size()`
- Serialization: `toString()`

**Why It Exists**: Allows modules to work with values without exposing JSON format, enabling future implementations (binary, database, etc.)

---

### JsonDataValue Implementation
**Location**: `/mnt/c/Users/alexi/Documents/projects/groveengine/include/grove/JsonDataValue.h`
**Implementation**: `/mnt/c/Users/alexi/Documents/projects/groveengine/src/JsonDataValue.cpp`

**Concrete Implementation**: Backed by `nlohmann::json`

**Key Features**:
- Transparent JSON wrapping
- Direct JSON access for internal use: `getJson()`, `getJson()`
- All interface methods delegated to JSON type system
- No conversion overhead (move semantics)

---

### IDataNode Interface
**Location**: `/mnt/c/Users/alexi/Documents/projects/groveengine/include/grove/IDataNode.h` (259 lines)

**Responsibility**: Single tree node with hierarchical navigation, search, and modification

**Major Capabilities**:

#### 1. Tree Navigation
```cpp
std::unique_ptr<IDataNode> getChild(const std::string& name)
std::vector<std::string> getChildNames()
bool hasChildren()
```

#### 2. Exact Search (Direct Children Only)
```cpp
std::vector<IDataNode*> getChildrenByName(const std::string& name)
bool hasChildrenByName(const std::string& name) const
IDataNode* getFirstChildByName(const std::string& name)
```

#### 3. Pattern Matching (Deep Subtree Search)
```cpp
// Examples: "component*", "*heavy*", "model_*"
std::vector<IDataNode*> getChildrenByNameMatch(const std::string& pattern)
bool hasChildrenByNameMatch(const std::string& pattern) const
IDataNode* getFirstChildByNameMatch(const std::string& pattern)
```

#### 4. Property-Based Queries (Functional)
```cpp
std::vector<IDataNode*> queryByProperty(const std::string& propName,
    const std::function<bool(const IDataValue&)>& predicate)

// Example: Find all tanks with armor > 150
auto heavy = root->queryByProperty("armor", 
    [](const IDataValue& val) {
        return val.isNumber() && val.asInt() > 150;
    });
```

#### 5. Typed Data Access
```cpp
std::string getString(const std::string& name, const std::string& default = "")
int getInt(const std::string& name, int default = 0)
double getDouble(const std::string& name, double default = 0.0)
bool getBool(const std::string& name, bool default = false)
bool hasProperty(const std::string& name)
```

#### 6. Hash System (Validation & Synchronization)
```cpp
std::string getDataHash()      // SHA256 of this node's data
std::string getTreeHash()      // SHA256 of entire subtree
std::string getSubtreeHash(const std::string& childPath)  // Specific child
```

**Use Cases**:
- Validate config hasn't been corrupted
- Detect changes for synchronization
- Fast change detection without full tree comparison

#### 7. Node Data Management
```cpp
std::unique_ptr<IDataValue> getData() const
bool hasData() const
void setData(std::unique_ptr<IDataValue> data)
```

#### 8. Tree Modification
```cpp
void setChild(const std::string& name, std::unique_ptr<IDataNode> node)
bool removeChild(const std::string& name)
void clearChildren()
```

**Restrictions**: Only works on data/ and runtime/ nodes. Config nodes are read-only.

#### 9. Metadata
```cpp
std::string getPath() const      // Full path: "vehicles/tanks/heavy"
std::string getName() const      // Node name only
std::string getNodeType() const  // "JsonDataNode"
```

---

### JsonDataNode Implementation
**Location**: `/mnt/c/Users/alexi/Documents/projects/groveengine/include/grove/JsonDataNode.h` (109 lines)
**Implementation**: `/mnt/c/Users/alexi/Documents/projects/groveengine/src/JsonDataNode.cpp` (344 lines)

**Internal Structure**:
```cpp
class JsonDataNode : public IDataNode {
private:
    std::string m_name;
    json m_data;                                    // Node's own data
    JsonDataNode* m_parent;                         // Parent reference (path building)
    bool m_readOnly;                                // For config/ nodes
    std::map<std::string, std::unique_ptr<JsonDataNode>> m_children;  // Child nodes
}
```

**Key Capabilities**:

1. **Pattern Matching Implementation**
   - Converts wildcard patterns to regex: `*` → `.*`
   - Escapes all special regex chars except `*`
   - Recursive depth-first search: `collectMatchingNodes()`
   - O(n) complexity where n = subtree size

2. **Hash Computation**
   - Uses OpenSSL SHA256
   - Data hash: `SHA256(m_data.dump())`
   - Tree hash: Combined hash of data + all children
   - Format: Lowercase hex string

3. **Copy-on-Access Pattern**
   - `getChild()` returns a new unique_ptr copy
   - Preserves encapsulation
   - Enables safe distribution

4. **Read-Only Enforcement**
   - `checkReadOnly()` throws if modification attempted on config
   - Error: `"Cannot modify read-only node: " + getPath()`

---

### IDataTree Interface
**Location**: `/mnt/c/Users/alexi/Documents/projects/groveengine/include/grove/IDataTree.h` (128 lines)

**Responsibility**: Root container managing three separate trees

**Key Methods**:

#### Tree Access
```cpp
std::unique_ptr<IDataNode> getRoot()              // Everything
std::unique_ptr<IDataNode> getNode(const std::string& path)  // "config/vehicles/tanks"

// Recommended: Access separate roots
std::unique_ptr<IDataNode> getConfigRoot()       // Read-only config
std::unique_ptr<IDataNode> getDataRoot()         // Persistent data
std::unique_ptr<IDataNode> getRuntimeRoot()      // Temporary state
```

#### Save Operations
```cpp
bool saveData()                               // Save entire data/
bool saveNode(const std::string& path)        // Save specific node (data/ only)
```

#### Hot-Reload
```cpp
bool checkForChanges()                        // Check if config files changed
bool reloadIfChanged()                        // Reload if changed, fire callbacks
void onTreeReloaded(std::function<void()> callback)  // Register reload handler
```

#### Metadata
```cpp
std::string getType()  // "JsonDataTree"
```

---

### JsonDataTree Implementation
**Location**: `/mnt/c/Users/alexi/Documents/projects/groveengine/include/grove/JsonDataTree.h` (87 lines)
**Implementation**: `/mnt/c/Users/alexi/Documents/projects/groveengine/src/JsonDataTree.cpp` (partial read)

**Internal Structure**:
```cpp
class JsonDataTree : public IDataTree {
private:
    std::string m_basePath;                        // Root directory
    std::unique_ptr<JsonDataNode> m_root;          // Root container
    std::unique_ptr<JsonDataNode> m_configRoot;    // config/ subtree
    std::unique_ptr<JsonDataNode> m_dataRoot;      // data/ subtree
    std::unique_ptr<JsonDataNode> m_runtimeRoot;   // runtime/ subtree (in-memory)
    
    std::map<std::string, std::filesystem::file_time_type> m_configFileTimes;
    std::vector<std::function<void()>> m_reloadCallbacks;
}
```

**Key Features**:

1. **Initialization** (`JsonDataTree(basePath)`)
   - Creates root node
   - Calls `loadConfigTree()` from disk
   - Calls `loadDataTree()` from disk
   - Calls `initializeRuntimeTree()` (empty in-memory)
   - Attaches all three as children to root

2. **File-Based Loading** (`scanDirectory()`)
   - Recursively scans config/ and data/ directories
   - Creates JsonDataNode tree from JSON files
   - Builds hierarchical structure
   - config/ marked as read-only

3. **Hot-Reload Detection** (`checkForChanges()`)
   - Tracks file modification times
   - Detects file deletions
   - Detects new files
   - Returns bool (changed?)

4. **Hot-Reload Execution** (`reloadIfChanged()`)
   - Calls `loadConfigTree()` to reload from disk
   - Fires all registered callbacks
   - Allows modules to refresh configuration

5. **Save Operations**
   - `saveData()`: Saves data/ subtree to disk
   - `saveNode(path)`: Saves specific data/ path
   - Only allows data/ paths (read-only protection)
   - Creates JSON files matching hierarchy

---

## 3. Data Flow Patterns

### Pattern 1: Reading Configuration
```cpp
// Engine startup
auto tree = std::make_unique<JsonDataTree>("gamedata");
auto tankConfig = tree->getConfigRoot()
                    ->getChild("tanks")
                    ->getChild("heavy_mk1");

// Module receives config
void TankModule::setConfiguration(const IDataNode& config, ...) {
    m_armor = config.getInt("armor");           // Default: 0
    m_speed = config.getDouble("speed");        // Default: 0.0
    m_weaponType = config.getString("weapon_type");  // Default: ""
}
```

### Pattern 2: Saving State
```cpp
// Module creates state
auto state = std::make_unique<JsonDataNode>("state", json::object());
state->setData(std::make_unique<JsonDataValue>(
    json{{"position", {x, y}}, {"health", hp}}
));

// Engine persists
tree->getDataRoot()->setChild("tank_123", std::move(state));
tree->saveNode("data/tank_123");
```

### Pattern 3: Hot-Reload (Distributed)
```cpp
// Master: Detect and broadcast
if (masterTree->reloadIfChanged()) {
    auto config = masterTree->getConfigRoot();
    io->publish("config:reload", std::move(config));
}

// Worker: Receive and apply
auto msg = io->pullMessage();
if (msg.topic == "config:reload") {
    auto configRoot = tree->getConfigRoot();
    // Shared/const payload (zero-copy bus) → copy its json into an owned node (setChild takes unique_ptr).
    configRoot->setChild("updated",
        std::make_unique<JsonDataNode>("updated",
            dynamic_cast<const JsonDataNode&>(*msg.data).getJsonData()));
    
    // Notify modules
    for (auto& module : modules) {
        auto moduleConfig = configRoot->getChild(module->getType());
        module->setConfiguration(*moduleConfig, io, scheduler);
    }
}
```

### Pattern 4: Advanced Queries
```cpp
// Pattern matching
auto heavyUnits = root->getChildrenByNameMatch("*_heavy_*");
auto tanks = root->getChildrenByNameMatch("tank_*");

// Property-based query
auto highArmor = root->queryByProperty("armor",
    [](const IDataValue& val) {
        return val.isNumber() && val.asInt() > 150;
    });

// Hash validation
std::string oldHash = configNode->getTreeHash();
// ... later ...
if (oldHash != configNode->getTreeHash()) {
    // Config changed - refresh caches
}
```

---

## 4. Storage and Persistence

### File Structure
```
gamedata/
├── config/
│   ├── tanks.json          → JsonDataNode "tanks" with children
│   ├── weapons.json
│   └── mods/super_mod/
│       └── new_tanks.json
├── data/
│   ├── campaign_progress.json
│   ├── player_stats.json
│   └── unlocks.json
└── runtime/
    (in-memory only)
```

### JSON Format
Each node can have:
- Own data (any JSON value)
- Child nodes (files become nodes)
- Properties (key-value pairs in object data)

**Example tanks.json**:
```json
{
  "heavy_mk1": {
    "armor": 200,
    "speed": 15.5,
    "weapon_type": "cannon_105mm"
  },
  "medium_t_72": {
    "armor": 140,
    "speed": 60.0,
    "weapon_type": "cannon_125mm"
  }
}
```

**Tree Structure**:
```
config/
└── tanks
    ├── heavy_mk1 (data: {armor: 200, ...})
    └── medium_t_72 (data: {armor: 140, ...})
```

---

## 5. Synchronization Mechanisms

### Hash System
- **Data Hash**: Validates single node integrity
- **Tree Hash**: Validates subtree (change detection)
- **Subtree Hash**: Validates specific child path

**Use Case**: Quick change detection without tree traversal

### Hot-Reload System
1. `checkForChanges()` - File timestamp comparison
2. `reloadIfChanged()` - Reload + callback firing
3. `onTreeReloaded()` - Register callback handlers

**Distributed Pattern**:
- Master detects changes
- Broadcasts new config via IIO
- Workers apply synchronized updates
- Modules notified via `setConfiguration()`

### Read-Only Enforcement
- config/ nodes: Immutable by modules
- data/ nodes: Writable by modules
- runtime/ nodes: Always writable

---

## 6. Synchronization Features

### Thread Safety Considerations
Current implementation is **NOT thread-safe**:
- No mutex protection in JsonDataNode
- No mutex protection in JsonDataTree
- Concurrent access requires external synchronization

**Recommended Pattern**:
```cpp
std::mutex treeMutex;

// Reader
std::lock_guard<std::mutex> lock(treeMutex);
auto data = tree->getNode(path);

// Writer
std::lock_guard<std::mutex> lock(treeMutex);
tree->getDataRoot()->setChild("path", std::move(node));
tree->saveData();
```

### Copy Semantics
All getters return unique_ptr copies (not references):
- `getChild()` → new JsonDataNode copy
- `getData()` → new JsonDataValue copy
- Protects internal state
- Enables safe distribution

---

## 7. Existing Tests

### Test Files Found
- `/mnt/c/Users/alexi/Documents/projects/groveengine/tests/integration/test_04_race_condition.cpp`
  - Tests concurrent compilation and hot-reload
  - Uses JsonDataNode for configuration
  - Tests module integrity validation
  - Tests concurrent access patterns

### Test Scenarios (planTI/)
1. **scenario_01_production_hotreload.md** - Hot-reload validation
2. **scenario_02_chaos_monkey.md** - Random failure injection
3. **scenario_03_stress_test.md** - Load testing
4. **scenario_04_race_condition.md** - Concurrency testing
5. **scenario_05_multimodule.md** - Multi-module coordination
6. **scenario_07_limits.md** - Extreme conditions
7. **scenario_06_error_recovery.md** - Error handling

---

## 8. Critical Features Requiring Integration Tests

### 1. Tree Navigation & Search
**What Needs Testing**:
- Tree construction from file system
- Exact name matching (getChildrenByName)
- Pattern matching with wildcards
- Deep subtree search efficiency
- Path building and navigation
- Edge cases: empty names, special characters, deep nesting

**Test Scenarios**:
```cpp
// Test exact matching
auto tanks = root->getChildrenByName("tanks");
assert(tanks.size() == 1);

// Test pattern matching
auto heavy = root->getChildrenByNameMatch("*heavy*");
assert(heavy.size() == 2);  // e.g., heavy_mk1, tank_heavy_v2

// Test deep navigation
auto node = root->getChild("vehicles")->getChild("tanks")->getChild("heavy");
assert(node != nullptr);
```

### 2. Data Persistence & Save/Load
**What Needs Testing**:
- Save entire data/ tree
- Save specific nodes
- Load from disk
- Nested structure preservation
- Data type preservation (numbers, strings, booleans, arrays)
- Empty node handling
- Large data handling (1MB+)
- File corruption recovery

**Test Scenarios**:
```cpp
// Create and save
auto node = std::make_unique<JsonDataNode>("player", json::object());
tree->getDataRoot()->setChild("player1", std::move(node));
assert(tree->saveNode("data/player1"));

// Reload and verify
auto reloaded = tree->getDataRoot()->getChild("player1");
assert(reloaded != nullptr);
assert(reloaded->hasData());
```

### 3. Hot-Reload System
**What Needs Testing**:
- File change detection
- Config reload accuracy
- Callback execution
- Multiple callback handling
- Timing consistency
- No data/ changes during reload
- No runtime/ changes during reload
- Rapid successive reloads

**Test Scenarios**:
```cpp
// Register callback
bool callbackFired = false;
tree->onTreeReloaded([&]() { callbackFired = true; });

// Modify config file
modifyConfigFile("config/tanks.json");
std::this_thread::sleep_for(10ms);

// Trigger reload
assert(tree->reloadIfChanged());
assert(callbackFired);
```

### 4. Property-Based Queries
**What Needs Testing**:
- Predicate evaluation
- Type-safe access
- Complex predicates (AND, OR)
- Performance with large datasets
- Empty result sets
- Single result matches
- Null value handling

**Test Scenarios**:
```cpp
// Query by numeric property
auto armored = root->queryByProperty("armor",
    [](const IDataValue& val) {
        return val.isNumber() && val.asInt() >= 150;
    });
assert(armored.size() >= 1);

// Query by string property
auto cannons = root->queryByProperty("weapon",
    [](const IDataValue& val) {
        return val.isString() && val.asString().find("cannon") != std::string::npos;
    });
```

### 5. Hash System & Validation
**What Needs Testing**:
- Hash consistency (same data = same hash)
- Hash change detection
- Tree hash includes all children
- Subtree hash isolation
- Hash format (lowercase hex, 64 chars for SHA256)
- Performance of hash computation
- Deep tree hashing

**Test Scenarios**:
```cpp
auto hash1 = node->getDataHash();
auto hash2 = node->getDataHash();
assert(hash1 == hash2);  // Consistent

// Modify data
node->setData(...);
auto hash3 = node->getDataHash();
assert(hash1 != hash3);  // Changed

// Tree hash includes children
auto treeHash1 = node->getTreeHash();
node->setChild("new", ...);
auto treeHash2 = node->getTreeHash();
assert(treeHash1 != treeHash2);  // Child change detected
```

### 6. Read-Only Enforcement
**What Needs Testing**:
- config/ nodes reject modifications
- data/ nodes allow modifications
- runtime/ nodes allow modifications
- Exception on modification attempt
- Error message contains path
- Read-only flag propagation to children
- Inherited read-only status

**Test Scenarios**:
```cpp
auto configNode = tree->getConfigRoot();
assert_throws<std::runtime_error>([&]() {
    configNode->setChild("new", std::make_unique<JsonDataNode>("x", json::object()));
});

auto dataNode = tree->getDataRoot();
dataNode->setChild("new", std::make_unique<JsonDataNode>("x", json::object()));  // OK
```

### 7. Type Safety & Data Access
**What Needs Testing**:
- getString with default fallback
- getInt with type coercion
- getDouble precision
- getBool parsing
- hasProperty existence check
- Wrong type access returns default
- Null handling
- Array/object access edge cases

**Test Scenarios**:
```cpp
auto node = ...;  // Has {"armor": 200, "speed": 60.5, "active": true}

assert(node->getInt("armor") == 200);
assert(node->getDouble("speed") == 60.5);
assert(node->getBool("active") == true);
assert(node->getString("name", "default") == "default");  // Missing key

assert(node->hasProperty("armor"));
assert(!node->hasProperty("missing"));
```

### 8. Concurrent Access Patterns
**What Needs Testing**:
- Safe reader access (multiple threads reading simultaneously)
- Safe writer access (single writer with lock)
- Race condition detection
- No data corruption under load
- Reload safety during concurrent reads
- No deadlocks

**Test Scenarios**:
```cpp
std::mutex treeMutex;
std::vector<std::thread> readers;

for (int i = 0; i < 10; ++i) {
    readers.emplace_back([&]() {
        std::lock_guard<std::mutex> lock(treeMutex);
        auto data = tree->getConfigRoot()->getChild("tanks");
        assert(data != nullptr);
    });
}

for (auto& t : readers) t.join();
```

### 9. Error Handling & Edge Cases
**What Needs Testing**:
- Invalid paths (non-existent nodes)
- Empty names
- Special characters in names
- Null data nodes
- Circular reference prevention
- Memory cleanup on exception
- File system errors (permissions, disk full)
- Corrupted JSON recovery

**Test Scenarios**:
```cpp
// Non-existent node
auto missing = tree->getNode("config/does/not/exist");
assert(missing == nullptr);

// Empty name
auto node = std::make_unique<JsonDataNode>("", json::object());
assert(node->getName() == "");
assert(node->getPath() == "");  // Root-like behavior
```

### 10. Performance & Scale
**What Needs Testing**:
- Large tree navigation (1000+ nodes)
- Deep nesting (100+ levels)
- Pattern matching performance
- Hash computation speed
- File I/O performance
- Memory usage
- Reload speed

**Test Scenarios**:
```cpp
// Create large tree
auto root = std::make_unique<JsonDataNode>("root", json::object());
for (int i = 0; i < 1000; ++i) {
    root->setChild("child_" + std::to_string(i),
        std::make_unique<JsonDataNode>("x", json::object()));
}

// Benchmark pattern matching
auto start = std::chrono::high_resolution_clock::now();
auto results = root->getChildrenByNameMatch("child_*");
auto end = std::chrono::high_resolution_clock::now();

assert(results.size() == 1000);
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
assert(duration < 100);  // Should be fast
```

---

## Summary

The DataNode system is a complete, production-ready data management framework providing:

1. **Three-tier abstraction** (Interface → Implementation → Integration)
2. **Hierarchical organization** (config/, data/, runtime/)
3. **Advanced queries** (exact matching, pattern matching, property-based)
4. **Hash-based validation** (change detection, integrity checking)
5. **Hot-reload support** (file monitoring, callback system)
6. **Type-safe access** (IDataValue interface with coercion)
7. **Read-only enforcement** (configuration immutability)
8. **Persistence layer** (file-based save/load)

**Critical missing piece**: No integration tests specifically for DataNode system. All existing tests focus on module loading, hot-reload, and race conditions, but not on DataNode functionality itself.

---

