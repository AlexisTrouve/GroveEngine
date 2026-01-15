#pragma once
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include "BaseModule.h"
#include <memory>
#include <vector>
#include <spdlog/spdlog.h>

namespace grove {

/**
 * @brief Module that depends on BaseModule
 *
 * This module declares an explicit dependency on BaseModule and uses its services.
 * When BaseModule is reloaded, this module should be cascaded reloaded automatically.
 *
 * The module collects numbers from BaseModule and accumulates them.
 */
class DependentModule : public IModule {
public:
    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "DependentModule"; }
    bool isIdle() const override { return true; }

    // Dependency API
    std::vector<std::string> getDependencies() const override {
        return {"BaseModule"};  // Explicit dependency on BaseModule
    }

    int getVersion() const override { return version_; }

    // Setter for dependency injection
    void setBaseModule(BaseModule* baseModule) {
        baseModule_ = baseModule;
    }

    // Get collected numbers (for testing)
    const std::vector<int>& getCollectedNumbers() const {
        return collectedNumbers_;
    }

private:
    int version_ = 1;
    BaseModule* baseModule_ = nullptr;
    std::vector<int> collectedNumbers_;  // Accumulate values from BaseModule
    int processCount_ = 0;
    std::unique_ptr<IDataNode> configNode_;
    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
