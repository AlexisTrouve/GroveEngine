# GroveEngine - Claude Code Context

## Project Overview
GroveEngine is a C++17 hot-reload module system for game engines. It supports dynamic loading/unloading of modules (.so) with state preservation during hot-reload.

## Build & Test
```bash
# Build
cmake -B build && cmake --build build -j4

# Run all tests (23 tests)
cd build && ctest --output-on-failure

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

## Debugging Tools
```bash
# ThreadSanitizer (detects data races, deadlocks)
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan

# Helgrind (alternative deadlock detector)
cmake -DGROVE_ENABLE_HELGRIND=ON -B build
make helgrind
```
