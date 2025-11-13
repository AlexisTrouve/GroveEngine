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
 * - PURE FUNCTION: process() method has minimal side effects
 * - CONFIG VIA IDATANODE: Configuration via immutable IDataNode references
 * - IDATANODE COMMUNICATION: All data via IDataNode abstraction (backend agnostic)
 * - IIO FOR PERSISTENCE: Save requests via IIO publish (Engine handles persistence)
 * - NO INFRASTRUCTURE: No threading, networking, or framework dependencies
 * - HOT-RELOAD READY: State serialization for seamless module replacement
 * - CLAUDE OPTIMIZED: Micro-context size for AI development efficiency
 *
 * DATA FLOW:
 * - Configuration: Read-only via setConfiguration(const IDataNode&)
 * - Input: Read-only via process(const IDataNode&)
 * - Save: Publish via IIO: m_io->publish("save:module:state", data)
 * - State: Serialized via getState() for hot-reload
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

    /**
     * @brief Check if module is idle (no processing in progress)
     * @return True if module has no active processing and can be safely hot-reloaded
     *
     * Used by hot-reload system to ensure safe reload timing.
     * A module is considered idle when:
     * - No synchronous processing in progress
     * - Not waiting for critical state updates
     * - Safe to extract state via getState()
     *
     * Note: Async tasks scheduled via ITaskScheduler are tracked separately
     * by the module system and don't affect idle status.
     *
     * Default implementation should return true unless module explicitly
     * tracks long-running synchronous operations.
     */
    virtual bool isIdle() const = 0;
};

} // namespace grove