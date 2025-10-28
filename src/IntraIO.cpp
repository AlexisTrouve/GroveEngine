#include <grove/IntraIO.h>
#include <grove/IntraIOManager.h>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace warfactory {

// Factory function for IntraIOManager to avoid circular include
std::shared_ptr<IntraIO> createIntraIOInstance(const std::string& instanceId) {
    return std::make_shared<IntraIO>(instanceId);
}

IntraIO::IntraIO(const std::string& instanceId) : instanceId(instanceId) {
    // Create logger with file and console output
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/intra_io.log", true);

    console_sink->set_level(spdlog::level::debug);
    file_sink->set_level(spdlog::level::trace);

    logger = std::make_shared<spdlog::logger>("IntraIO[" + instanceId + "]",
        spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::debug);

    spdlog::register_logger(logger);

    logIOStart();
    lastHealthCheck = std::chrono::high_resolution_clock::now();
}

IntraIO::~IntraIO() {
    logger->info("🌐 IntraIO[{}] destructor called", instanceId);

    // Unregister from manager
    try {
        IntraIOManager::getInstance().removeInstance(instanceId);
    } catch (const std::exception& e) {
        logger->warn("⚠️ Failed to unregister from manager: {}", e.what());
    }

    auto finalMetrics = getDetailedMetrics();
    logger->info("📊 Final IntraIO[{}] metrics:", instanceId);
    logger->info("   Total published: {}", finalMetrics["total_published"]);
    logger->info("   Total pulled: {}", finalMetrics["total_pulled"]);
    logger->info("   Total dropped: {}", finalMetrics["total_dropped"]);
    logger->info("   Final queue size: {}", finalMetrics["queue_size"]);

    logger->trace("🏗️ IntraIO[{}] destroyed", instanceId);
}

void IntraIO::publish(const std::string& topic, const json& message) {
    std::lock_guard<std::mutex> lock(operationMutex);

    logPublish(topic, message);
    totalPublished++;

    try {
        // Route message through manager to all interested instances
        IntraIOManager::getInstance().routeMessage(instanceId, topic, message);
        logger->trace("📤 Message routed through manager: '{}'", topic);

    } catch (const std::exception& e) {
        logger->error("❌ Error publishing message to topic '{}': {}", topic, e.what());
        throw;
    }
}

void IntraIO::subscribe(const std::string& topicPattern, const SubscriptionConfig& config) {
    std::lock_guard<std::mutex> lock(operationMutex);

    logSubscription(topicPattern, false);

    try {
        // Register with manager for routing
        IntraIOManager::getInstance().registerSubscription(instanceId, topicPattern, false);

        Subscription sub;
        sub.pattern = compileTopicPattern(topicPattern);
        sub.originalPattern = topicPattern;
        sub.config = config;
        sub.lastBatch = std::chrono::high_resolution_clock::now();

        highFreqSubscriptions.push_back(std::move(sub));

        logger->info("✅ High-frequency subscription added: '{}'", topicPattern);
        logger->debug("🔧 Subscription config: replaceable={}, compress={}",
                     config.replaceable, config.compress);

    } catch (const std::exception& e) {
        logger->error("❌ Error creating subscription for pattern '{}': {}", topicPattern, e.what());
        throw;
    }
}

void IntraIO::subscribeLowFreq(const std::string& topicPattern, const SubscriptionConfig& config) {
    std::lock_guard<std::mutex> lock(operationMutex);

    logSubscription(topicPattern, true);

    try {
        // Register with manager for routing
        IntraIOManager::getInstance().registerSubscription(instanceId, topicPattern, true);

        Subscription sub;
        sub.pattern = compileTopicPattern(topicPattern);
        sub.originalPattern = topicPattern;
        sub.config = config;
        sub.lastBatch = std::chrono::high_resolution_clock::now();

        lowFreqSubscriptions.push_back(std::move(sub));

        logger->info("✅ Low-frequency subscription added: '{}' (interval: {}ms)",
                    topicPattern, config.batchInterval);
        logger->debug("🔧 LowFreq config: replaceable={}, batchSize={}, interval={}ms",
                     config.replaceable, config.maxBatchSize, config.batchInterval);

    } catch (const std::exception& e) {
        logger->error("❌ Error creating low-freq subscription for pattern '{}': {}", topicPattern, e.what());
        throw;
    }
}

