#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <stdexcept>
#include <logger/Logger.h>

namespace grove {

IntraIOManager::IntraIOManager() {
    // Create logger with domain organization (file logging disabled for Windows compatibility)
    stillhammer::LoggerConfig config;
    config.disableFile();  // TEMPORARY: Disable file logging to fix Windows crash
    logger = stillhammer::createDomainLogger("IntraIOManager", "io", config);
    logger->info("🌐🔗 IntraIOManager created - Central message router initialized");

    // Start batch flush thread for low-latency message delivery
    batchThreadRunning = true;
    batchThread = std::thread(&IntraIOManager::batchFlushLoop, this);
    logger->info("✅ Batch flush thread started for push-based message delivery");
}

IntraIOManager::~IntraIOManager() {
    // Stop batch thread first
    batchThreadRunning = false;
    // Join the batch thread
    if (batchThread.joinable()) {
        batchThread.join();
    }

    // IMPORTANT: During static destruction order on Windows (especially MinGW GCC 15),
    // spdlog's registry may be destroyed BEFORE this singleton destructor runs.
    // Using a destroyed logger causes STATUS_STACK_BUFFER_OVERRUN (0xc0000409).
    // We must check if our logger is still valid before using it.
    bool loggerValid = false;
    try {
        // Check if spdlog registry still exists and our logger is registered
        loggerValid = logger && spdlog::get(logger->name()) != nullptr;
    } catch (...) {
        // spdlog registry may throw during destruction
        loggerValid = false;
    }

    if (loggerValid) {
        logger->info("🛑 Batch flush thread stopped");

        // Get stats before locking to avoid recursive lock
        auto stats = getRoutingStats();
        logger->info("📊 Final routing stats:");
        logger->info("   Total routed messages: {}", stats["total_routed_messages"].get<size_t>());
        logger->info("   Total routes: {}", stats["total_routes"].get<size_t>());
        logger->info("   Active instances: {}", stats["active_instances"].get<size_t>());
    }

    {
        std::unique_lock lock(managerMutex);  // WRITE - exclusive access needed
        instances.clear();
        topicTree.clear();
        instancePatterns.clear();
        subscriptionInfoMap.clear();
    }

    {
        std::lock_guard<std::mutex> batchLock(batchMutex);
        batchBuffers.clear();
    }

    if (loggerValid) {
        logger->info("🌐🔗 IntraIOManager destroyed");
    }
}

std::shared_ptr<IntraIO> IntraIOManager::createInstance(const std::string& instanceId) {
    std::unique_lock lock(managerMutex);  // WRITE - exclusive access needed

    auto it = instances.find(instanceId);
    if (it != instances.end()) {
        logger->warn("⚠️ Instance '{}' already exists, returning existing", instanceId);
        // Need to cast back to IntraIO
        return std::static_pointer_cast<IntraIO>(it->second);
    }

    // Create new IntraIO instance via factory function
    auto instance = createIntraIOInstance(instanceId);
    instances[instanceId] = instance;

    logger->info("✅ Created IntraIO instance: '{}'", instanceId);
    logger->debug("📊 Total instances: {}", instances.size());

    return instance;
}

void IntraIOManager::registerInstance(const std::string& instanceId, std::shared_ptr<IIntraIODelivery> instance) {
    std::unique_lock lock(managerMutex);  // WRITE - exclusive access needed
    instances[instanceId] = instance;
    logger->info("📋 Registered instance: '{}'", instanceId);
}

void IntraIOManager::removeInstance(const std::string& instanceId) {
    std::unique_lock lock(managerMutex);  // WRITE - exclusive access needed

    auto it = instances.find(instanceId);
    if (it == instances.end()) {
        logger->warn("⚠️ Instance '{}' not found for removal", instanceId);
        return;
    }

    // Remove all subscriptions for this instance from TopicTree
    topicTree.unregisterSubscriberAll(instanceId);

    // Clean up tracking data
    instancePatterns.erase(instanceId);

    instances.erase(it);

    logger->info("🗑️ Removed IntraIO instance: '{}'", instanceId);
    logger->debug("📊 Remaining instances: {}", instances.size());
}

std::shared_ptr<IntraIO> IntraIOManager::getInstance(const std::string& instanceId) const {
    std::shared_lock lock(managerMutex);  // READ - concurrent access allowed!

    auto it = instances.find(instanceId);
    if (it != instances.end()) {
        return std::static_pointer_cast<IntraIO>(it->second);
    }
    return nullptr;
}

void IntraIOManager::routeMessage(const std::string& sourceId, const std::string& topic, const json& messageData) {
    // DEADLOCK FIX: Use scoped_lock for consistent lock ordering when both mutexes needed
    std::scoped_lock lock(managerMutex, batchMutex);

    totalRoutedMessages++;
    messagesSinceLastLog++;
    size_t deliveredCount = 0;

    // Batched logging - log tous les LOG_BATCH_SIZE messages
    bool shouldLog = (messagesSinceLastLog % LOG_BATCH_SIZE == 0);

    if (shouldLog) {
        logger->info("📊 Routing stats: {} total messages routed", totalRoutedMessages.load());
    }

    logger->trace("📨 Routing message: {} → '{}'", sourceId, topic);

    // Find all matching subscribers - O(k) where k = topic depth
    auto subscribers = topicTree.findSubscribers(topic);

    logger->trace("  🔍 Found {} matching subscriber(s) for topic '{}'", subscribers.size(), topic);

    for (const auto& subscriberId : subscribers) {
        // Don't deliver back to sender
        if (subscriberId == sourceId) {
            logger->debug("  ⏭️ Skipping sender '{}'", subscriberId);
            continue;
        }

        auto targetInstance = instances.find(subscriberId);
        if (targetInstance != instances.end()) {
            // Get subscription info for this subscriber
            // IMPORTANT: We need to find which pattern actually matched this topic!
            bool isLowFreq = false;
            std::string matchedPattern;

            // Helper lambda to check if a pattern matches a topic
            auto patternMatches = [](const std::string& pattern, const std::string& topic) -> bool {
                // Simple wildcard matching: convert pattern to check
                // pattern: "batch:.*" matches topic: "batch:metric"
                // pattern: "player:*" matches topic: "player:123" but not "player:123:health"

                size_t ppos = 0, tpos = 0;
                while (ppos < pattern.size() && tpos < topic.size()) {
                    if (pattern.substr(ppos, 2) == ".*") {
                        // Multi-level wildcard - matches everything from here
                        return true;
                    } else if (pattern[ppos] == '*') {
                        // Single-level wildcard - match until next : or end
                        while (tpos < topic.size() && topic[tpos] != ':') {
                            tpos++;
                        }
                        ppos++;
                    } else if (pattern[ppos] == topic[tpos]) {
                        ppos++;
                        tpos++;
                    } else {
                        return false;
                    }
                }
                return ppos == pattern.size() && tpos == topic.size();
            };

            for (const auto& pattern : instancePatterns[subscriberId]) {
                auto it = subscriptionInfoMap.find(pattern);
                if (it != subscriptionInfoMap.end() && patternMatches(pattern, topic)) {
                    isLowFreq = it->second.isLowFreq;
                    matchedPattern = pattern;
                    logger->debug("  🔍 Pattern '{}' matched topic '{}' → isLowFreq={}", pattern, topic, isLowFreq);
                    break;
                }
            }

            if (isLowFreq) {
                // Add to batch buffer instead of immediate delivery
                // NOTE: batchMutex already held via scoped_lock

                auto& buffer = batchBuffers[matchedPattern];
                buffer.instanceId = subscriberId;
                buffer.pattern = matchedPattern;
                buffer.messages.push_back({topic, messageData});

                deliveredCount++;
                logger->debug("  📦 Buffered for '{}' (pattern: {}, buffer size: {})",
                             subscriberId, matchedPattern, buffer.messages.size());
            } else {
                // High-freq: immediate delivery
                json dataCopy = messageData;
                auto dataNode = std::make_unique<JsonDataNode>("message", dataCopy);
                targetInstance->second->deliverMessage(topic, std::move(dataNode), false);
                deliveredCount++;
                logger->trace("  ↪️ Delivered to '{}' (high-freq)", subscriberId);
            }
        } else {
            logger->warn("⚠️ Target instance '{}' not found", subscriberId);
        }
    }

    // Trace-only logging pour éviter spam
    logger->trace("📤 Message '{}' delivered to {} instances", topic, deliveredCount);
}

void IntraIOManager::registerSubscription(const std::string& instanceId, const std::string& pattern, bool isLowFreq, int batchInterval) {
    // DEADLOCK FIX: Use scoped_lock for consistent lock ordering
    std::scoped_lock lock(managerMutex, batchMutex);

    try {
        // Register in TopicTree - O(k) where k = pattern depth
        topicTree.registerSubscriber(pattern, instanceId);

        // Track pattern for management
        instancePatterns[instanceId].push_back(pattern);

        SubscriptionInfo info;
        info.instanceId = instanceId;
        info.isLowFreq = isLowFreq;
        info.batchInterval = batchInterval;
        subscriptionInfoMap[pattern] = info;

        // Initialize batch buffer if low-freq
        // NOTE: batchMutex already held via scoped_lock
        if (isLowFreq) {
            auto& buffer = batchBuffers[pattern];
            buffer.instanceId = instanceId;
            buffer.pattern = pattern;
            buffer.batchInterval = batchInterval;
            buffer.lastFlush = std::chrono::steady_clock::now();
            buffer.messages.clear();
        }

        totalRoutes++;

        logger->info("📋 Registered subscription: '{}' → '{}' ({}, interval={}ms)",
                    instanceId, pattern, isLowFreq ? "low-freq" : "high-freq", batchInterval);

    } catch (const std::exception& e) {
        logger->error("❌ Failed to register subscription '{}' for '{}': {}",
                     pattern, instanceId, e.what());
        throw;
    }
}

void IntraIOManager::unregisterSubscription(const std::string& instanceId, const std::string& pattern) {
    // DEADLOCK FIX: Use scoped_lock for consistent lock ordering
    std::scoped_lock lock(managerMutex, batchMutex);

    // Remove from TopicTree
    topicTree.unregisterSubscriber(pattern, instanceId);

    // Remove from tracking
    auto& patterns = instancePatterns[instanceId];
    patterns.erase(std::remove(patterns.begin(), patterns.end(), pattern), patterns.end());

    subscriptionInfoMap.erase(pattern);

    // Remove batch buffer if exists
    // NOTE: batchMutex already held via scoped_lock
    batchBuffers.erase(pattern);

    logger->info("🗑️ Unregistered subscription: '{}' → '{}'", instanceId, pattern);
}

void IntraIOManager::clearAllRoutes() {
    // DEADLOCK FIX: Use scoped_lock for consistent lock ordering
    std::scoped_lock lock(managerMutex, batchMutex);

    auto clearedCount = topicTree.subscriberCount();
    topicTree.clear();
    instancePatterns.clear();
    subscriptionInfoMap.clear();

    // NOTE: batchMutex already held via scoped_lock
    batchBuffers.clear();

    logger->info("🧹 Cleared {} routing entries", clearedCount);
}

size_t IntraIOManager::getInstanceCount() const {
    std::shared_lock lock(managerMutex);  // READ - concurrent access allowed!
    return instances.size();
}

std::vector<std::string> IntraIOManager::getInstanceIds() const {
    std::shared_lock lock(managerMutex);  // READ - concurrent access allowed!

    std::vector<std::string> ids;
    for (const auto& pair : instances) {
        ids.push_back(pair.first);
    }
    return ids;
}

json IntraIOManager::getRoutingStats() const {
    std::shared_lock lock(managerMutex);  // READ - concurrent access allowed!

    json stats;
    stats["total_routed_messages"] = totalRoutedMessages.load();
    stats["total_routes"] = totalRoutes.load();
    stats["active_instances"] = instances.size();
    stats["routing_entries"] = topicTree.subscriberCount();

    // Instance details
    json instanceDetails = json::object();
    for (const auto& pair : instances) {
        instanceDetails[pair.first] = {
            {"active", true},
            {"type", "IntraIO"}
        };
    }
    stats["instances"] = instanceDetails;

    return stats;
}

void IntraIOManager::setLogLevel(spdlog::level::level_enum level) {
    logger->set_level(level);
    logger->info("📝 Log level set to: {}", spdlog::level::to_string_view(level));
}

// Batch flush loop - runs in separate thread
// DEADLOCK FIX: Collect buffers to flush under batchMutex, then flush under managerMutex
void IntraIOManager::batchFlushLoop() {
    logger->info("🔄 Batch flush loop started");

    while (batchThreadRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Collect buffers that need flushing (under batchMutex only)
        std::vector<BatchBuffer> buffersToFlush;
        {
            std::lock_guard<std::mutex> batchLock(batchMutex);
            auto now = std::chrono::steady_clock::now();

            logger->trace("🔄 Batch flush check: {} buffers", batchBuffers.size());

            for (auto& [pattern, buffer] : batchBuffers) {
                if (buffer.messages.empty()) {
                    continue;
                }

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - buffer.lastFlush).count();

                logger->debug("🔄 Pattern '{}': {} messages, elapsed={}ms, interval={}ms",
                             pattern, buffer.messages.size(), elapsed, buffer.batchInterval);

                if (elapsed >= buffer.batchInterval) {
                    logger->info("📦 Triggering flush for pattern '{}' ({} messages)", pattern, buffer.messages.size());
                    // Copy buffer for flush, clear original
                    buffersToFlush.push_back(buffer);
                    buffer.messages.clear();
                    buffer.lastFlush = now;
                }
            }
        }
        // batchMutex released here

        // Now flush each buffer (under managerMutex only) - NO DEADLOCK
        for (auto& buffer : buffersToFlush) {
            flushBatchBufferSafe(buffer);
        }
    }

