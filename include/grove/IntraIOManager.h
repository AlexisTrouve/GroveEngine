#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>  // For shared_mutex (C++17)
#include <thread>
#include <chrono>
#include <atomic>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "IIO.h"
#include <topictree/TopicTree.h>

using json = nlohmann::json;

namespace grove {

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
    mutable std::shared_mutex managerMutex;  // Reader-writer lock for instances

    // Registry of IntraIO instances
    std::unordered_map<std::string, std::shared_ptr<IIntraIODelivery>> instances;

    // Subscription info for each instance
    struct SubscriptionInfo {
        std::string instanceId;
        bool isLowFreq;
        int batchInterval; // milliseconds
    };

    // Ultra-fast topic routing using TopicTree
    topictree::TopicTree<std::string> topicTree;  // Maps patterns to instanceIds

    // Track subscription info per instance (for management)
    std::unordered_map<std::string, std::vector<std::string>> instancePatterns;  // instanceId -> patterns
    std::unordered_map<std::string, SubscriptionInfo> subscriptionInfoMap;  // pattern -> subscription info

    // Batching for low-frequency subscriptions
    struct BatchBuffer {
        std::string instanceId;
        std::string pattern;
        int batchInterval;
        std::chrono::steady_clock::time_point lastFlush;
        std::vector<std::pair<std::string, json>> messages; // topic + data pairs
    };
    std::unordered_map<std::string, BatchBuffer> batchBuffers; // pattern -> buffer
    mutable std::mutex batchMutex;
    std::thread batchThread;
    std::atomic<bool> batchThreadRunning{false};

    void batchFlushLoop();
    void flushBatchBuffer(BatchBuffer& buffer);
    void flushBatchBufferSafe(BatchBuffer& buffer);  // Safe version - no nested locks

    // Statistics
    mutable std::atomic<size_t> totalRoutedMessages{0};
    mutable std::atomic<size_t> totalRoutes{0};

    // Batched logging (pour éviter spam)
    static constexpr size_t LOG_BATCH_SIZE = 100;
    mutable std::atomic<size_t> messagesSinceLastLog{0};

public:
    IntraIOManager();
    ~IntraIOManager();

    // Instance management
    std::shared_ptr<IntraIO> createInstance(const std::string& instanceId);
    void registerInstance(const std::string& instanceId, std::shared_ptr<IIntraIODelivery> instance);
    void removeInstance(const std::string& instanceId);
    std::shared_ptr<IntraIO> getInstance(const std::string& instanceId) const;

    // Routing (called by IntraIO instances)
    void routeMessage(const std::string& sourceid, const std::string& topic, const json& messageData);
    void registerSubscription(const std::string& instanceId, const std::string& pattern, bool isLowFreq, int batchInterval = 1000);
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

} // namespace grove