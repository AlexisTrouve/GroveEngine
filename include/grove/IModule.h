#pragma once

#include <string>
#include <memory>
#include "IDataNode.h"
#include "ITaskScheduler.h"

// Forward declarations
namespace grove {
    class IIO;
}

namespace grove {



/**
 * @brief Pure business logic interface - optimized for Claude Code development
 *
 * This interface defines the contract for all game modules. Each module contains
 * 200-300 lines of pure game logic with zero infrastructure code.
 *
 * Key design principles:
 * - PURE FUNCTION: process() method has no side effects beyond return value
 * - CONFIG VIA DATATREE: Configuration via immutable IDataNode references
 * - JSON ONLY: All communication via JSON input/output
 * - NO INFRASTRUCTURE: No threading, networking, or framework dependencies
 * - HOT-RELOAD READY: State serialization for seamless module replacement
 * - CLAUDE OPTIMIZED: Micro-context size for AI development efficiency
 *
 * BREAKING CHANGES:
 * - Removed initialize() method - use setConfiguration() instead
 * - Configuration via const IDataNode& for immutability
 * - Health check returns detailed JSON status
 *
 * Module constraint: Maximum 300 lines per module (Exception: ProductionModule 500-800 lines)
 */
class IModule {
public:
    virtual ~IModule() = default;

    /**
     * @brief Process game logic
     * @param input Data input from other modules or the module system
     *
     * This is the core method where all module logic is implemented.
     * Modules communicate via IIO pub/sub and can delegate tasks via ITaskScheduler.
     * Must handle state properly through getState/setState for hot-reload.
     */
    virtual void process(const IDataNode& input) = 0;

    /**
     * @brief Set module configuration (replaces initialize)
     * @param configNode Configuration node (immutable reference)
     * @param io Pub/sub communication interface for messaging
     * @param scheduler Task scheduling interface for delegating work
     *
     * Called when the module is loaded or configuration changes.
     * Should setup internal state, validate configuration, and store service references.
     */
    virtual void setConfiguration(const IDataNode& configNode, IIO* io, ITaskScheduler* scheduler) = 0;

    /**
     * @brief Get current module configuration
     * @return Configuration node reference
     */
    virtual const IDataNode& getConfiguration() = 0;

    /**
     * @brief Get detailed health status of the module
     * @return Health report with status, metrics, and diagnostics
     */
    virtual std::unique_ptr<IDataNode> getHealthStatus() = 0;

    /**
     * @brief Cleanup and shutdown the module
     *
     * Called when the module is being unloaded. Should clean up any
     * resources and prepare for safe destruction.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Get current module state for hot-reload support
     * @return Data representation of all module state
     *
     * Critical for hot-reload functionality. Must serialize all internal
     * state that needs to be preserved when the module is replaced.
     * The returned data should be sufficient to restore the module to
     * its current state via setState().
     */
    virtual std::unique_ptr<IDataNode> getState() = 0;

    /**
     * @brief Restore module state after hot-reload
     * @param state State previously returned by getState()
     *
     * Called after module replacement to restore the previous state.
     * Must be able to reconstruct all internal state from the data
     * to ensure seamless hot-reload without game disruption.
     */
    virtual void setState(const IDataNode& state) = 0;

    /**
     * @brief Get module type identifier
     * @return Module type as string (e.g., "tank", "economy", "production")
     */
    virtual std::string getType() const = 0;
};

} // namespace grove