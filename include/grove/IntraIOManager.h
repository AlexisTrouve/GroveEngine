#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <regex>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "IIO.h"

using json = nlohmann::json;

namespace warfactory {

class IntraIO; // Forward declaration
class IIntraIODelivery; // Forward declaration

// Factory function for creating IntraIO (defined in IntraIO.cpp to avoid circular include)
std::shared_ptr<IntraIO> createIntraIOInstance(const std::string& instanceId);

/**
 * @brief Central router for IntraIO instances
 *
 * IntraIOManager coordinates message passing between multiple IntraIO instances.
 * Each module gets its own IntraIO instance, and the manager handles routing
 * messages between them based on subscriptions.
 *
 * Architecture:
 * - One IntraIO instance per module (isolation)
 * - Central routing of messages between instances
 * - Pattern-based subscription matching
 * - Thread-safe operations
 *
 * Performance:
 * - Direct memory routing (no serialization)
 * - Pattern caching for fast lookup
 * - Batched delivery for efficiency
 */
class IntraIOManager {
private:
    std::shared_ptr<spdlog::logger> logger;
    mutable std::mutex managerMutex;

    // Registry of IntraIO instances
    std::unordered_map<std::string, std::shared_ptr<IIntraIODelivery>> instances;

    // Subscription routing table
    struct RouteEntry {
        std::string instanceId;
        std::regex pattern;
        std::string originalPattern;
        bool isLowFreq;
    };
    std::vector<RouteEntry> routingTable;

    // Statistics
    mutable std::atomic<size_t> totalRoutedMessages{0};
    mutable std::atomic<size_t> totalRoutes{0};

public:
    IntraIOManager();
    ~IntraIOManager();

    // Instance management
    std::shared_ptr<IntraIO> createInstance(const std::string& instanceId);
    void registerInstance(const std::string& instanceId, std::shared_ptr<IIntraIODelivery> instance);
    void removeInstance(const std::string& instanceId);
    std::shared_ptr<IntraIO> getInstance(const std::string& instanceId) const;

    // Routing (called by IntraIO instances)
    void routeMessage(const std::string& sourceid, const std::string& topic, const json& message);
    void registerSubscription(const std::string& instanceId, const std::string& pattern, bool isLowFreq);
    void unregisterSubscription(const std::string& instanceId, const std::string& pattern);

    // Management
    void clearAllRoutes();
    size_t getInstanceCount() const;
    std::vector<std::string> getInstanceIds() const;

    // Debug and monitoring
    json getRoutingStats() const;
    void setLogLevel(spdlog::level::level_enum level);

    // Singleton access (for global routing)
    static IntraIOManager& getInstance();
};

} // namespace warfactory