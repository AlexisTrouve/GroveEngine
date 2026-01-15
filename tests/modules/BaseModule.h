#pragma once
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include <memory>
#include <atomic>
#include <spdlog/spdlog.h>

namespace grove {

/**
 * @brief Base module with no dependencies - provides services to other modules
 *
 * This module serves as a dependency for DependentModule in the Module Dependencies test.
 * It exposes a simple service (generateNumber) that other modules can use.
 *
 * Version changes:
 * - v1: generateNumber() returns 42
 * - v2: generateNumber() returns 100
 */
class BaseModule : public IModule {
public:
    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "BaseModule"; }
    bool isIdle() const override { return true; }

    // Dependency API
    std::vector<std::string> getDependencies() const override {
        return {};  // No dependencies
    }

    int getVersion() const override { return version_; }

    // Service exposed to other modules
    int generateNumber() const;

private:
    int version_ = 1;
    std::atomic<int> processCount_{0};
    int generatedValue_ = 42;  // V1: 42, V2: 100
    std::unique_ptr<IDataNode> configNode_;
    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
