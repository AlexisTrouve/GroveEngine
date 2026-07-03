#pragma once

#include <memory>
#include <string>
#include <queue>
#include <deque>
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

// Why a message was dropped from an inbox (IO contract §9 observability). Pairs with BackpressurePolicy.
enum class DropReason : uint8_t {
    DropOldest = 0,  // evicted as the oldest droppable message when the inbox overflowed
    Coalesced,       // superseded by a newer same-topic message (latest-wins)
    Rejected,        // a Reject-policy message rejected at the door on an all-Reject overflow
};

// A diagnostic record of ONE dropped message: enough to answer "which message did my inbox lose, and why?"
// (the direct, false-positive-free counterpart of inferring loss from seq gaps). Populated only while the
// per-inbox drop log is enabled (IntraIO::enableDropLog).
struct DroppedRecord {
    std::string source;  // publisher instance id (from the dropped message's envelope)
    uint64_t    seq;     // per-source sequence of the dropped message
    std::string topic;   // the topic it was published to
    DropReason  reason;  // why it was dropped
};

// Interface for message delivery to avoid circular include
class IIntraIODelivery {
public:
    virtual ~IIntraIODelivery() = default;
    // message: the SHARED immutable payload (zero-copy) — the same node the publisher created,
    // shared by pointer across all subscribers, not deep-copied per delivery.
    // env: the transport-owned message envelope (source/seq/lamport/tick/simTime), stamped by the
    // router on publish and carried to the sink so it lands on the delivered Message (IO contract §5).
    virtual void deliverMessage(const std::string& topic, std::shared_ptr<const IDataNode> message, bool isLowFreq, const Envelope& env) = 0;
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

    // TRUE zero-copy gate. When true, publish() shares the publisher's ORIGINAL payload node by
    // pointer (0 json copies) instead of re-homing it into a core-built node. Only safe when the
    // publisher's CODE lives in the core for the whole process (a static/core module): its node
    // can never dangle past an .so unload. Default false = re-home (always safe) — see publish().
    // Set true by IntraIOManager::createInstance(..., coreResident=true), used by the engine for
    // static modules (registerStaticModule). Hot-loaded .so modules + ad-hoc instances stay false.
    bool coreResident_ = false;

    // Message storage. std::deque (not std::queue) so the per-topic backpressure policy (§9) can scan/erase
    // a middle element: Coalesce removes a pending same-topic message at enqueue, and the overflow eviction
    // skips Reject-policy messages to pick a droppable victim. Front = oldest, back = newest (FIFO via
    // push_back / pop_front) — identical order to the old std::queue.
    std::deque<Message> messageQueue;
    std::deque<Message> lowFreqMessageQueue;

    // Per-topic backpressure policy (§9), resolved at enqueue + eviction. Only NON-default topics are stored
    // (absent => DropOldest), so the default hot path is one empty-map lookup. Read/written under operationMutex.
    // Two tiers: EXACT topics in a map (O(1) fast path), and wildcard PATTERNS ("render:*") in a small ordered
    // list scanned only when no exact match hits (first match wins). Exact beats pattern.
    std::unordered_map<std::string, BackpressurePolicy> topicPolicies_;
    struct TopicPolicyRule {
        std::regex         pattern;     // compiled matcher
        std::string        rawPattern;  // the source string (for replace/remove)
        BackpressurePolicy policy;
    };
    std::vector<TopicPolicyRule> topicPolicyPatterns_;

    // Per-inbox DROP LOG (§9 observability): a bounded ring of the most recent DroppedRecords, so a debugging
    // host can see exactly which messages backpressure lost (and why). Opt-in: dropLogCap_ == 0 disables it,
    // so the drop path costs one size check when off. All access under operationMutex (drops happen there).
    std::deque<DroppedRecord> dropLog_;
    size_t                    dropLogCap_ = 0;  // 0 = disabled

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
    mutable std::atomic<size_t> totalCoalesced{0};  // §9 Coalesce: pending same-topic messages superseded (by design)
    mutable std::atomic<size_t> totalRejected{0};   // §9 Reject: critical messages rejected at the door on all-Reject overflow
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
    // Resolve a topic's backpressure policy (default DropOldest). Caller holds operationMutex.
    BackpressurePolicy policyFor(const std::string& topic) const;
    // Record a dropped message into the drop log (no-op if the log is disabled). Caller holds operationMutex.
    void logDrop(const Message& m, DropReason reason);
    void logPublish(const std::string& topic, const IDataNode& message) const;
    void logSubscription(const std::string& pattern, bool isLowFreq) const;
    void logPull(const Message& message) const;

public:
    // coreResident: true ⇒ this instance's publisher is core-resident for the whole process, so
    // publish() may share its payload node directly (true zero-copy). Default false ⇒ re-home
    // every payload into a core node (safe for hot-loaded .so publishers). See coreResident_.
    IntraIO(const std::string& instanceId, bool coreResident = false);
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

    // Per-topic backpressure policy (IO contract §9). Declare how THIS inbox handles a topic under pressure:
    // Coalesce (latest-wins) for flooding state topics, Reject to protect a critical command. `topic` may be an
    // EXACT topic ("render:camera") or a WILDCARD pattern ("render:*") — a '*' in the string makes it a pattern
    // rule (exact topics take precedence over patterns; among patterns, first-set wins). Setting back to
    // DropOldest removes the entry/rule. Opt-in: unset topics keep the default global drop-oldest behavior.
    void setTopicPolicy(const std::string& topic, BackpressurePolicy policy);
    BackpressurePolicy getTopicPolicy(const std::string& topic) const;
    size_t getCoalescedCount() const { return totalCoalesced.load(); }
    size_t getRejectedCount() const { return totalRejected.load(); }

    // Per-inbox drop log (IO contract §9). enableDropLog turns ON a bounded ring recording each dropped
    // message's {source, seq, topic, reason} — the direct answer to "what did backpressure lose here?".
    // Off by default (a count is always in getHealth().droppedMessageCount; this adds the WHICH + WHY).
    void enableDropLog(size_t capacity);
    void disableDropLog();
    std::vector<DroppedRecord> getRecentDrops() const;

    // Debug and monitoring
    json getDetailedMetrics() const;
    void setLogLevel(spdlog::level::level_enum level);
    size_t getSubscriptionCount() const;
    std::vector<std::string> getActiveTopics() const;

    // Testing utilities
    void simulateHighLoad(int messageCount, const std::string& topicPrefix = "test");
    void forceProcessLowFreqBatches();

    // Manager interface (called by IntraIOManager)
    void deliverMessage(const std::string& topic, std::shared_ptr<const IDataNode> message, bool isLowFreq, const Envelope& env) override;
    const std::string& getInstanceId() const override;
};

} // namespace grove