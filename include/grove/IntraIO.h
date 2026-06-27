#pragma once

#include <memory>
#include <string>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <regex>
#include <mutex>
#include <chrono>
#include <atomic>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "IIO.h"
#include "LamportClock.h"   // per-node logical clock for envelope causal stamping (§5)

using json = nlohmann::json;

namespace grove {

// Interface for message delivery to avoid circular include
class IIntraIODelivery {
public:
    virtual ~IIntraIODelivery() = default;
    // env: the transport-owned message envelope (source/seq/lamport/tick/simTime), stamped by the
    // router on publish and carried to the sink so it lands on the delivered Message (IO contract §5).
    virtual void deliverMessage(const std::string& topic, std::unique_ptr<IDataNode> message, bool isLowFreq, const Envelope& env) = 0;
    virtual const std::string& getInstanceId() const = 0;
};

/**
 * @brief Intra-process IO implementation with central routing
 *
 * IntraIO provides same-process pub/sub communication with zero network overhead.
 * Each module gets its own IntraIO instance, and messages are routed through
 * IntraIOManager for proper multi-module delivery.
 *
 * Features:
 * - Per-module isolation (one instance per module)
 * - Central routing via IntraIOManager
 * - Topic pattern matching with wildcards (e.g., "player:*", "economy:*")
 * - Low-frequency batching with configurable intervals
 * - Message replacement for reducible topics (latest-only semantics)
 * - Comprehensive health monitoring and metrics
 * - Thread-safe operations
 * - Pull-based message consumption
 *
 * Performance characteristics:
 * - Publish: ~10-50ns (direct memory copy + routing)
 * - Subscribe: ~100-500ns (pattern registration)
 * - Pull: ~50-200ns (queue operations)
 * - Zero network serialization overhead
 */
class IntraIO : public IIO, public IIntraIODelivery {
private:
    std::shared_ptr<spdlog::logger> logger;
    mutable std::mutex operationMutex; // Thread safety for all operations

    // Instance identification for routing
    std::string instanceId;

    // Envelope stamping (IO contract §5). seqCounter_ = monotonic per-source sequence;
    // lamportClock_ = this node's logical clock. BOTH are mutated ONLY under operationMutex
    // (seqCounter_ + lamportClock_.tick() in publish(); lamportClock_.update() in
    // deliverMessage()), so they are plain non-atomic members and stay race-free.
    uint64_t seqCounter_ = 0;
    LamportClock lamportClock_;

    // Message storage
    std::queue<Message> messageQueue;
    std::queue<Message> lowFreqMessageQueue;

    // Subscription management
    struct Subscription {
        std::regex pattern;
        std::string originalPattern;
        MessageHandler handler;  // Callback for this subscription
        SubscriptionConfig config;
        std::chrono::high_resolution_clock::time_point lastBatch;
        std::unordered_map<std::string, Message> batchedMessages; // For replaceable messages
        std::vector<Message> accumulatedMessages; // For non-replaceable messages

        // Default constructor
        Subscription() = default;

        // Move-only (Message contains unique_ptr, handler is copyable)
        Subscription(Subscription&&) = default;
        Subscription& operator=(Subscription&&) = default;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
    };

    std::vector<Subscription> highFreqSubscriptions;
    std::vector<Subscription> lowFreqSubscriptions;

    // Health monitoring
    mutable std::atomic<size_t> totalPublished{0};
    mutable std::atomic<size_t> totalPulled{0};
    mutable std::atomic<size_t> totalDropped{0};
    mutable std::chrono::high_resolution_clock::time_point lastHealthCheck;
    mutable float averageProcessingRate = 0.0f;

    // Configuration
    static constexpr size_t DEFAULT_MAX_QUEUE_SIZE = 10000;
    size_t maxQueueSize = DEFAULT_MAX_QUEUE_SIZE;

    // Helper methods
    void logIOStart();
    bool matchesPattern(const std::string& topic, const std::regex& pattern) const;
    std::regex compileTopicPattern(const std::string& pattern) const;
    void processLowFreqSubscriptions();
    void flushBatchedMessages(Subscription& sub);
    void updateHealthMetrics() const;
    void enforceQueueLimits();
    void logPublish(const std::string& topic, const IDataNode& message) const;
    void logSubscription(const std::string& pattern, bool isLowFreq) const;
    void logPull(const Message& message) const;

public:
    IntraIO(const std::string& instanceId);
    virtual ~IntraIO();

    // IIO implementation
    void publish(const std::string& topic, std::unique_ptr<IDataNode> message) override;
    void subscribe(const std::string& topicPattern, MessageHandler handler, const SubscriptionConfig& config = {}) override;
    void subscribeLowFreq(const std::string& topicPattern, MessageHandler handler, const SubscriptionConfig& config = {}) override;
    int hasMessages() const override;
    void pullAndDispatch() override;
    IOHealth getHealth() const override;
    IOType getType() const override;

    // Configuration and management
    void setMaxQueueSize(size_t maxSize);
    size_t getMaxQueueSize() const;
    void clearAllMessages();
    void clearAllSubscriptions();

    // Debug and monitoring
    json getDetailedMetrics() const;
    void setLogLevel(spdlog::level::level_enum level);
    size_t getSubscriptionCount() const;
    std::vector<std::string> getActiveTopics() const;

    // Testing utilities
    void simulateHighLoad(int messageCount, const std::string& topicPrefix = "test");
    void forceProcessLowFreqBatches();

    // Manager interface (called by IntraIOManager)
    void deliverMessage(const std::string& topic, std::unique_ptr<IDataNode> message, bool isLowFreq, const Envelope& env) override;
    const std::string& getInstanceId() const override;
};

} // namespace grove