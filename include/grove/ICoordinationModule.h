#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include "IModule.h"
#include "IDataTree.h"

// Forward declarations
namespace grove {
    class IEngine;
    class IModuleSystem;
}

namespace grove {

/**
 * @brief Global system orchestrator - First launched, last shutdown
 *
 * The CoordinationModule is the main system orchestrator that manages the entire
 * game system lifecycle, module deployment topology, and configuration synchronization.
 *
 * ARCHITECTURE FLOW:
 * 1. MainServer launches CoordinationModule (first module)
 * 2. CoordinationModule loads gameconfig.json via IDataTree
 * 3. Parses deployment section to determine module topology
 * 4. Deploys modules to local IEngine or remote servers
 * 5. Synchronizes configuration across all deployed modules
 * 6. Coordinates shutdown (last module to close)
 *
 * DESIGN DECISIONS:
 * - No state persistence: behavior driven entirely by gameconfig.json
 * - No network protocol: all communication via IIO abstraction
 * - No security for now: local/trusted environment assumed
 * - Module deployment via IModuleSystem delegation
 * - Configuration immutability via const IDataNode references
 */
class ICoordinationModule : public IModule {
public:
    virtual ~ICoordinationModule() = default;

    // ========================================
    // GAME LIFECYCLE MANAGEMENT
    // ========================================

    /**
     * @brief Start new game session with configuration
     * @param gameConfigPath Path to gameconfig.json file
     *
     * Complete startup sequence:
     * 1. Load and parse gameconfig.json via IDataTree
     * 2. Initialize local IEngine and IModuleSystem
     * 3. Parse deployment topology from config
     * 4. Deploy local modules (target: "local")
     * 5. Launch remote servers and deploy remote modules
     * 6. Synchronize all configurations
     * 7. Return when system is ready
     */
    virtual void startNewGame(const std::string& gameConfigPath) = 0;

    /**
     * @brief Load existing game from save file
     * @param savePath Path to save file
     */
    virtual void loadGame(const std::string& savePath) = 0;

    /**
     * @brief Shutdown entire game system gracefully
     *
     * Coordinates graceful shutdown:
     * 1. Signal all modules to save state
     * 2. Undeploy remote modules first
     * 3. Undeploy local modules
     * 4. Shutdown remote servers
     * 5. Shutdown local IEngine
     * 6. CoordinationModule shuts down last
     */
    virtual void shutdownGame() = 0;

    // ========================================
    // MODULE DEPLOYMENT TOPOLOGY
    // ========================================

    /**
     * @brief Deploy module according to gameconfig.json specification
     * @param moduleInstanceId Module instance ID as defined in config
     *
     * Deployment process:
     * 1. Read module config from gameconfig.json deployment section
     * 2. Determine target: "local" vs "server:IP" vs "cluster:name"
     * 3. Get module-specific configuration from modules section
     * 4. For local: delegate to local IEngine->IModuleSystem
     * 5. For remote: send deployment command to remote server
     * 6. Pass const IDataNode& configuration to module
     */
    virtual void deployModule(const std::string& moduleInstanceId) = 0;

    /**
     * @brief Stop and undeploy module instance
     * @param moduleInstanceId Module instance ID to undeploy
     */
    virtual void undeployModule(const std::string& moduleInstanceId) = 0;

    /**
     * @brief Get list of currently deployed module instances
     * @return Vector of module instance IDs currently running
     */
    virtual std::vector<std::string> getDeployedModules() = 0;

    // ========================================
    // CONFIGURATION SYNCHRONIZATION
    // ========================================

    /**
     * @brief Synchronize configuration changes to all deployed modules
     *
     * Process:
     * 1. Reload gameconfig.json via IDataTree hot-reload
     * 2. For each deployed module, get updated configuration
     * 3. Call module->setConfiguration() with new const IDataNode&
     * 4. Handle any modules that fail to reconfigure
     */
    virtual void syncConfiguration() = 0;

    /**
     * @brief Set configuration tree for the coordination system
     * @param configTree Configuration data tree loaded from gameconfig.json
     */
    virtual void setConfigurationTree(std::unique_ptr<IDataTree> configTree) = 0;

    /**
     * @brief Get current configuration tree
     * @return Configuration tree pointer for accessing gameconfig.json data
     */
    virtual IDataTree* getConfigurationTree() = 0;

    // ========================================
    // SYSTEM HEALTH AND MANAGEMENT
    // ========================================

    /**
     * @brief Check if all deployed modules are healthy and responsive
     * @return true if system is healthy, false if issues detected
     *
     * Aggregates health status from all deployed modules:
     * - Calls getHealthStatus() on each module
     * - Checks network connectivity to remote servers
     * - Validates configuration consistency
     * - Could trigger auto-save in future versions
     */
    virtual bool isSystemHealthy() = 0;

    /**
     * @brief Get detailed system health report
     * @return JSON health report aggregating all module health status
     */
    virtual json getSystemHealthReport() = 0;
};

} // namespace grove