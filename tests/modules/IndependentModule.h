#pragma once
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include <memory>
#include <atomic>
#include <spdlog/spdlog.h>

namespace grove {

/**
 * @brief Independent module with no dependencies - serves as a witness/control
 *
 * This module is completely isolated from BaseModule and DependentModule.
 * It should NEVER be reloaded when other modules are reloaded (unless explicitly targeted).
 * Used to verify that cascade reloads don't affect unrelated modules.
 */
class IndependentModule : public IModule {
public:
    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override { return "IndependentModule"; }
    bool isIdle() const override { return true; }

    // Dependency API
    std::vector<std::string> getDependencies() const override {
        return {};  // No dependencies - completely isolated
    }

    int getVersion() const override { return version_; }

private:
    int version_ = 1;
    std::atomic<int> processCount_{0};
    std::atomic<int> reloadCount_{0};  // Track how many times setState is called
    std::unique_ptr<IDataNode> configNode_;
    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
