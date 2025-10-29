#include <grove/IModule.h>
#include <grove/JsonDataNode.h>
#include <grove/JsonDataValue.h>
#include <iostream>
#include <memory>

namespace grove {

/**
 * @brief Simple test module for hot-reload validation
 *
 * This module demonstrates:
 * - State preservation across reloads
 * - IDataNode-based configuration
 * - Simple counter logic
 */
class TestModule : public IModule {
private:
    int counter = 0;
    std::string moduleVersion = "v1.0";
    IIO* io = nullptr;
    ITaskScheduler* scheduler = nullptr;
    std::unique_ptr<IDataNode> config;

public:
    TestModule() {
        std::cout << "[TestModule] Constructor called - " << moduleVersion << std::endl;
    }

    ~TestModule() override {
        std::cout << "[TestModule] Destructor called" << std::endl;
    }

    void process(const IDataNode& input) override {
        counter++;
        std::cout << "[TestModule] Process #" << counter
                  << " - Version: " << moduleVersion << std::endl;

        // Print input if available
        std::string message = input.getString("message", "");
        if (!message.empty()) {
            std::cout << "[TestModule] Received message: " << message << std::endl;
        }
    }

    void setConfiguration(const IDataNode& configNode, IIO* ioPtr, ITaskScheduler* schedulerPtr) override {
        std::cout << "[TestModule] Configuration set" << std::endl;

        this->io = ioPtr;
        this->scheduler = schedulerPtr;

        // Clone configuration for storage
        config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());

        // Extract version if available
        moduleVersion = configNode.getString("version", "v1.0");
        std::cout << "[TestModule] Version set to: " << moduleVersion << std::endl;
    }

    const IDataNode& getConfiguration() override {
        if (!config) {
            config = std::make_unique<JsonDataNode>("config", nlohmann::json::object());
        }
        return *config;
    }

    std::unique_ptr<IDataNode> getHealthStatus() override {
        nlohmann::json health = {
            {"status", "healthy"},
            {"counter", counter},
            {"version", moduleVersion}
        };
        return std::make_unique<JsonDataNode>("health", health);
    }

    void shutdown() override {
        std::cout << "[TestModule] Shutdown called - Counter at: " << counter << std::endl;
    }

    std::unique_ptr<IDataNode> getState() override {
        std::cout << "[TestModule] getState() - Saving counter: " << counter << std::endl;

        nlohmann::json state = {
            {"counter", counter},
            {"version", moduleVersion}
        };

        return std::make_unique<JsonDataNode>("state", state);
    }

    void setState(const IDataNode& state) override {
        counter = state.getInt("counter", 0);
        std::cout << "[TestModule] setState() - Restored counter: " << counter << std::endl;

        std::string oldVersion = state.getString("version", "unknown");
        std::cout << "[TestModule] setState() - Previous version was: " << oldVersion << std::endl;
    }

    std::string getType() const override {
        return "TestModule";
    }
};

} // namespace grove

// Module factory function - required for dynamic loading
extern "C" {
    grove::IModule* createModule() {
        return new grove::TestModule();
    }

    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
