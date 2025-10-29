#include <grove/IntraIOManager.h>
#include <grove/IntraIO.h>
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
    routingTable.clear();

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

    // Remove all routing entries for this instance
    routingTable.erase(
        std::remove_if(routingTable.begin(), routingTable.end(),
            [&instanceId](const RouteEntry& entry) {
                return entry.instanceId == instanceId;
            }),
        routingTable.end()
    );

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

void IntraIOManager::routeMessage(const std::string& sourceId, const std::string& topic, std::unique_ptr<IDataNode> message) {
    std::lock_guard<std::mutex> lock(managerMutex);

    totalRoutedMessages++;
    size_t deliveredCount = 0;

    logger->info("📨 Routing message: {} → '{}'", sourceId, topic);

    // Find all matching routes
    for (const auto& route : routingTable) {
        // Don't deliver back to sender
        if (route.instanceId == sourceId) {
            continue;
        }

        // Check pattern match
        logger->info("  🔍 Testing pattern '{}' against topic '{}'", route.originalPattern, topic);
        if (std::regex_match(topic, route.pattern)) {
            auto targetInstance = instances.find(route.instanceId);
            if (targetInstance != instances.end()) {
                // Clone message for each recipient (except the last one)
                // TODO: implement IDataNode::clone() for proper deep copy
                // For now we'll need to move for the last recipient
                // This is a limitation that will need IDataNode cloning support

                // Direct delivery to target instance's queue
                // Note: This will move the message, so only the first match will receive it
                // Full implementation needs IDataNode::clone()
                targetInstance->second->deliverMessage(topic, std::move(message), route.isLowFreq);
                deliveredCount++;
                logger->info("  ↪️ Delivered to '{}' ({})",
                             route.instanceId,
                             route.isLowFreq ? "low-freq" : "high-freq");
                // Break after first delivery since we moved the message
                break;
            } else {
                logger->warn("⚠️ Target instance '{}' not found for route", route.instanceId);
            }
        } else {
            logger->info("  ❌ Pattern '{}' did not match topic '{}'", route.originalPattern, topic);
        }
    }

    if (deliveredCount > 0) {
        logger->debug("📤 Message '{}' delivered to {} instances", topic, deliveredCount);
    } else {
        logger->trace("📪 No subscribers for topic '{}'", topic);
    }
}

void IntraIOManager::registerSubscription(const std::string& instanceId, const std::string& pattern, bool isLowFreq) {
    std::lock_guard<std::mutex> lock(managerMutex);

    try {
        // Convert topic pattern to regex - use same logic as IntraIO
        std::string regexPattern = pattern;

        // Escape special regex characters except our wildcards (: is NOT special)
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

        logger->info("🔍 Pattern conversion: '{}' → '{}'", pattern, regexPattern);

        RouteEntry entry;
        entry.instanceId = instanceId;
        entry.pattern = std::regex(regexPattern);
        entry.originalPattern = pattern;
        entry.isLowFreq = isLowFreq;

        routingTable.push_back(entry);
        totalRoutes++;

        logger->info("📋 Registered subscription: '{}' → '{}' ({})",
                    instanceId, pattern, isLowFreq ? "low-freq" : "high-freq");
        logger->debug("📊 Total routes: {}", routingTable.size());

    } catch (const std::exception& e) {
        logger->error("❌ Failed to register subscription '{}' for '{}': {}",
                     pattern, instanceId, e.what());
        throw;
    }
}

void IntraIOManager::unregisterSubscription(const std::string& instanceId, const std::string& pattern) {
    std::lock_guard<std::mutex> lock(managerMutex);

    auto oldSize = routingTable.size();
    routingTable.erase(
        std::remove_if(routingTable.begin(), routingTable.end(),
            [&instanceId, &pattern](const RouteEntry& entry) {
                return entry.instanceId == instanceId && entry.originalPattern == pattern;
            }),
        routingTable.end()
    );

    auto removed = oldSize - routingTable.size();
    if (removed > 0) {
        logger->info("🗑️ Unregistered {} subscription(s): '{}' → '{}'", removed, instanceId, pattern);
    } else {
        logger->warn("⚠️ Subscription not found for removal: '{}' → '{}'", instanceId, pattern);
    }
}

void IntraIOManager::clearAllRoutes() {
    std::lock_guard<std::mutex> lock(managerMutex);

    auto clearedCount = routingTable.size();
    routingTable.clear();

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
    stats["routing_entries"] = routingTable.size();

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