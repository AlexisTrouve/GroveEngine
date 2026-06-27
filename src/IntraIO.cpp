#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <grove/JsonDataNode.h>
#include <stdexcept>
#include <iostream>
#include <chrono>

namespace grove {

// Factory function for IntraIOManager to avoid circular include
std::shared_ptr<IntraIO> createIntraIOInstance(const std::string& instanceId) {
    return std::make_shared<IntraIO>(instanceId);
}

IntraIO::IntraIO(const std::string& id) : instanceId(id) {
    std::cout << "[IntraIO] Created instance: " << instanceId << std::endl;
    lastHealthCheck = std::chrono::high_resolution_clock::now();
}

IntraIO::~IntraIO() {
    // NOTE: deliberately NO std::cout / iostream here.
    // ~IntraIO() can run during static destruction at exit() (the singleton
    // IntraIOManager destroys its IntraIO instances from _execute_onexit_table).
    // The std::cout global lives in another translation unit; cross-TU static
    // destruction order is unspecified, so std::cout may already be destroyed
    // here -> operator<< on a dead stream = SIGSEGV. Use no I/O in this path.
    // Unregister from manager to prevent dangling pointer access
    // Guard: if the IntraIOManager singleton has already been destroyed
    // (s_destroyed flag set in its destructor), skip removeInstance().
    // Without this check, calling getInstance() after singleton destruction
    // returns a dangling reference, causing a segfault / stack-buffer-overrun
    // on Windows during static teardown when destruction order is unspecified.
    if (!IntraIOManager::isDestroyed()) {
        try {
            IntraIOManager::getInstance().removeInstance(instanceId);
        } catch (...) {
            // Ignore errors during cleanup - manager might already be destroyed
        }
    }
    // (see note at top of destructor: no std::cout during static teardown)
}

void IntraIO::publish(const std::string& topic, std::unique_ptr<IDataNode> message) {
    // DEADLOCK PREVENTION — do NOT hold operationMutex while calling routeMessage().
    //
    // WHY this is a deadlock without this fix (ABBA pattern):
    //   Thread A (caller of publish):
    //     HOLDS  operationMutex  (old: locked at top of this function)
    //     WAITS  managerMutex    (routeMessage() calls scoped_lock(managerMutex, batchMutex))
    //
    //   Thread B (batchFlushLoop internal thread):
    //     HOLDS  managerMutex    (flushBatchBufferSafe() locks managerMutex)
    //     WAITS  operationMutex  (deliverMessage() calls lock_guard(operationMutex))
    //
    //   → circular wait → deadlock.
    //
    // FIX: Extract and COPY all needed data under the lock, then release the lock
    // BEFORE calling into IntraIOManager. routeMessage() receives a value copy
    // of the JSON — it does not touch our internal state.

    nlohmann::json jsonDataCopy;
    uint64_t seq = 0;
    uint64_t lamport = 0;
    {
        std::lock_guard<std::mutex> lock(operationMutex);

        totalPublished++;

        // Envelope SEND-stamp (IO contract §5), under operationMutex so the counters stay
        // serialized with everything else this instance touches: a monotonic per-source
        // sequence number + a Lamport "send" tick. These are the SENDER's values, so they are
        // computed HERE (not at the sink). The router adds tick/simTime; the sink lands the
        // full envelope on the delivered Message and folds this lamport into its own clock.
        seq = ++seqCounter_;
        lamport = lamportClock_.tick();

        // Validate and copy the JSON payload while we hold the lock.
        // We copy (not reference) so that releasing operationMutex cannot
        // invalidate the data we pass to routeMessage().
        auto* jsonNode = dynamic_cast<JsonDataNode*>(message.get());
        if (!jsonNode) {
            throw std::runtime_error("IntraIO::publish() requires JsonDataNode for message data");
        }
        jsonDataCopy = jsonNode->getJsonData();  // deep copy
    }
    // operationMutex is now released — safe to call routeMessage()

    // Route the copied message via central manager.
    // NOTE: routeMessage() acquires managerMutex internally.
    //       We must NOT hold operationMutex here (see ABBA comment above).
    IntraIOManager::getInstance().routeMessage(instanceId, topic, jsonDataCopy, seq, lamport);
}

void IntraIO::subscribe(const std::string& topicPattern, MessageHandler handler, const SubscriptionConfig& config) {
    // DEADLOCK PREVENTION — same ABBA risk as publish():
    // subscribe() → registerSubscription() needs managerMutex.
    // Build the subscription object under the lock, release, then register with manager.
    {
        std::lock_guard<std::mutex> lock(operationMutex);

        Subscription sub;
        sub.originalPattern = topicPattern;
        sub.pattern = compileTopicPattern(topicPattern);
        sub.handler = handler;  // Store callback
        sub.config = config;
        sub.lastBatch = std::chrono::high_resolution_clock::now();

        highFreqSubscriptions.push_back(std::move(sub));
    }
    // operationMutex released — safe to call into IntraIOManager now

    // Register subscription with central manager for routing.
    // NOTE: registerSubscription() acquires managerMutex; operationMutex NOT held.
    IntraIOManager::getInstance().registerSubscription(instanceId, topicPattern, false);
}

void IntraIO::subscribeLowFreq(const std::string& topicPattern, MessageHandler handler, const SubscriptionConfig& config) {
    // DEADLOCK PREVENTION — same ABBA risk as publish()/subscribe().
    {
        std::lock_guard<std::mutex> lock(operationMutex);

        Subscription sub;
        sub.originalPattern = topicPattern;
        sub.pattern = compileTopicPattern(topicPattern);
        sub.handler = handler;  // Store callback
        sub.config = config;
        sub.lastBatch = std::chrono::high_resolution_clock::now();

        lowFreqSubscriptions.push_back(std::move(sub));
    }
    // operationMutex released — safe to call into IntraIOManager now

    // Register subscription with central manager for routing.
    // NOTE: registerSubscription() acquires managerMutex; operationMutex NOT held.
    IntraIOManager::getInstance().registerSubscription(instanceId, topicPattern, true, config.batchInterval);
}

int IntraIO::hasMessages() const {
    std::lock_guard<std::mutex> lock(operationMutex);
    return static_cast<int>(messageQueue.size() + lowFreqMessageQueue.size());
}

void IntraIO::pullAndDispatch() {
    // DEADLOCK PREVENTION — do NOT hold operationMutex while invoking user callbacks.
    //
    // WHY this is a deadlock without this fix (ABBA pattern):
    //   Thread A (caller of pullAndDispatch):
    //     HOLDS  operationMutex     (old: locked for the entire function)
    //     The user callback calls publish() → routeMessage() → WAITS managerMutex
    //
    //   Thread B (batchFlushLoop):
    //     HOLDS  managerMutex       (flushBatchBufferSafe acquires managerMutex)
    //     WAITS  operationMutex     (deliverMessage() acquires operationMutex)
    //
    //   → circular wait → deadlock.
    //
    // FIX: Two-phase approach:
    //   Phase 1: Under the lock, dequeue ONE message and snapshot all matching
    //            (handler, message) pairs into a local vector.
    //   Phase 2: Release the lock, then invoke every captured handler.
    //
    // This preserves the original semantics exactly:
    //   - One message dequeued per call (high-freq preferred over low-freq)
    //   - All matching subscriptions receive the message
    //   - Same dispatch order
    //   - Exception if queue is empty

    // Struct to capture a handler reference + the message to deliver.
    // We store the handler by value (std::function copy) to keep it alive
    // after we release the lock.
    struct DispatchEntry {
        MessageHandler handler;
        // The message is shared across all matching handlers for this call.
        // We capture a raw const-pointer to the message and keep it alive via
        // a shared_ptr so that the message outlives the lock scope.
        const Message* msgPtr;
    };

    std::vector<DispatchEntry> toDispatch;
    // Keep the dequeued message alive past the lock scope via shared ownership.
    std::shared_ptr<Message> msgHolder;

    {
        std::lock_guard<std::mutex> lock(operationMutex);

        if (messageQueue.empty() && lowFreqMessageQueue.empty()) {
            throw std::runtime_error("No messages available");
        }

        // Dequeue exactly one message — high-freq queue takes priority.
        bool isLowFreq = false;
        msgHolder = std::make_shared<Message>();
        if (!messageQueue.empty()) {
            *msgHolder = std::move(messageQueue.front());
            messageQueue.pop();
        } else {
            *msgHolder = std::move(lowFreqMessageQueue.front());
            lowFreqMessageQueue.pop();
            isLowFreq = true;
        }

        totalPulled++;

        // Snapshot all matching (handler, message-ptr) pairs into toDispatch.
        // We do NOT invoke handlers here — only collect them.
        // This is safe because handlers are std::function objects (copyable) and
        // the message stays alive via msgHolder (shared_ptr).
        const auto& subscriptions = isLowFreq ? lowFreqSubscriptions : highFreqSubscriptions;
        for (const auto& sub : subscriptions) {
            if (matchesPattern(msgHolder->topic, sub.pattern) && sub.handler) {
                toDispatch.push_back({ sub.handler, msgHolder.get() });
            }
        }
    }
    // operationMutex is now released — safe to invoke user callbacks

    // Phase 2: Invoke all captured handlers WITHOUT holding operationMutex.
    // Each handler receives a const-ref to the same message (original semantics).
    // If a handler calls publish() → routeMessage(), it will acquire managerMutex
    // freely, with no risk of deadlock against batchFlushLoop.
    for (const auto& entry : toDispatch) {
        entry.handler(*entry.msgPtr);
    }
}

IOHealth IntraIO::getHealth() const {
    std::lock_guard<std::mutex> lock(operationMutex);

    IOHealth health;
    health.queueSize = static_cast<int>(messageQueue.size() + lowFreqMessageQueue.size());
    health.maxQueueSize = static_cast<int>(maxQueueSize);
    health.dropping = (health.queueSize >= health.maxQueueSize);
    health.droppedMessageCount = static_cast<int>(totalDropped.load());
    health.averageProcessingRate = averageProcessingRate;

    return health;
}

IOType IntraIO::getType() const {
    return IOType::INTRA;
}

void IntraIO::setMaxQueueSize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(operationMutex);
    maxQueueSize = maxSize;
}

