#pragma once
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include <memory>
#include <spdlog/spdlog.h>

namespace grove {

/**
 * ErrorRecoveryModule - Module de test pour validation du système de recovery
 *
 * Contrairement au ChaosModule (aléatoire), ce module permet de déclencher
 * des crashes de manière CONTRÔLÉE via sa configuration :
 *
 * - crashAtFrame: Frame spécifique où crasher
 * - crashType: Type de crash (runtime_error, logic_error, etc.)
 * - enableAutoRecovery: Si true, le module peut se "guérir" après reload
 * - versionTag: Tag de version pour valider hot-reload
 */
class ErrorRecoveryModule : public IModule {
public:
    // IModule interface
    void process(const IDataNode& input) override;
    void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) override;
    const IDataNode& getConfiguration() override;
    std::unique_ptr<IDataNode> getHealthStatus() override;
    void shutdown() override;
    std::unique_ptr<IDataNode> getState() override;
    void setState(const IDataNode& state) override;
    std::string getType() const override;
    bool isIdle() const override { return !isProcessing; }

private:
    // État du module
    int frameCount = 0;
    int crashCount = 0;
    int recoveryCount = 0;
    bool isProcessing = false;
    bool hasCrashed = false;

    // Configuration
    int crashAtFrame = -1;        // -1 = pas de crash planifié
    int crashType = 0;            // 0=runtime_error, 1=logic_error, 2=out_of_range, 3=segfault simulation
    bool enableAutoRecovery = true;
    std::string versionTag = "v1.0";

    std::shared_ptr<spdlog::logger> logger;
    std::unique_ptr<IDataNode> config;

    // Déclenche le crash configuré
    void triggerConfiguredCrash();
};

} // namespace grove

// Export symbols
extern "C" {
    grove::IModule* createModule();
    void destroyModule(grove::IModule* module);
}