int IntraIO::hasMessages() const {
    std::lock_guard<std::mutex> lock(operationMutex);

    int totalMessages = messageQueue.size() + lowFreqMessageQueue.size();

    logger->trace("🔍 Messages available: {} (high-freq: {}, low-freq: {})",
                 totalMessages, messageQueue.size(), lowFreqMessageQueue.size());

    return totalMessages;
}

Message IntraIO::pullMessage() {
    std::lock_guard<std::mutex> lock(operationMutex);

    Message msg;

    // Pull from high-frequency queue first (priority)
    if (!messageQueue.empty()) {
        msg = messageQueue.front();
        messageQueue.pop();
        logger->trace("📥 Pulled high-frequency message from topic: '{}'", msg.topic);
    } else if (!lowFreqMessageQueue.empty()) {
        msg = lowFreqMessageQueue.front();
        lowFreqMessageQueue.pop();
        logger->trace("📥 Pulled low-frequency message from topic: '{}'", msg.topic);
    } else {
        logger->error("❌ No messages available to pull");
        throw std::runtime_error("No messages available in IntraIO");
    }

    totalPulled++;
    logPull(msg);
    updateHealthMetrics();

    return msg;
}

IOHealth IntraIO::getHealth() const {
    std::lock_guard<std::mutex> lock(operationMutex);
    updateHealthMetrics();

    IOHealth health;
    health.queueSize = messageQueue.size() + lowFreqMessageQueue.size();
    health.maxQueueSize = maxQueueSize;
    health.dropping = health.queueSize >= maxQueueSize;
    health.averageProcessingRate = averageProcessingRate;
    health.droppedMessageCount = totalDropped.load();

    logger->trace("🏥 Health check: queue={}/{}, dropping={}, rate={:.1f}msg/s",
                 health.queueSize, health.maxQueueSize, health.dropping, health.averageProcessingRate);

    return health;
}

IOType IntraIO::getType() const {
    logger->trace("🏷️ IO type requested: INTRA");
    return IOType::INTRA;
}

void IntraIO::setMaxQueueSize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(operationMutex);

    logger->info("🔧 Setting max queue size: {} -> {}", maxQueueSize, maxSize);
    maxQueueSize = maxSize;
}

size_t IntraIO::getMaxQueueSize() const {
    return maxQueueSize;
}

void IntraIO::clearAllMessages() {
    std::lock_guard<std::mutex> lock(operationMutex);

    size_t clearedCount = messageQueue.size() + lowFreqMessageQueue.size();

    while (!messageQueue.empty()) messageQueue.pop();
    while (!lowFreqMessageQueue.empty()) lowFreqMessageQueue.pop();

    logger->info("🧹 Cleared all messages: {} messages removed", clearedCount);
}

void IntraIO::clearAllSubscriptions() {
    std::lock_guard<std::mutex> lock(operationMutex);

    size_t clearedCount = highFreqSubscriptions.size() + lowFreqSubscriptions.size();

    highFreqSubscriptions.clear();
    lowFreqSubscriptions.clear();

    logger->info("🧹 Cleared all subscriptions: {} subscriptions removed", clearedCount);
}

json IntraIO::getDetailedMetrics() const {
    std::lock_guard<std::mutex> lock(operationMutex);

    json metrics = {
        {"io_type", "intra"},
        {"queue_size", messageQueue.size() + lowFreqMessageQueue.size()},
        {"high_freq_queue_size", messageQueue.size()},
        {"low_freq_queue_size", lowFreqMessageQueue.size()},
        {"max_queue_size", maxQueueSize},
        {"total_published", totalPublished.load()},
        {"total_pulled", totalPulled.load()},
        {"total_dropped", totalDropped.load()},
        {"high_freq_subscriptions", highFreqSubscriptions.size()},
        {"low_freq_subscriptions", lowFreqSubscriptions.size()},
        {"average_processing_rate", averageProcessingRate}
    };

    logger->trace("📊 Detailed metrics: {}", metrics.dump());
    return metrics;
}

