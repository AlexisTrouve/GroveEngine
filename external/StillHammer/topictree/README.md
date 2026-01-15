# TopicTree

**Ultra-fast hierarchical topic matching for pub/sub systems**

A standalone header-only C++17 library providing O(k) topic matching using hierarchical hash maps, replacing traditional O(n×m) regex-based pattern matching.

## Features

- **Blazing Fast**: O(k) lookup where k = topic depth (typically 2-4 segments)
- **Zero-copy parsing**: Uses `string_view` for efficient string operations
- **Wildcard support**:
  - Single-level: `player:*:position` matches `player:123:position`
  - Multi-level: `player:.*` matches all player topics
- **Header-only**: No compilation required, just include and use
- **Thread-safe**: Mutex-protected operations
- **Template-based**: Generic subscriber type support
- **Zero dependencies**: Standard library only

## Quick Start

```cpp
#include <topictree/TopicTree.h>

// Create tree with string subscriber IDs
topictree::TopicTree<std::string> tree;

// Register patterns
tree.registerSubscriber("player:*:position", "subscriber1");
tree.registerSubscriber("player:.*", "subscriber2");
tree.registerSubscriber("enemy:001:health", "subscriber3");

// Find matching subscribers
auto matches = tree.findSubscribers("player:123:position");
// Returns: ["subscriber1", "subscriber2"]

// Unregister
tree.unregisterSubscriber("player:*:position", "subscriber1");
```

## Pattern Syntax

- **Separator**: `:` (colon)
- **Single wildcard**: `*` - Matches exactly one segment
  - `player:*:health` matches `player:001:health`, `player:002:health`
  - Does NOT match `player:001:stats:health` (wrong depth)
- **Multi-level wildcard**: `.*` - Matches all remaining segments
  - `player:.*` matches `player:001`, `player:001:health`, `player:001:stats:armor`
  - Equivalent to "match everything after this point"

## Performance

Replaces regex-based matching:
- **Before**: O(n patterns × m regex operations) - Test ALL patterns for EACH message
- **After**: O(k topic depth) - Walk hash tree by segments

For a typical system with 100 patterns and topics of depth 3:
- Regex: ~100 pattern tests per message
- TopicTree: ~3 hash lookups per message

**Verified Performance** (from test suite):
- Average lookup: < 1ms with 1,000 patterns
- Deep topics (10+ levels): < 100μs
- Scalability: < 5ms with 10,000 patterns
- Thread-safe under 85,000+ concurrent operations

## Integration

### CMake (via add_subdirectory)

```cmake
add_subdirectory(path/to/topictree)
target_link_libraries(your_target PRIVATE topictree::topictree)
```

### Manual include

```cmake
target_include_directories(your_target PRIVATE
    path/to/topictree/include
)
```

### Direct include

Just copy the `include/topictree/` directory to your project and:

```cpp
#include <topictree/TopicTree.h>
```

## API Reference

### Constructor

```cpp
topictree::TopicTree<SubscriberType> tree;
```

### Methods

```cpp
// Register a subscriber for a pattern
void registerSubscriber(const std::string& pattern, const SubscriberType& subscriber);

// Find all subscribers matching a topic
std::vector<SubscriberType> findSubscribers(const std::string& topic) const;

// Unregister from specific pattern
void unregisterSubscriber(const std::string& pattern, const SubscriberType& subscriber);

// Unregister from ALL patterns
void unregisterSubscriberAll(const SubscriberType& subscriber);

// Clear all subscriptions
void clear();

// Get total subscriber count
size_t subscriberCount() const;
```

## Architecture

```
Topic: "player:123:position"
Split: ["player", "123", "position"]

Tree structure:
{
    "player": {
        "123": {
            "position": [subscribers],
            "*": [wildcard_subscribers]
        },
        "*": { "position": [wildcard_subscribers] },
        ".*": [multi_level_subscribers]
    }
}
```

Lookup walks the tree level by level, collecting subscribers from:
1. Exact matches at each level
2. Single wildcards (`*`) at each level
3. Multi-level wildcards (`.*`) that match everything below

## Testing

Comprehensive test suite with 100% pass rate:

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

**Test Coverage:**
- 10 scenarios, 63 test sections
- Functional correctness (exact, wildcards, overlapping)
- Performance benchmarks (up to 10,000 patterns)
- Thread-safety (concurrent reads/writes)
- Edge cases and stress testing

See [TEST_PLAN.md](TEST_PLAN.md) and [TEST_RESULTS.md](TEST_RESULTS.md) for details.

## Requirements

- C++17 or later
- Standard library only (no external dependencies)
- Optional: Catch2 v3.5+ for running tests (auto-fetched)

## Use Cases

- **Pub/Sub systems**: Efficient topic routing
- **Game engines**: Entity event matching
- **Message brokers**: Pattern-based message delivery
- **IoT platforms**: Device event filtering
- **Monitoring systems**: Metric subscription management

## License

MIT License - See [LICENSE](LICENSE) file

## Version

1.0.0

## Author

StillHammer
