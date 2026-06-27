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

/**
 * @brief Transport-owned metadata header stamped on every control-plane message — the "envelope".
 *
 * The envelope (IO contract §5, docs/design/iio-contract.md) carries ordering/causality metadata
 * OWNED BY THE TRANSPORT, separate from the module's payload (Message::data). It lets the transport
 * do dedup / gap-detection / causal ordering generically, and lets a canonical order be
 * reconstructed offline for replay/debug — WITHOUT the module ever touching it. A module reads it
 * only if it wants to (e.g. a relocated remote module reads `simTime`/`tick` instead of holding the
 * EngineClock pointer). Stamped by the transport on publish; never authored by the payload.
 */
struct Envelope {
    std::string source;            // publisher instance id (IntraIO::instanceId) — routing + per-source order
    uint64_t    seq      = 0;      // monotonic per-source counter — gap detection / dedup
    uint64_t    lamport  = 0;      // logical clock send-stamp — causal total order, tie-broken by source
    uint64_t    tick     = 0;      // engine tick at publish (EngineClock) — coarse replay axis
    double      simTime  = 0.0;    // engine simTime at publish — so a remote module needs no clock sync
    std::string causedBy;          // optional correlation id (response->request). Reserved; not yet populated.
};

struct Message {
    std::string topic;
    // SHARED IMMUTABLE payload: one published node is wrapped once and shared by pointer across all
    // N subscribers (and across the route/deliver lock boundary) instead of being json-deep-copied
    // per delivery — the intra zero-copy delivery. `const` makes it safe to share concurrently (no
    // subscriber can mutate a payload another is reading) and compile-prevents the destructive
    // getChild on a shared node. Readers use const methods / const getJsonData().
    std::shared_ptr<const IDataNode> data;
    uint64_t timestamp;
    Envelope env;                  // transport-owned header (source/seq/lamport/tick/simTime) — see Envelope

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
 * @brief Message handler callback type
 *
 * Callback invoked when a message matching the subscribed pattern is pulled.
 * Module implements this to handle specific message types without if-forest dispatch.
 */
using MessageHandler = std::function<void(const Message&)>;

/**
 * @brief Pub/Sub communication interface with pull-based callback dispatch
 *
 * Pull-based pub/sub system with automatic message dispatch to registered handlers.
 * Modules subscribe with callbacks, then pull messages - dispatch is automatic.
 *
 * Design:
 * - Modules retain control over WHEN to process (pull-based)
 * - No if-forest dispatch (callbacks registered at subscription)
 * - Thread-safe for multi-threaded module systems
 *
 * Features:
 * - Topic patterns with wildcards (e.g., "player:*", "economy:*")
 * - Low-frequency subscriptions for bandwidth optimization
 * - Automatic callback dispatch on pull
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
     * @brief Subscribe to topic pattern with callback handler (high-frequency)
     * @param topicPattern Topic pattern with wildcards (e.g., "player:*")
     * @param handler Callback invoked when matching message is pulled
     * @param config Optional subscription configuration
     *
     * Example:
     *   io->subscribe("input:mouse", [this](const Message& msg) {
     *       handleMouseInput(msg);
     *   });
     */
    virtual void subscribe(
        const std::string& topicPattern,
        MessageHandler handler,
        const SubscriptionConfig& config = {}
    ) = 0;

    /**
     * @brief Subscribe to topic pattern with callback (low-frequency batched)
     * @param topicPattern Topic pattern with wildcards
     * @param handler Callback invoked when matching message is pulled
     * @param config Subscription configuration (batchInterval, etc.)
     *
     * Example:
     *   io->subscribeLowFreq("analytics:*", [this](const Message& msg) {
     *       processBatchedAnalytics(msg);
     *   }, {.batchInterval = 5000});
     */
    virtual void subscribeLowFreq(
        const std::string& topicPattern,
        MessageHandler handler,
        const SubscriptionConfig& config = {}
    ) = 0;

    /**
     * @brief Get count of pending messages
     * @return Number of messages waiting to be pulled
     */
    virtual int hasMessages() const = 0;

    /**
     * @brief Pull and auto-dispatch one message to registered handler
     * @throws std::runtime_error if no messages available
     *
     * Pulls oldest message from queue and invokes the callback registered
     * during subscribe(). Message is consumed (removed from queue).
     *
     * Example usage:
     *   while (io->hasMessages() > 0) {
     *       io->pullAndDispatch();  // Callbacks invoked automatically
     *   }
     */
    virtual void pullAndDispatch() = 0;

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