void IntraIO::setLogLevel(spdlog::level::level_enum level) {
    logger->info("🔧 Setting log level to: {}", spdlog::level::to_string_view(level));
    logger->set_level(level);
}

size_t IntraIO::getSubscriptionCount() const {
    std::lock_guard<std::mutex> lock(operationMutex);
    return highFreqSubscriptions.size() + lowFreqSubscriptions.size();
}

std::vector<std::string> IntraIO::getActiveTopics() const {
    std::lock_guard<std::mutex> lock(operationMutex);

    std::unordered_set<std::string> topicSet;
    std::queue<Message> tempQueue = messageQueue;

    while (!tempQueue.empty()) {
        topicSet.insert(tempQueue.front().topic);
        tempQueue.pop();
    }

    tempQueue = lowFreqMessageQueue;
    while (!tempQueue.empty()) {
        topicSet.insert(tempQueue.front().topic);
        tempQueue.pop();
    }

    return std::vector<std::string>(topicSet.begin(), topicSet.end());
}

void IntraIO::simulateHighLoad(int messageCount, const std::string& topicPrefix) {
    logger->info("🧪 Simulating high load: {} messages with prefix '{}'", messageCount, topicPrefix);

    for (int i = 0; i < messageCount; ++i) {
        json testMessage = {
            {"test_id", i},
            {"payload", "test_data_" + std::to_string(i)},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count()}
        };

        publish(topicPrefix + ":" + std::to_string(i), testMessage);
    }

    logger->info("✅ High load simulation completed");
}

void IntraIO::forceProcessLowFreqBatches() {
    std::lock_guard<std::mutex> lock(operationMutex);
    logger->debug("🔧 Force processing all low-frequency batches");

    for (auto& sub : lowFreqSubscriptions) {
        flushBatchedMessages(sub);
    }
}

// Private helper methods
void IntraIO::logIOStart() {
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("🌐 INTRA-PROCESS IO INITIALIZED");
    logger->info("=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=" "=");
    logger->info("🎯 Transport Type: INTRA (Same-process)");
    logger->info("🔧 Features: Direct function calls, zero latency");
    logger->info("📊 Performance: ~10-50ns publish, thread-safe");
    logger->info("🔧 Max queue size: {}", maxQueueSize);
    logger->trace("🏗️ IntraIO object created at: {}", static_cast<void*>(this));
}

bool IntraIO::matchesPattern(const std::string& topic, const std::regex& pattern) const {
    return std::regex_match(topic, pattern);
}

std::regex IntraIO::compileTopicPattern(const std::string& pattern) const {
    // Convert wildcard pattern to regex
    std::string regexPattern = pattern;

    // Escape special regex characters except our wildcards
    std::string specialChars = ".^$+()[]{}|\\";
    for (char c : specialChars) {
        std::string from = std::string(1, c);
        std::string to = "\\" + from;

        size_t pos = 0;
        while ((pos = regexPattern.find(from, pos)) != std::string::npos) {
            regexPattern.replace(pos, 1, to);
            pos += 2;
        }
    }

    // Convert * to regex equivalent
    size_t pos2 = 0;
    while ((pos2 = regexPattern.find("*", pos2)) != std::string::npos) {
        regexPattern.replace(pos2, 1, ".*");
        pos2 += 2;
    }

    logger->trace("🔍 Compiled pattern '{}' -> '{}'", pattern, regexPattern);

    return std::regex(regexPattern);
}

void IntraIO::processLowFreqSubscriptions() {
    auto currentTime = std::chrono::high_resolution_clock::now();

    for (auto& sub : lowFreqSubscriptions) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - sub.lastBatch).count();

        if (elapsed >= sub.config.batchInterval) {
            logger->trace("⏰ Processing low-freq batch for pattern '{}' ({}ms elapsed)",
                         sub.originalPattern, elapsed);
            flushBatchedMessages(sub);
            sub.lastBatch = currentTime;
        }
    }
}

