# Synchronization Guidelines - GroveEngine

## Overview

This document describes the thread-safety patterns used in GroveEngine to prevent deadlocks and optimize concurrent access.

## Mutex Types

### std::mutex
Use for simple, exclusive access to resources with balanced read/write patterns.

### std::shared_mutex (C++17)
Use for **read-heavy workloads** where multiple readers can access data concurrently.
- `std::shared_lock` - Allows concurrent reads
- `std::unique_lock` - Exclusive write access

## DO: Use std::scoped_lock for multiple mutexes

When you need to lock multiple mutexes, **always** use `std::scoped_lock` to prevent deadlock via lock-order-inversion:

```cpp
void function() {
    std::scoped_lock lock(mutex1, mutex2, mutex3);
    // Safe - lock order guaranteed by implementation (deadlock-free algorithm)
}
```

## DON'T: Use std::lock_guard for multiple mutexes

```cpp
void function() {
    std::lock_guard<std::mutex> lock1(mutex1);  // BAD
    std::lock_guard<std::mutex> lock2(mutex2);  // DEADLOCK RISK
    // If another thread locks in reverse order -> deadlock
}
```

## DO: Use std::unique_lock with std::lock if you need early unlock

```cpp
void function() {
    std::unique_lock<std::mutex> lock1(mutex1, std::defer_lock);
    std::unique_lock<std::mutex> lock2(mutex2, std::defer_lock);
    std::lock(lock1, lock2);  // Safe deadlock-free acquisition

    // ... do work ...

    // Can unlock early if needed
    lock1.unlock();
}
```

## DO: Use shared_lock for read operations

```cpp
class DataStore {
    mutable std::shared_mutex mutex;
    std::map<std::string, Data> data;

public:
    Data get(const std::string& key) const {
        std::shared_lock lock(mutex);  // Multiple readers allowed
        return data.at(key);
    }

    void set(const std::string& key, Data value) {
        std::unique_lock lock(mutex);  // Exclusive write access
        data[key] = std::move(value);
    }
};
```

## Read/Write Ratio Guidelines

| Ratio | Recommendation |
|-------|---------------|
| >10:1 (read-heavy) | Use `std::shared_mutex` |
| 1:1 to 10:1 | Consider `std::shared_mutex` |
| <1:1 (write-heavy) | Use `std::mutex` (shared_mutex overhead not worth it) |

## GroveEngine Specific Patterns

### TopicTree
- Uses `std::shared_mutex`
- `findSubscribers()` - `shared_lock` (READ)
- `registerSubscriber()` - `unique_lock` (WRITE)
- `unregisterSubscriber()` - `unique_lock` (WRITE)

### IntraIOManager
- Uses `std::shared_mutex` for instances map
- Uses `std::scoped_lock` when both `managerMutex` and `batchMutex` needed
- `getInstance()` - `shared_lock` (READ)
- `createInstance()` - `unique_lock` (WRITE)
- `routeMessage()` - `scoped_lock` (both mutexes)

## Validation Tools

### ThreadSanitizer (TSan)
Build with TSan to detect lock-order-inversions and data races:
```bash
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan
cmake --build build-tsan
TSAN_OPTIONS="detect_deadlocks=1" ctest
```

### Helgrind
Alternative detector via Valgrind:
```bash
cmake -DGROVE_ENABLE_HELGRIND=ON -B build
cmake --build build
make helgrind
```

## Common Pitfalls

1. **Nested lock calls** - Don't call a function that takes a lock while holding the same lock
2. **Lock-order-inversion** - Always use `scoped_lock` for multiple mutexes
3. **shared_lock in write context** - Never use shared_lock when modifying data
4. **Recursive locking** - Avoid; redesign if needed

---
**Author**: Claude Code
**Date**: 2025-01-21
