# TopicTree - Test Integration Plan

## Overview
Complete integration test suite for TopicTree ultra-fast topic matching library.

**Goal**: Validate all features with 10 comprehensive test scenarios covering functionality, performance, thread-safety, and edge cases.

---

## Test Scenarios

### **Scenario 1: Basic Exact Matching**
**Objective**: Verify exact topic matching without wildcards

**Steps**:
1. Register pattern `"player:001:position"` → subscriber1
2. Publish `"player:001:position"` → expect `[subscriber1]`
3. Publish `"player:002:position"` → expect `[]` (no match)
4. Publish `"player:001:health"` → expect `[]` (different topic)

**Expected**: Only exact matches are returned

---

### **Scenario 2: Single Wildcard at Different Positions**
**Objective**: Validate `*` wildcard matching at various segment positions

**Steps**:
1. Register `"player:*:position"` → sub1
2. Register `"*:001:health"` → sub2
3. Register `"enemy:*:*"` → sub3
4. Publish `"player:123:position"` → expect `[sub1]`
5. Publish `"player:001:health"` → expect `[sub2]`
6. Publish `"enemy:boss:health"` → expect `[sub3]`
7. Publish `"player:999:health"` → expect `[]`

**Expected**: Wildcards match any single segment at their position

---

### **Scenario 3: Multi-Level Wildcard Matching**
**Objective**: Validate `.*` wildcard matching remaining segments

**Steps**:
1. Register `"player:.*"` → sub1
2. Register `"game:.*"` → sub2
3. Publish `"player:001"` → expect `[sub1]`
4. Publish `"player:001:position"` → expect `[sub1]`
5. Publish `"player:001:stats:armor"` → expect `[sub1]`
6. Publish `"game:level:01:start"` → expect `[sub2]`
7. Publish `"enemy:001"` → expect `[]`

**Expected**: `.*` matches all remaining segments after prefix

---

### **Scenario 4: Overlapping Patterns**
**Objective**: Multiple patterns can match the same topic

**Steps**:
1. Register `"player:001:position"` → exactSub
2. Register `"player:*:position"` → wildcardSub
3. Register `"player:.*"` → multiSub
4. Publish `"player:001:position"` → expect `[exactSub, wildcardSub, multiSub]`
5. Publish `"player:002:position"` → expect `[wildcardSub, multiSub]`
6. Publish `"player:001:health"` → expect `[multiSub]`

**Expected**: All matching patterns return their subscribers

---

### **Scenario 5: Unregister Specific Pattern**
**Objective**: Selective pattern removal for a subscriber

**Steps**:
1. Register `"player:*:health"` → sub1
2. Register `"player:.*"` → sub2
3. Publish `"player:001:health"` → expect `[sub1, sub2]`
4. Unregister pattern `"player:*:health"` for sub1
5. Publish `"player:001:health"` → expect `[sub2]` only
6. Publish `"player:002:health"` → expect `[sub2]` only

**Expected**: Only specified pattern is removed, others remain

---

### **Scenario 6: Unregister All Patterns for Subscriber**
**Objective**: Remove subscriber from all registered patterns

**Steps**:
1. Register `"player:*:position"` → sub1
2. Register `"enemy:*:health"` → sub1
3. Register `"game:.*"` → sub1
4. Register `"player:*:position"` → sub2 (different subscriber)
5. Publish `"player:001:position"` → expect `[sub1, sub2]`
6. UnregisterAll sub1
7. Publish `"player:001:position"` → expect `[sub2]`
8. Publish `"enemy:boss:health"` → expect `[]`

**Expected**: sub1 removed from all patterns, sub2 unaffected

---

### **Scenario 7: Deep Topic Hierarchies**
**Objective**: Handle topics with many segments (8+ levels)

**Steps**:
1. Register `"game:world:region:zone:*:entity:player"` → sub1
2. Register `"game:world:*:zone:*:entity:player"` → sub2
3. Register `"game:.*"` → sub3
4. Publish `"game:world:region:zone:001:entity:player"` → expect `[sub1, sub2, sub3]`
5. Publish `"game:world:north:zone:002:entity:player"` → expect `[sub2, sub3]`
6. Publish `"game:world:region:area:001:entity:npc"` → expect `[sub3]`

**Expected**: Deep hierarchies work correctly with wildcards at multiple levels

