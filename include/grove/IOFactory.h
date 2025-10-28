#pragma once

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "IIO.h"

using json = nlohmann::json;

namespace grove {

/**
 * @brief Factory for creating IO transport implementations
 *
 * IOFactory provides centralized creation of different communication transports:
 * - "intra" -> IntraIO (same-process direct function calls, zero network overhead)
 * - "local" -> LocalIO (same-machine via named pipes/sockets, production single-server)
 * - "network" -> NetworkIO (TCP/WebSocket for distributed deployment, MMO scale)
 *
 * Each IO type provides different performance and deployment characteristics while
 * maintaining the same pub/sub interface, enabling progressive scaling from
 * development to massive distributed systems.
 *
 * Usage:
 * ```cpp
 * auto io = IOFactory::create("intra");
 * auto io = IOFactory::create(IOType::NETWORK);
 * auto io = IOFactory::createFromConfig(config);
 * ```
 */
class IOFactory {
public:
    /**
     * @brief Create IO transport from string type name
     * @param transportType String representation of transport type
     * @param instanceId Unique identifier for this IO instance (required for IntraIO)
     * @return Unique pointer to IO implementation
     * @throws std::invalid_argument if transport type is unknown
     */
    static std::unique_ptr<IIO> create(const std::string& transportType, const std::string& instanceId = "");

    /**
     * @brief Create IO transport from enum type
     * @param ioType IOType enum value
     * @param instanceId Unique identifier for this IO instance (required for IntraIO)
     * @return Unique pointer to IO implementation
     * @throws std::invalid_argument if type is not implemented
     */
    static std::unique_ptr<IIO> create(IOType ioType, const std::string& instanceId = "");

    /**
     * @brief Create IO transport from JSON configuration
     * @param config JSON configuration object
     * @param instanceId Unique identifier for this IO instance (required for IntraIO)
     * @return Unique pointer to configured IO transport
     * @throws std::invalid_argument if config is invalid
     *
     * Expected config format:
     * ```json
     * {
     *   "type": "network",
     *   "instance_id": "module-name",
     *   "host": "localhost",
     *   "port": 8080,
     *   "protocol": "tcp",
     *   "buffer_size": 4096,
     *   "timeout": 5000,
     *   "compression": true
     * }
     * ```
     */
    static std::unique_ptr<IIO> createFromConfig(const json& config, const std::string& instanceId = "");

    /**
     * @brief Get list of available transport types
     * @return Vector of supported transport strings
     */
    static std::vector<std::string> getAvailableTransports();

    /**
     * @brief Check if transport type is supported
     * @param transportType Transport string to check
     * @return True if transport type is supported
     */
    static bool isTransportSupported(const std::string& transportType);

    /**
     * @brief Parse transport string to enum (case-insensitive)
     * @param transportStr String representation of transport
     * @return IOType enum value
     * @throws std::invalid_argument if string is invalid
     */
    static IOType parseTransport(const std::string& transportStr);

    /**
     * @brief Convert transport enum to string
     * @param ioType IOType enum value
     * @return String representation of transport
     */
    static std::string transportToString(IOType ioType);

    /**
     * @brief Get recommended transport for deployment scenario
     * @param expectedClients Expected number of concurrent clients (0 = single-user)
     * @param distributed Whether system will be distributed across machines
     * @param development Whether this is for development/debugging
     * @return Recommended IOType
     */
    static IOType getRecommendedTransport(int expectedClients = 1,
                                         bool distributed = false,
                                         bool development = true);

    /**
     * @brief Create IO transport with automatic endpoint discovery
     * @param transportType Transport type to create
     * @param endpoint Optional endpoint specification (auto-detected if empty)
     * @param instanceId Unique identifier for this IO instance (required for IntraIO)
     * @return Unique pointer to configured IO transport
     */
    static std::unique_ptr<IIO> createWithEndpoint(const std::string& transportType,
                                                   const std::string& endpoint = "",
                                                   const std::string& instanceId = "");

private:
    static std::shared_ptr<spdlog::logger> getFactoryLogger();
    static std::string toLowercase(const std::string& str);
    static std::string generateEndpoint(IOType ioType);
};

} // namespace grove