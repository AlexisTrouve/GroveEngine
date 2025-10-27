#pragma once

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "IModuleSystem.h"

using json = nlohmann::json;

namespace warfactory {

/**
 * @brief Factory for creating ModuleSystem implementations
 *
 * ModuleSystemFactory provides centralized creation of different execution strategies:
 * - "sequential" -> SequentialModuleSystem (debug/test, one-at-a-time execution)
 * - "threaded" -> ThreadedModuleSystem (each module in own thread)
 * - "thread_pool" -> ThreadPoolModuleSystem (tasks distributed across pool)
 * - "cluster" -> ClusterModuleSystem (distributed across machines)
 *
 * Each ModuleSystem type provides different performance characteristics while
 * maintaining the same interface, enabling progressive scaling.
 *
 * Usage:
 * ```cpp
 * auto moduleSystem = ModuleSystemFactory::create("sequential");
 * auto moduleSystem = ModuleSystemFactory::create(ModuleSystemType::THREAD_POOL);
 * auto moduleSystem = ModuleSystemFactory::createFromConfig(config);
 * ```
 */
class ModuleSystemFactory {
public:
    /**
     * @brief Create ModuleSystem from string strategy name
     * @param strategy String representation of execution strategy
     * @return Unique pointer to ModuleSystem implementation
     * @throws std::invalid_argument if strategy is unknown
     */
    static std::unique_ptr<IModuleSystem> create(const std::string& strategy);

    /**
     * @brief Create ModuleSystem from enum type
     * @param systemType ModuleSystemType enum value
     * @return Unique pointer to ModuleSystem implementation
     * @throws std::invalid_argument if type is not implemented
     */
    static std::unique_ptr<IModuleSystem> create(ModuleSystemType systemType);

    /**
     * @brief Create ModuleSystem from JSON configuration
     * @param config JSON configuration object
     * @return Unique pointer to configured ModuleSystem
     * @throws std::invalid_argument if config is invalid
     *
     * Expected config format:
     * ```json
     * {
     *   "strategy": "thread_pool",
     *   "thread_count": 4,
     *   "queue_size": 1000,
     *   "priority": "normal"
     * }
     * ```
     */
    static std::unique_ptr<IModuleSystem> createFromConfig(const json& config);

    /**
     * @brief Get list of available ModuleSystem strategies
     * @return Vector of supported strategy strings
     */
    static std::vector<std::string> getAvailableStrategies();

    /**
     * @brief Check if strategy is supported
     * @param strategy Strategy string to check
     * @return True if strategy is supported
     */
    static bool isStrategySupported(const std::string& strategy);

    /**
     * @brief Parse strategy string to enum (case-insensitive)
     * @param strategyStr String representation of strategy
     * @return ModuleSystemType enum value
     * @throws std::invalid_argument if string is invalid
     */
    static ModuleSystemType parseStrategy(const std::string& strategyStr);

    /**
     * @brief Convert strategy enum to string
     * @param systemType ModuleSystemType enum value
     * @return String representation of strategy
     */
    static std::string strategyToString(ModuleSystemType systemType);

    /**
     * @brief Get recommended strategy for given performance requirements
     * @param targetFPS Target frames per second (0 = no preference)
     * @param moduleCount Expected number of modules
     * @param cpuCores Available CPU cores (0 = auto-detect)
     * @return Recommended ModuleSystemType
     */
    static ModuleSystemType getRecommendedStrategy(int targetFPS = 60,
                                                  int moduleCount = 1,
                                                  int cpuCores = 0);

private:
    static std::shared_ptr<spdlog::logger> getFactoryLogger();
    static std::string toLowercase(const std::string& str);
    static int detectCpuCores();
};

} // namespace warfactory