size_t IntraIO::getMaxQueueSize() const {
    return maxQueueSize;
}

void IntraIO::clearAllMessages() {
    std::lock_guard<std::mutex> lock(operationMutex);
    while (!messageQueue.empty()) messageQueue.pop();
    while (!lowFreqMessageQueue.empty()) lowFreqMessageQueue.pop();
}

void IntraIO::clearAllSubscriptions() {
    std::lock_guard<std::mutex> lock(operationMutex);
    highFreqSubscriptions.clear();
    lowFreqSubscriptions.clear();
}

nlohmann::json IntraIO::getDetailedMetrics() const {
    std::lock_guard<std::mutex> lock(operationMutex);

    nlohmann::json metrics;
    metrics["instance_id"] = instanceId;
    metrics["total_published"] = totalPublished.load();
    metrics["total_pulled"] = totalPulled.load();
    metrics["total_dropped"] = totalDropped.load();
    metrics["queue_size"] = messageQueue.size() + lowFreqMessageQueue.size();
    metrics["max_queue_size"] = maxQueueSize;
    metrics["high_freq_subscriptions"] = highFreqSubscriptions.size();
    metrics["low_freq_subscriptions"] = lowFreqSubscriptions.size();

    return metrics;
}

void IntraIO::setLogLevel(spdlog::level::level_enum level) {
    if (logger) {
        logger->set_level(level);
    }
}

