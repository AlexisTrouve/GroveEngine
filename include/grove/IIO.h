#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include "IDataNode.h"

namespace grove {

enum class IOType {
    INTRA = 0,      // Same process
    LOCAL = 1,      // Same machine
    NETWORK = 2     // TCP/WebSocket
};

struct SubscriptionConfig {
    bool replaceable = false;        // Replace vs accumulate for low-freq
    int batchInterval = 30000;       // ms for low-freq batching
    int maxBatchSize = 100;         // Max messages per batch
    bool compress = false;           // Compress batched data
};

struct Message {
    std::string topic;
    std::unique_ptr<IDataNode> data;
    uint64_t timestamp;

    // Default constructor
    Message() = default;

    // Move constructor and assignment (unique_ptr is move-only)
    Message(Message&&) = default;
    Message& operator=(Message&&) = default;

    // Delete copy (unique_ptr cannot be copied)
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
};

struct IOHealth {
    int queueSize;
    int maxQueueSize;
    bool dropping = false;           // Started dropping messages?
    float averageProcessingRate;     // Messages/second processed by module
    int droppedMessageCount = 0;     // Total dropped since last check
};

/**
 * @brief Pub/Sub communication interface with pull-based synchronous design
 *
 * Pull-based pub/sub system optimized for game modules. Modules have full control
 * over when they process messages, avoiding threading issues.
 *
 * Features:
 * - Topic patterns with wildcards (e.g., "player:*", "economy:*")
 * - Low-frequency subscriptions for bandwidth optimization
 * - Message consumption (pull removes message from queue)
 * - Engine health monitoring for backpressure management
 */
class IIO {
public:
    virtual ~IIO() = default;

    /**
     * @brief Publish message to a topic
     * @param topic Topic name (e.g., "player:123", "economy:prices")
     * @param message Message data
     */
    virtual void publish(const std::string& topic, std::unique_ptr<IDataNode> message) = 0;

    /**
     * @brief Subscribe to topic pattern (high-frequency)
     * @param topicPattern Topic pattern with wildcards (e.g., "player:*")
     * @param config Optional subscription configuration
     */
    virtual void subscribe(const std::string& topicPattern, const SubscriptionConfig& config = {}) = 0;

    /**
     * @brief Subscribe to topic pattern (low-frequency batched)
     * @param topicPattern Topic pattern with wildcards
     * @param config Subscription configuration (batchInterval, etc.)
     */
    virtual void subscribeLowFreq(const std::string& topicPattern, const SubscriptionConfig& config = {}) = 0;

    /**
     * @brief Get count of pending messages
     * @return Number of messages waiting to be pulled
     */
    virtual int hasMessages() const = 0;

    /**
     * @brief Pull and consume one message
     * @return Message from queue (oldest first). Message is removed from queue.
     * @throws std::runtime_error if no messages available
     */
    virtual Message pullMessage() = 0;

    /**
     * @brief Get IO health status for Engine monitoring
     * @return Health metrics including queue size, drop status, processing rate
     */
    virtual IOHealth getHealth() const = 0;

    /**
     * @brief Get IO type identifier
     * @return IO type enum value for identification
     */
    virtual IOType getType() const = 0;
};

} // namespace grove