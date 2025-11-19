#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
#include <grove/JsonDataNode.h>
#include <stdexcept>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace grove {

IntraIOManager::IntraIOManager() {
    // Create logger
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/intra_io_manager.log", true);

    console_sink->set_level(spdlog::level::debug);
    file_sink->set_level(spdlog::level::trace);

    logger = std::make_shared<spdlog::logger>("IntraIOManager",
        spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::debug);

    spdlog::register_logger(logger);

    logger->info("🌐🔗 IntraIOManager created - Central message router initialized");
}

IntraIOManager::~IntraIOManager() {
    std::lock_guard<std::mutex> lock(managerMutex);

    auto stats = getRoutingStats();
    logger->info("📊 Final routing stats:");
    logger->info("   Total routed messages: {}", stats["total_routed_messages"]);
    logger->info("   Total routes: {}", stats["total_routes"]);
    logger->info("   Active instances: {}", stats["active_instances"]);

    instances.clear();
    topicTree.clear();
    instancePatterns.clear();
    subscriptionFreqMap.clear();

    logger->info("🌐🔗 IntraIOManager destroyed");
}

std::shared_ptr<IntraIO> IntraIOManager::createInstance(const std::string& instanceId) {
    std::lock_guard<std::mutex> lock(managerMutex);

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
    std::lock_guard<std::mutex> lock(managerMutex);
    instances[instanceId] = instance;
    logger->info("📋 Registered instance: '{}'", instanceId);
}

void IntraIOManager::removeInstance(const std::string& instanceId) {
    std::lock_guard<std::mutex> lock(managerMutex);

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
    std::lock_guard<std::mutex> lock(managerMutex);

    auto it = instances.find(instanceId);
    if (it != instances.end()) {
        return std::static_pointer_cast<IntraIO>(it->second);
    }
    return nullptr;
}

void IntraIOManager::routeMessage(const std::string& sourceId, const std::string& topic, const json& messageData) {
    std::lock_guard<std::mutex> lock(managerMutex);

    totalRoutedMessages++;
    messagesSinceLastLog++;
    size_t deliveredCount = 0;

    // Batched logging - log tous les LOG_BATCH_SIZE messages
    bool shouldLog = (messagesSinceLastLog % LOG_BATCH_SIZE == 0);

    if (shouldLog) {
        logger->info("📊 Routing stats: {} total messages routed", totalRoutedMessages.load());
    }

    logger->trace("📨 Routing message: {} → '{}'", sourceId, topic);

    // Find all matching subscribers - O(k) where k = topic depth 🚀
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
            // Copy JSON data for each recipient (JSON is copyable!)
            json dataCopy = messageData;

            // Recreate DataNode from JSON copy
            auto dataNode = std::make_unique<JsonDataNode>("message", dataCopy);

            // Get frequency info (default to false if not found)
            bool isLowFreq = false;
            for (const auto& pattern : instancePatterns[subscriberId]) {
                auto it = subscriptionFreqMap.find(pattern);
                if (it != subscriptionFreqMap.end()) {
                    isLowFreq = it->second;
                    break;
                }
            }

            // Deliver to target instance's queue
            targetInstance->second->deliverMessage(topic, std::move(dataNode), isLowFreq);
            deliveredCount++;
            logger->trace("  ↪️ Delivered to '{}' ({})",
                          subscriberId,
                          isLowFreq ? "low-freq" : "high-freq");
        } else {
            logger->warn("⚠️ Target instance '{}' not found", subscriberId);
        }
    }

    // Trace-only logging pour éviter spam
    logger->trace("📤 Message '{}' delivered to {} instances", topic, deliveredCount);
}

void IntraIOManager::registerSubscription(const std::string& instanceId, const std::string& pattern, bool isLowFreq) {
    std::lock_guard<std::mutex> lock(managerMutex);

    try {
        // Register in TopicTree - O(k) where k = pattern depth
        topicTree.registerSubscriber(pattern, instanceId);

        // Track pattern for management
        instancePatterns[instanceId].push_back(pattern);
        subscriptionFreqMap[pattern] = isLowFreq;

        totalRoutes++;

        logger->info("📋 Registered subscription: '{}' → '{}' ({})",
                    instanceId, pattern, isLowFreq ? "low-freq" : "high-freq");

    } catch (const std::exception& e) {
        logger->error("❌ Failed to register subscription '{}' for '{}': {}",
                     pattern, instanceId, e.what());
        throw;
    }
}

void IntraIOManager::unregisterSubscription(const std::string& instanceId, const std::string& pattern) {
    std::lock_guard<std::mutex> lock(managerMutex);

    // Remove from TopicTree
    topicTree.unregisterSubscriber(pattern, instanceId);

    // Remove from tracking
    auto& patterns = instancePatterns[instanceId];
    patterns.erase(std::remove(patterns.begin(), patterns.end(), pattern), patterns.end());

    subscriptionFreqMap.erase(pattern);

    logger->info("🗑️ Unregistered subscription: '{}' → '{}'", instanceId, pattern);
}

void IntraIOManager::clearAllRoutes() {
    std::lock_guard<std::mutex> lock(managerMutex);

    auto clearedCount = topicTree.subscriberCount();
    topicTree.clear();
    instancePatterns.clear();
    subscriptionFreqMap.clear();

    logger->info("🧹 Cleared {} routing entries", clearedCount);
}

size_t IntraIOManager::getInstanceCount() const {
    std::lock_guard<std::mutex> lock(managerMutex);
    return instances.size();
}

std::vector<std::string> IntraIOManager::getInstanceIds() const {
    std::lock_guard<std::mutex> lock(managerMutex);

    std::vector<std::string> ids;
    for (const auto& pair : instances) {
        ids.push_back(pair.first);
    }
    return ids;
}

json IntraIOManager::getRoutingStats() const {
    std::lock_guard<std::mutex> lock(managerMutex);

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

// Singleton implementation
IntraIOManager& IntraIOManager::getInstance() {
    static IntraIOManager instance;
    return instance;
}

} // namespace grove