void IntraIO::flushBatchedMessages(Subscription& sub) {
    size_t flushedCount = 0;

    // Flush replaceable messages (latest only)
    for (auto& [topic, message] : sub.batchedMessages) {
        lowFreqMessageQueue.push(message);
        flushedCount++;
        logger->trace("📤 Flushed replaceable message: topic '{}', data size {}",
                     topic, message.data.dump().size());
    }
    sub.batchedMessages.clear();

    // Flush accumulated messages (all)
    for (const auto& message : sub.accumulatedMessages) {
        lowFreqMessageQueue.push(message);
        flushedCount++;
        logger->trace("📤 Flushed accumulated message: topic '{}', data size {}",
                     message.topic, message.data.dump().size());
    }
    sub.accumulatedMessages.clear();

    if (flushedCount > 0) {
        logger->debug("📦 Flushed {} low-freq messages for pattern '{}'",
                     flushedCount, sub.originalPattern);
    }
}

void IntraIO::updateHealthMetrics() const {
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<float>(currentTime - lastHealthCheck).count();

    if (elapsed >= 1.0f) { // Update every second
        size_t currentPulled = totalPulled.load();
        static size_t lastPulledCount = 0;

        averageProcessingRate = (currentPulled - lastPulledCount) / elapsed;
        lastPulledCount = currentPulled;
        lastHealthCheck = currentTime;

        logger->trace("📊 Health metrics updated: rate={:.1f}msg/s", averageProcessingRate);
    }
}

void IntraIO::enforceQueueLimits() {
    size_t totalSize = messageQueue.size() + lowFreqMessageQueue.size();

    if (totalSize >= maxQueueSize) {
        logger->warn("⚠️ Queue size limit reached: {}/{} - dropping oldest messages", totalSize, maxQueueSize);

        // Drop oldest messages to make room
        size_t toDrop = totalSize - maxQueueSize + 1;

        for (size_t i = 0; i < toDrop && !messageQueue.empty(); ++i) {
            messageQueue.pop();
            totalDropped++;
        }

        logger->warn("🗑️ Dropped {} messages to enforce queue limit", toDrop);
    }
}

void IntraIO::logPublish(const std::string& topic, const json& message) const {
    logger->trace("📡 Publishing to topic '{}', data size: {} bytes",
                 topic, message.dump().size());
}

void IntraIO::logSubscription(const std::string& pattern, bool isLowFreq) const {
    logger->debug("📨 {} subscription request: pattern '{}'",
                 isLowFreq ? "Low-frequency" : "High-frequency", pattern);
}

void IntraIO::logPull(const Message& message) const {
    logger->trace("📥 Message pulled: topic '{}', timestamp {}, data size {} bytes",
                 message.topic, message.timestamp, message.data.dump().size());
}

void IntraIO::deliverMessage(const std::string& topic, const json& message, bool isLowFreq) {
    std::lock_guard<std::mutex> lock(operationMutex);

    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    Message msg{topic, message, static_cast<uint64_t>(timestamp)};

    try {
        if (isLowFreq) {
            // Handle low-frequency message delivery
            for (auto& sub : lowFreqSubscriptions) {
                if (matchesPattern(topic, sub.pattern)) {
                    if (sub.config.replaceable) {
                        sub.batchedMessages[topic] = msg;
                        logger->trace("🔄 Low-freq replaceable message delivered: '{}'", topic);
                    } else {
                        sub.accumulatedMessages.push_back(msg);
                        logger->trace("📚 Low-freq message accumulated: '{}'", topic);
                    }
                    break;
                }
            }
        } else {
            // Handle high-frequency message delivery
            logger->info("🔍 deliverMessage: looking for high-freq subscriptions for '{}', have {} subs", topic, highFreqSubscriptions.size());
            for (const auto& sub : highFreqSubscriptions) {
                logger->info("🔍 deliverMessage: testing pattern '{}' vs topic '{}'", sub.originalPattern, topic);
                if (matchesPattern(topic, sub.pattern)) {
                    messageQueue.push(msg);
                    logger->info("📨 High-freq message delivered to queue: '{}'", topic);
                    break;
                } else {
                    logger->info("❌ Pattern '{}' did not match topic '{}'", sub.originalPattern, topic);
                }
            }
        }

        // Enforce queue limits
        enforceQueueLimits();

    } catch (const std::exception& e) {
        logger->error("❌ Error delivering message to topic '{}': {}", topic, e.what());
        throw;
    }
}

const std::string& IntraIO::getInstanceId() const {
    return instanceId;
}

} // namespace warfactory