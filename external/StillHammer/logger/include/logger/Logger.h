#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace stillhammer {

/**
 * @brief Wrapper around spdlog for simplified, domain-organized logging
 *
 * Features:
 * - Auto-organize logs by domain (logs/domain/component.log)
 * - Simplified API (1 line instead of 10+)
 * - Centralized configuration
 * - Thread-safe logger registry
 *
 * Example:
 *   auto log = stillhammer::createLogger("MyComponent");
 *   log->info("Hello world");
 *
 *   auto domainLog = stillhammer::createDomainLogger("NetworkIO", "network");
 *   domainLog->debug("Packet received");  // → logs/network/network_io.log
 */

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5,
    Off = 6
};

struct LoggerConfig {
    std::string domain = "";              // Empty = root logs/ directory
    LogLevel consoleLevel = LogLevel::Info;
    LogLevel fileLevel = LogLevel::Debug;
    bool enableConsole = true;
    bool enableFile = true;
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v";

    LoggerConfig& setDomain(const std::string& d) { domain = d; return *this; }
    LoggerConfig& setConsoleLevel(LogLevel level) { consoleLevel = level; return *this; }
    LoggerConfig& setFileLevel(LogLevel level) { fileLevel = level; return *this; }
    LoggerConfig& setPattern(const std::string& p) { pattern = p; return *this; }
    LoggerConfig& disableConsole() { enableConsole = false; return *this; }
    LoggerConfig& disableFile() { enableFile = false; return *this; }
};

/**
 * @brief Create a logger with automatic configuration
 *
 * @param name Logger name (used in log output)
 * @param config Optional configuration (uses defaults if not provided)
 * @return Shared pointer to spdlog logger
 */
std::shared_ptr<spdlog::logger> createLogger(
    const std::string& name,
    const LoggerConfig& config = LoggerConfig()
);

/**
 * @brief Create a domain-scoped logger
 *
 * Automatically organizes logs: logs/domain/component.log
 *
 * @param name Component name (e.g., "IntraIO")
 * @param domain Domain name (e.g., "network", "io", "engine")
 * @param config Optional additional configuration
 * @return Shared pointer to spdlog logger
 */
std::shared_ptr<spdlog::logger> createDomainLogger(
    const std::string& name,
    const std::string& domain,
    const LoggerConfig& config = LoggerConfig()
);

/**
 * @brief Get an existing logger by name
 *
 * @param name Logger name
 * @return Shared pointer to logger, or nullptr if not found
 */
std::shared_ptr<spdlog::logger> getLogger(const std::string& name);

/**
 * @brief Set global log level for all loggers
 */
void setGlobalLogLevel(LogLevel level);

/**
 * @brief Flush all loggers (useful before shutdown)
 */
void flushAll();

} // namespace stillhammer
