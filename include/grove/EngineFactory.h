#pragma once

#include <memory>
#include <string>
#include <stdexcept>
#include <spdlog/spdlog.h>

#include "IEngine.h"
#include "DebugEngine.h"

namespace grove {

/**
 * @brief Factory for creating engine implementations
 *
 * EngineFactory provides a centralized way to create different engine types
 * based on configuration or runtime requirements.
 *
 * Supported engine types:
 * - "debug" or "DEBUG" -> DebugEngine (maximum logging, step debugging)
 * - "production" or "PRODUCTION" -> ProductionEngine (future implementation)
 * - "high_performance" or "HIGH_PERFORMANCE" -> HighPerformanceEngine (future)
 *
 * Usage:
 * ```cpp
 * auto engine = EngineFactory::createEngine("debug");
 * auto engine = EngineFactory::createEngine(EngineType::DEBUG);
 * auto engine = EngineFactory::createFromConfig("config/engine.json");
 * ```
 */
class EngineFactory {
public:
    /**
     * @brief Create engine from string type
     * @param engineType String representation of engine type
     * @return Unique pointer to engine implementation
     * @throws std::invalid_argument if engine type is unknown
     */
    static std::unique_ptr<IEngine> createEngine(const std::string& engineType);

    /**
     * @brief Create engine from enum type
     * @param engineType Engine type enum value
     * @return Unique pointer to engine implementation
     * @throws std::invalid_argument if engine type is not implemented
     */
    static std::unique_ptr<IEngine> createEngine(EngineType engineType);

    /**
     * @brief Create engine from configuration file
     * @param configPath Path to JSON configuration file
     * @return Unique pointer to engine implementation
     * @throws std::runtime_error if config file cannot be read
     * @throws std::invalid_argument if engine type in config is invalid
     *
     * Expected config format:
     * ```json
     * {
     *   "engine": {
     *     "type": "debug",
     *     "log_level": "trace",
     *     "features": {
     *       "step_debugging": true,
     *       "performance_monitoring": true
     *     }
     *   }
     * }
     * ```
     */
    static std::unique_ptr<IEngine> createFromConfig(const std::string& configPath);

    /**
     * @brief Get list of available engine types
     * @return Vector of supported engine type strings
     */
    static std::vector<std::string> getAvailableEngineTypes();

    /**
     * @brief Check if engine type is supported
     * @param engineType Engine type string to check
     * @return True if engine type is supported
     */
    static bool isEngineTypeSupported(const std::string& engineType);

    /**
     * @brief Get engine type from string (case-insensitive)
     * @param engineTypeStr String representation of engine type
     * @return EngineType enum value
     * @throws std::invalid_argument if string is not a valid engine type
     */
    static EngineType parseEngineType(const std::string& engineTypeStr);

    /**
     * @brief Convert engine type enum to string
     * @param engineType Engine type enum value
     * @return String representation of engine type
     */
    static std::string engineTypeToString(EngineType engineType);

private:
    static std::shared_ptr<spdlog::logger> getFactoryLogger();
    static std::string toLowercase(const std::string& str);
};

} // namespace grove