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
    std::cout << "[IntraIO] Destroyed instance: " << instanceId << std::endl;
}

void IntraIO::publish(const std::string& topic, std::unique_ptr<IDataNode> message) {
    std::lock_guard<std::mutex> lock(operationMutex);

    totalPublished++;

    // Extract JSON data from the DataNode
    auto* jsonNode = dynamic_cast<JsonDataNode*>(message.get());
    if (!jsonNode) {
        throw std::runtime_error("IntraIO::publish() requires JsonDataNode for message data");
    }

    // Get the JSON data (this is a const reference, no copy yet)
    const nlohmann::json& jsonData = jsonNode->getJsonData();

    // Route message via central manager (this will copy JSON for each subscriber)
    IntraIOManager::getInstance().routeMessage(instanceId, topic, jsonData);
}

void IntraIO::subscribe(const std::string& topicPattern, const SubscriptionConfig& config) {
    std::lock_guard<std::mutex> lock(operationMutex);

    Subscription sub;
    sub.originalPattern = topicPattern;
    sub.pattern = compileTopicPattern(topicPattern);
    sub.config = config;
    sub.lastBatch = std::chrono::high_resolution_clock::now();

    highFreqSubscriptions.push_back(std::move(sub));

    // Register subscription with central manager for routing
    IntraIOManager::getInstance().registerSubscription(instanceId, topicPattern, false);
}

void IntraIO::subscribeLowFreq(const std::string& topicPattern, const SubscriptionConfig& config) {
    std::lock_guard<std::mutex> lock(operationMutex);

    Subscription sub;
    sub.originalPattern = topicPattern;
    sub.pattern = compileTopicPattern(topicPattern);
    sub.config = config;
    sub.lastBatch = std::chrono::high_resolution_clock::now();

    lowFreqSubscriptions.push_back(std::move(sub));

    // Register subscription with central manager for routing
    IntraIOManager::getInstance().registerSubscription(instanceId, topicPattern, true, config.batchInterval);
}

int IntraIO::hasMessages() const {
    std::lock_guard<std::mutex> lock(operationMutex);
    return static_cast<int>(messageQueue.size() + lowFreqMessageQueue.size());
}

Message IntraIO::pullMessage() {
    std::lock_guard<std::mutex> lock(operationMutex);

    if (messageQueue.empty() && lowFreqMessageQueue.empty()) {
        throw std::runtime_error("No messages available");
    }

    Message msg;
    if (!messageQueue.empty()) {
        msg = std::move(messageQueue.front());
        messageQueue.pop();
    } else {
        msg = std::move(lowFreqMessageQueue.front());
        lowFreqMessageQueue.pop();
    }

    totalPulled++;
    return msg;
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

void IntraIO::deliverMessage(const std::string& topic, std::unique_ptr<IDataNode> message, bool isLowFreq) {
    std::lock_guard<std::mutex> lock(operationMutex);

    Message msg;
    msg.topic = topic;
    msg.data = std::move(message);
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (isLowFreq) {
        lowFreqMessageQueue.push(std::move(msg));
    } else {
        messageQueue.push(std::move(msg));
    }
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
    // Convert wildcard pattern to regex
    std::string regexPattern = pattern;

    // Escape special regex characters except *
    std::string escaped;
    for (char c : regexPattern) {
        if (c == '*') {
            escaped += ".*";
        } else if (c == '.' || c == '+' || c == '?' || c == '^' || c == '$' ||
                   c == '(' || c == ')' || c == '[' || c == ']' || c == '{' ||
                   c == '}' || c == '|' || c == '\\') {
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
    size_t totalSize = messageQueue.size() + lowFreqMessageQueue.size();

    while (totalSize > maxQueueSize && !messageQueue.empty()) {
        messageQueue.pop();
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