    logger->info("🔄 Batch flush loop stopped");
}

void IntraIOManager::flushBatchBuffer(BatchBuffer& buffer) {
    // DEPRECATED: Use flushBatchBufferSafe instead to avoid deadlocks
    flushBatchBufferSafe(buffer);
}

// Safe version that only takes managerMutex (called after releasing batchMutex)
void IntraIOManager::flushBatchBufferSafe(BatchBuffer& buffer) {
    if (buffer.messages.empty()) {
        return;
    }

    std::unique_lock lock(managerMutex);  // WRITE - exclusive access needed

    auto targetInstance = instances.find(buffer.instanceId);
    if (targetInstance == instances.end()) {
        logger->warn("⚠️ Cannot flush batch for '{}': instance not found", buffer.instanceId);
        buffer.messages.clear();
        return;
    }

    size_t batchSize = buffer.messages.size();
    logger->info("📦 Flushing batch for '{}': {} messages (pattern: {})",
                 buffer.instanceId, batchSize, buffer.pattern);

    // Create a single batch message containing all messages as an array
    json batchArray = json::array();
    std::string firstTopic;

    for (const auto& [topic, messageData] : buffer.messages) {
        if (firstTopic.empty()) {
            firstTopic = topic;
        }
        batchArray.push_back({
            {"topic", topic},
            {"data", messageData}
        });
    }

    // Deliver ONE batch message containing the array
    auto batchDataNode = std::make_unique<JsonDataNode>("batch", batchArray);
    targetInstance->second->deliverMessage(firstTopic, std::move(batchDataNode), true);

    logger->info("✅ Batch delivered to '{}' successfully", buffer.instanceId);

    buffer.messages.clear();
}

// Singleton implementation
IntraIOManager& IntraIOManager::getInstance() {
    static IntraIOManager instance;
    return instance;
}

} // namespace grove