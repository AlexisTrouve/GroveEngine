#include "ProducerModule.h"
#include <grove/JsonDataNode.h>
#include <iostream>

namespace grove {

ProducerModule::ProducerModule() {
    std::cout << "[ProducerModule] Constructor" << std::endl;
}

ProducerModule::~ProducerModule() {
    std::cout << "[ProducerModule] Destructor" << std::endl;
}

void ProducerModule::process(const IDataNode& input) {
    // Get delta time from input
    float deltaTime = static_cast<float>(input.getDouble("deltaTime", 1.0/60.0));

    accumulator += deltaTime;

    // Calculate interval based on publish rate
    float interval = (publishRate > 0) ? (1.0f / publishRate) : 1.0f;

    // Publish messages at specified rate
    while (accumulator >= interval && publishRate > 0) {
        accumulator -= interval;
        publishedCount++;

        // Publish a test message
        nlohmann::json data = {
            {"id", publishedCount},
            {"timestamp", static_cast<uint64_t>(publishedCount * interval * 1000)}
        };

        auto dataNode = std::make_unique<JsonDataNode>("message", data);

        // Check if we should publish (can be controlled via input)
        std::string topic = input.getString("publishTopic", "");
        if (!topic.empty() && io) {
            io->publish(topic, std::move(dataNode));
        }
    }
}

void ProducerModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[ProducerModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    // Store config
    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());

    // Get publish rate from config if provided
    publishRate = static_cast<float>(configNode.getDouble("publishRate", 100.0));

    std::cout << "[ProducerModule] Publish rate: " << publishRate << " Hz" << std::endl;
}

const IDataNode& ProducerModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> ProducerModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"publishedCount", publishedCount},
        {"publishRate", publishRate}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void ProducerModule::shutdown() {
    std::cout << "[ProducerModule] Shutdown - Published " << publishedCount << " messages" << std::endl;
}

std::unique_ptr<IDataNode> ProducerModule::getState() {
    nlohmann::json state = {
        {"publishedCount", publishedCount},
        {"publishRate", publishRate},
        {"accumulator", accumulator}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void ProducerModule::setState(const IDataNode& state) {
    publishedCount = state.getInt("publishedCount", 0);
    publishRate = static_cast<float>(state.getDouble("publishRate", 100.0));
    accumulator = static_cast<float>(state.getDouble("accumulator", 0.0));

    std::cout << "[ProducerModule] State restored - Count: " << publishedCount << std::endl;
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::ProducerModule();
    }
}
