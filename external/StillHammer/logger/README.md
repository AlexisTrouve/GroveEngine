# StillHammer Logger

Elegant, domain-organized logging wrapper around spdlog.

## Features

- **Domain-based organization**: Automatically organize logs by domain (network, engine, io, etc.)
- **Simplified API**: 1 line instead of 10+ for common use cases
- **Zero-overhead**: Thin wrapper around spdlog (no performance cost)
- **Thread-safe**: Built on spdlog's thread-safe infrastructure
- **Flexible**: Easy to configure, supports console + file logging

## Quick Start

```cpp
#include <logger/Logger.h>

// Simple logger
auto log = stillhammer::createLogger("MyComponent");
log->info("Hello world");
log->debug("Debug info");
log->warn("Warning!");

// Domain-organized logger (logs/network/network_io.log)
auto netLog = stillhammer::createDomainLogger("NetworkIO", "network");
netLog->info("Packet received");

// Custom configuration
stillhammer::LoggerConfig config;
config.setDomain("custom")
      .setConsoleLevel(stillhammer::LogLevel::Warn)
      .setFileLevel(stillhammer::LogLevel::Debug);

auto customLog = stillhammer::createLogger("MyLogger", config);
```

## Log Organization

Logs are automatically organized by domain:

```
logs/
├── component.log          # Root-level loggers
├── network/
│   ├── tcp_server.log
│   └── udp_client.log
├── engine/
│   ├── renderer.log
│   └── physics.log
└── io/
    ├── intra_io.log
    └── io_manager.log
```

## API Reference

### Creating Loggers

```cpp
// Basic logger (logs to logs/component_name.log)
auto log = stillhammer::createLogger("ComponentName");

// Domain logger (logs to logs/domain/component_name.log)
auto log = stillhammer::createDomainLogger("ComponentName", "domain");

// Custom config
stillhammer::LoggerConfig config;
config.setDomain("network")
      .setConsoleLevel(stillhammer::LogLevel::Info)
      .setFileLevel(stillhammer::LogLevel::Debug)
      .setPattern("[%H:%M:%S] [%n] %v");

auto log = stillhammer::createLogger("NetworkIO", config);
```

### Logging

Uses standard spdlog API:

```cpp
log->trace("Very detailed info");
log->debug("Debug info");
log->info("Normal info");
log->warn("Warning");
log->error("Error occurred");
log->critical("Critical failure");
```

### Utility Functions

```cpp
// Get existing logger
auto log = stillhammer::getLogger("ComponentName");

// Set global log level
stillhammer::setGlobalLogLevel(stillhammer::LogLevel::Debug);

// Flush all loggers (useful before shutdown)
stillhammer::flushAll();
```

## Building

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTING=ON
make
ctest  # Run tests
```

## Requirements

- C++17
- spdlog (automatically fetched via CMake)

## License

MIT License - Part of StillHammer toolkit

## Integration

### CMake

```cmake
add_subdirectory(external/StillHammer/logger)
target_link_libraries(your_target PRIVATE stillhammer_logger)
```

### Usage

```cpp
#include <logger/Logger.h>

int main() {
    auto log = stillhammer::createLogger("MyApp");
    log->info("Application started");

    // Your code...

    stillhammer::flushAll();
    return 0;
}
```

## Comparison with raw spdlog

**Before (raw spdlog):**
```cpp
auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/io/intra_io.log", true);

console_sink->set_level(spdlog::level::info);
file_sink->set_level(spdlog::level::debug);

auto logger = std::make_shared<spdlog::logger>("IntraIO",
    spdlog::sinks_init_list{console_sink, file_sink});
logger->set_level(spdlog::level::debug);
logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%n] [%l] %v");
logger->flush_on(spdlog::level::warn);

spdlog::register_logger(logger);
```

**After (StillHammer Logger):**
```cpp
auto logger = stillhammer::createDomainLogger("IntraIO", "io");
```

Much cleaner! 🎉