---

### **Scenario 8: High Volume Performance Test**
**Objective**: Validate O(k) performance with large datasets

**Test Configuration**:
- 1000 unique patterns with various wildcard combinations
- 10,000 published messages
- Measure: avg lookup time, memory usage, correctness

**Success Criteria**:
- Average `findSubscribers()` < 1ms
- No memory leaks detected
- 100% match accuracy for sampled messages
- Performance remains stable (no degradation over time)

**Benchmark against**: Naive regex O(n×m) implementation

---

### **Scenario 9: Thread-Safety - Concurrent Access**
**Objective**: Verify thread-safe operations under concurrent load

**Test Configuration**:
- Thread 1: Continuously register new patterns (100/sec)
- Thread 2: Continuously unregister patterns (50/sec)
- Threads 3-10: Publish messages and verify matches (1000/sec each)
- Duration: 10 seconds
- Total operations: ~85,000

**Success Criteria**:
- No crashes or deadlocks
- No data races (run with ThreadSanitizer)
- Results remain consistent and correct
- All mutex operations complete successfully

---

### **Scenario 10: Edge Cases & Stress Test**
**Objective**: Handle unusual inputs and extreme conditions

**Test Cases**:
1. **Empty/Invalid Topics**:
   - Empty string `""`
   - Single segment `"player"`
   - Multiple separators `"a:::b"` (empty segments)

2. **Minimal Patterns**:
   - Pattern `"*"` alone
   - Pattern `".*"` alone
   - Pattern `"*:*:*"`

3. **High Subscriber Density**:
   - 100 different subscribers on same pattern
   - 1 subscriber on 100 different patterns

4. **Lifecycle**:
   - Clear() followed by immediate reuse
   - Register → Unregister → Re-register same pattern

5. **Extreme Depth**:
   - Topics with 20+ segment levels
   - Patterns with mixed `*` and `.*` at deep levels

**Expected**: Graceful handling, no crashes, defined behavior for edge cases

---

## Test Implementation

### Framework
- **Unit Test Framework**: Catch2 (lightweight, header-only)
- **Build System**: CMake with CTest integration
- **Thread Testing**: C++11 `<thread>` + ThreadSanitizer
- **Performance**: `<chrono>` for timing, custom benchmark harness

### File Structure
```
topictree/
├── tests/
│   ├── CMakeLists.txt
│   ├── scenario_01_basic_exact.cpp
│   ├── scenario_02_single_wildcard.cpp
│   ├── scenario_03_multilevel_wildcard.cpp
│   ├── scenario_04_overlapping.cpp
│   ├── scenario_05_unregister_specific.cpp
│   ├── scenario_06_unregister_all.cpp
│   ├── scenario_07_deep_hierarchies.cpp
│   ├── scenario_08_performance.cpp
│   ├── scenario_09_threadsafety.cpp
│   └── scenario_10_edge_cases.cpp
├── CMakeLists.txt (updated)
└── TEST_PLAN.md (this file)
```

### Running Tests
```bash
cd topictree
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

---

## Success Metrics

### Functional Correctness
- ✅ All 10 scenarios pass
- ✅ 100% match accuracy across all test cases
- ✅ No memory leaks (Valgrind/ASan)

### Performance
- ✅ Average lookup < 1ms for typical workloads
- ✅ O(k) scaling verified (k = topic depth)
- ✅ 10x+ faster than regex baseline

### Robustness
- ✅ Thread-safe under concurrent load
- ✅ Handles edge cases gracefully
- ✅ No crashes with extreme inputs

---

## Implementation Status

- [ ] Test framework setup (Catch2)
- [ ] Scenario 1: Basic Exact Matching
- [ ] Scenario 2: Single Wildcard
- [ ] Scenario 3: Multi-Level Wildcard
- [ ] Scenario 4: Overlapping Patterns
- [ ] Scenario 5: Unregister Specific
- [ ] Scenario 6: Unregister All
- [ ] Scenario 7: Deep Hierarchies
- [ ] Scenario 8: Performance Test
- [ ] Scenario 9: Thread-Safety
- [ ] Scenario 10: Edge Cases
- [ ] CI/CD Integration
- [ ] Documentation Update

---

**Version**: 1.0
**Last Updated**: 2025-11-19
**Author**: StillHammer Team
