# TopicTree

**Ultra-fast hierarchical topic matching for pub/sub systems**

A header-only C++17 library providing O(k) topic matching using hierarchical hash maps, replacing traditional O(n×m) regex-based pattern matching.

## Features

- **Blazing Fast**: O(k) lookup where k = topic depth (typically 2-4 segments)
- **Zero-copy parsing**: Uses `string_view` for efficient string operations
- **Wildcard support**:
  - Single-level: `player:*:position` matches `player:123:position`
  - Multi-level: `player:.*` matches all player topics
- **Header-only**: No compilation required, just include and use
- **Thread-safe**: Mutex-protected operations
- **Template-based**: Generic subscriber type support

## Performance

Replaces regex-based matching:
- **Before**: O(n patterns × m regex operations) - Test ALL patterns for EACH message
- **After**: O(k topic depth) - Walk hash tree by segments

For a typical system with 100 patterns and topics of depth 3:
- Regex: ~100 pattern tests per message
- TopicTree: ~3 hash lookups per message

## Usage

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
- **Single wildcard**: `*` - Matches one segment
  - `player:*:health` matches `player:001:health`, `player:002:health`
  - Does NOT match `player:001:stats:health` (wrong depth)
- **Multi-level wildcard**: `.*` - Matches remaining segments
  - `player:.*` matches `player:001`, `player:001:health`, `player:001:stats:armor`
  - Equivalent to "match everything after this point"

## Integration

### CMake (via add_subdirectory)

```cmake
add_subdirectory(external/StillHammer/topictree)
target_link_libraries(your_target PRIVATE topictree::topictree)
```

### Manual include

```cmake
target_include_directories(your_target PRIVATE
    external/StillHammer/topictree/include
)
```

## Requirements

- C++17 or later
- Standard library only (no external dependencies)

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

## License

MIT License - Part of the GroveEngine project

## Author

StillHammer - High-performance game engine components
