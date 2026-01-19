#include "MetricsModule.h"
#include <iostream>

namespace grove {

MetricsModule::MetricsModule() {
    std::cout << "[MetricsModule] Constructor" << std::endl;
}

MetricsModule::~MetricsModule() {
    std::cout << "[MetricsModule] Destructor" << std::endl;
}

void MetricsModule::process(const IDataNode& input) {
    float deltaTime = static_cast<float>(input.getDouble("deltaTime", 1.0/60.0));

    accumulator += deltaTime;

    // Collect metrics every 100ms
    if (accumulator >= 0.1f) {
        collectMetrics();
        accumulator = 0.0f;
    }

    // Pull and dispatch all pending messages (callbacks invoked automatically)
    if (io) {
        while (io->hasMessages() > 0) {
            io->pullAndDispatch();
        }
    }
}

void MetricsModule::setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) {
    std::cout << "[MetricsModule] setConfiguration called" << std::endl;

    this->io = ioPtr;
    this->scheduler = schedulerPtr;

    // Store config
    config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());

    // Subscribe to economy events with callback
    if (io) {
        io->subscribe("economy:*", [this](const Message& msg) {
            std::cout << "[MetricsModule] Received: " << msg.topic << std::endl;
        });
    }
}

const IDataNode& MetricsModule::getConfiguration() {
    if (!config) {
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
    }
    return *config;
}

std::unique_ptr<IDataNode> MetricsModule::getHealthStatus() {
    nlohmann::json health = {
        {"status", "healthy"},
        {"snapshotsPublished", snapshotsPublished}
    };
    return std::make_unique<JsonDataNode>("health", health);
}

void MetricsModule::shutdown() {
    std::cout << "[MetricsModule] Shutdown - Published " << snapshotsPublished << " snapshots" << std::endl;
}

std::unique_ptr<IDataNode> MetricsModule::getState() {
    nlohmann::json state = {
        {"snapshotsPublished", snapshotsPublished},
        {"accumulator", accumulator}
    };
    return std::make_unique<JsonDataNode>("state", state);
}

void MetricsModule::setState(const IDataNode& state) {
    snapshotsPublished = state.getInt("snapshotsPublished", 0);
    accumulator = static_cast<float>(state.getDouble("accumulator", 0.0));
    std::cout << "[MetricsModule] State restored" << std::endl;
}

void MetricsModule::setDataTree(IDataTree* treePtr) {
    this->tree = treePtr;
}

void MetricsModule::collectMetrics() {
    if (!tree) return;

    auto runtimeRoot = tree->getRuntimeRoot();

    nlohmann::json metricsData = {
        {"fps", 60.0},
        {"memory", 125000000},
        {"messageCount", snapshotsPublished}
    };

    auto metrics = std::make_unique<JsonDataNode>("metrics", metricsData);

    // Update runtime metrics (not persisted)
    // Note: Cannot use setChild directly, would need proper implementation
}

void MetricsModule::publishSnapshot() {
    if (!io) return;

    nlohmann::json snapshot = {
        {"fps", 60.0},
        {"memory", 125000000},
        {"snapshotsPublished", snapshotsPublished}
    };

    auto dataNode = std::make_unique<JsonDataNode>("snapshot", snapshot);
    io->publish("metrics:snapshot", std::move(dataNode));

    snapshotsPublished++;
}

} // namespace grove

// Export C API
extern "C" {
    grove::IModule* createModule() {
        return new grove::MetricsModule();
    }
}