size_t IntraIO::getSubscriptionCount() const {
    std::lock_guard<std::mutex> lock(operationMutex);
    return highFreqSubscriptions.size() + lowFreqSubscriptions.size();
}

std::vector<std::string> IntraIO::getActiveTopics() const {
    std::lock_guard<std::mutex> lock(operationMutex);

    std::vector<std::string> topics;
    for (const auto& sub : highFreqSubscriptions) {
        topics.push_back(sub.originalPattern);
    }
    for (const auto& sub : lowFreqSubscriptions) {
        topics.push_back(sub.originalPattern + " (low-freq)");
    }

    return topics;
}

void IntraIO::simulateHighLoad(int messageCount, const std::string& topicPrefix) {
    for (int i = 0; i < messageCount; ++i) {
        nlohmann::json data = {{"id", i}, {"value", i * 10}};
        auto node = std::make_unique<JsonDataNode>("test", data);
        publish(topicPrefix + ":" + std::to_string(i), std::move(node));
    }
}

void IntraIO::forceProcessLowFreqBatches() {
    processLowFreqSubscriptions();
}

void IntraIO::deliverMessage(const std::string& topic, std::unique_ptr<IDataNode> message, bool isLowFreq, const Envelope& env) {
    // deliverMessage() is called by IntraIOManager::flushBatchBufferSafe() while
    // holding managerMutex. It ONLY enqueues the message — it does NOT invoke any
    // user callback. There is therefore no outbound call that could try to re-acquire
    // managerMutex, so holding operationMutex here is safe.
    //
    // Lock order (from IntraIOManager's perspective):
    //   managerMutex (held by caller) → operationMutex (acquired here)
    // This is consistent — the reverse (operationMutex → managerMutex) is broken
    // by the fixes in publish() / subscribe() / pullAndDispatch().
    std::lock_guard<std::mutex> lock(operationMutex);

    // Envelope RECEIVE-rule (IO contract §5): fold the sender's lamport into our own clock
    // (max+1) so anything THIS instance publishes next is stamped causally after the message it
    // just received. Done under operationMutex — same lock as publish()'s tick — so this node's
    // Lamport clock is always serialized. The message keeps the sender's send-stamp (env.lamport).
    lamportClock_.update(env.lamport);

    Message msg;
    msg.topic = topic;
    msg.data = std::move(message);
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    msg.env = env;   // transport-owned header lands on the delivered Message

    if (isLowFreq) {
        lowFreqMessageQueue.push(std::move(msg));
    } else {
        messageQueue.push(std::move(msg));
    }

    // BACKPRESSURE: cap the queue here, under operationMutex. Without this call the
    // queues grew without bound whenever a consumer stalled (the advertised
    // maxQueueSize / dropping / droppedMessageCount were dead). This is the single
    // line whose absence made enforceQueueLimits() dead code.
    enforceQueueLimits();
}

const std::string& IntraIO::getInstanceId() const {
    return instanceId;
}

// Helper methods
void IntraIO::logIOStart() {
    if (logger) {
        logger->info("IntraIO[{}] started", instanceId);
    }
}

bool IntraIO::matchesPattern(const std::string& topic, const std::regex& pattern) const {
    return std::regex_match(topic, pattern);
}

std::regex IntraIO::compileTopicPattern(const std::string& pattern) const {
    // Patterns can be:
    // 1. Simple wildcard: "*" → convert to ".*" regex
    // 2. Regex patterns: "player:.*", "test:.*" → use as-is

    // If the pattern contains ".*", align with how the manager's TopicTree matches it:
    // ".*" is a TERMINAL multi-segment wildcard (it matches the REST of the topic and
    // TopicTree drops anything written after it). The old code used the whole pattern
    // as a raw regex, so a pattern like "a:.*:z" honored the ":z" suffix and
    // regex_match() FAILED on "a:1:b" — even though TopicTree had ROUTED that topic to
    // us. The message was then dequeued in pullAndDispatch() without firing any handler:
    // silently SWALLOWED. Mirror TopicTree by keeping only the literal prefix up to
    // ".*" and letting ".*" match the rest.
    size_t multiPos = pattern.find(".*");
    if (multiPos != std::string::npos) {
        return std::regex(pattern.substr(0, multiPos) + ".*");
    }

    // Otherwise, convert simple wildcards to regex
    std::string escaped;
    for (char c : pattern) {
        if (c == '*') {
            // Simple wildcard: convert to regex ".*"
            escaped += ".*";
        } else if (c == '+' || c == '?' || c == '^' || c == '$' ||
                   c == '(' || c == ')' || c == '[' || c == ']' || c == '{' ||
                   c == '}' || c == '|' || c == '\\' || c == '.') {
            // Escape special regex characters
            escaped += '\\';
            escaped += c;
        } else {
            escaped += c;
        }
    }

    return std::regex(escaped);
}

void IntraIO::processLowFreqSubscriptions() {
    // Simplified: flush all batched messages
    for (auto& sub : lowFreqSubscriptions) {
        flushBatchedMessages(sub);
    }
}

void IntraIO::flushBatchedMessages(Subscription& sub) {
    // Move accumulated messages to low-freq queue
    for (auto& [topic, msg] : sub.batchedMessages) {
        lowFreqMessageQueue.push(std::move(msg));
    }
    sub.batchedMessages.clear();

    for (auto& msg : sub.accumulatedMessages) {
        lowFreqMessageQueue.push(std::move(msg));
    }
    sub.accumulatedMessages.clear();
}

void IntraIO::updateHealthMetrics() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<float>(now - lastHealthCheck).count();

    if (duration > 0.0f) {
        float messagesPulled = static_cast<float>(totalPulled.load());
        averageProcessingRate = messagesPulled / duration;
    }

    lastHealthCheck = now;
}

void IntraIO::enforceQueueLimits() {
    // Bound the TOTAL queued messages to maxQueueSize. Drop the OLDEST first
    // (front of queue), draining the high-freq queue before the low-freq one, so a
    // stalled consumer causes BOUNDED, COUNTED message loss (totalDropped) instead of
    // unbounded memory growth → OOM. Caller holds operationMutex.
    // (Previously this drained only messageQueue, leaving lowFreqMessageQueue
    // unbounded; now both are capped.)
    size_t totalSize = messageQueue.size() + lowFreqMessageQueue.size();

    while (totalSize > maxQueueSize) {
        if (!messageQueue.empty()) {
            messageQueue.pop();
        } else if (!lowFreqMessageQueue.empty()) {
            lowFreqMessageQueue.pop();
        } else {
            break;
        }
        totalDropped++;
        totalSize--;
    }
}

void IntraIO::logPublish(const std::string& topic, const IDataNode& message) const {
    if (logger) {
        logger->trace("Published to topic: {}", topic);
    }
}

void IntraIO::logSubscription(const std::string& pattern, bool isLowFreq) const {
    if (logger) {
        logger->info("Subscribed to: {} ({})", pattern, isLowFreq ? "low-freq" : "high-freq");
    }
}

void IntraIO::logPull(const Message& message) const {
    if (logger) {
        logger->trace("Pulled message from topic: {}", message.topic);
    }
}

} // namespace grove
