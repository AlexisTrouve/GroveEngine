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
 * This interface defines the contract for all game modules. Each module is a major
 * SUBSYSTEM (e.g. an AI system, the RTS simulation, the colony system, a renderer)
 * containing pure game logic with zero infrastructure code. A module can be LARGE and
 * hold many classes/files internally — that is intended.
 *
 * Key design principles:
 * - PURE FUNCTION: process() method has minimal side effects
 * - CONFIG VIA IDATANODE: Configuration via immutable IDataNode references
 * - IDATANODE COMMUNICATION: All data via IDataNode abstraction (backend agnostic)
 * - IIO FOR PERSISTENCE: Save requests via IIO publish (Engine handles persistence)
 * - NO INFRASTRUCTURE: No threading, networking, or framework dependencies
 * - HOT-RELOAD READY: State serialization for seamless module replacement
 * - ISOLATED & TESTABLE: a primary benefit — a module runs IN ISOLATION. Instantiate
 *   it alone, feed it input topics via a test IIO, call process(), and assert the
 *   topics it publishes (its IIO contract). No engine, no other modules needed.
 * - CLAUDE OPTIMIZED: Subsystem granularity for AI development efficiency
 *
 * DATA FLOW:
 * - Configuration: Read-only via setConfiguration(const IDataNode&)
 * - Input: Read-only via process(const IDataNode&)
 * - Save: Publish via IIO: m_io->publish("save:module:state", data)
 * - State: Serialized via getState() for hot-reload
 *
 * Module granularity: one major subsystem per module — by subsystem / responsibility,
 * NOT by line count. Split a module only when it mixes TWO distinct subsystems, never
 * because it exceeds N lines. Modules communicate only via IIO pub/sub.
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

    /**
     * @brief Update module configuration at runtime (config hot-reload)
     * @param newConfigNode New configuration to apply
     * @return True if configuration was successfully applied, false if rejected
     *
     * This method enables runtime configuration changes without code reload.
     * Unlike setConfiguration(), this is called on an already-initialized module.
     *
     * Implementation should:
     * - Validate new configuration
     * - Reject invalid configurations and return false
     * - Preserve previous config for rollback if needed
     * - Apply changes atomically if possible
     *
     * Default implementation rejects all updates (returns false).
     * Modules that support config hot-reload must override this method.
     */
    virtual bool updateConfig(const IDataNode& newConfigNode) {
        // Default: reject config updates
        return false;
    }

    /**
     * @brief Update module configuration partially (merge with current config)
     * @param partialConfigNode Configuration fragment to merge
     * @return True if configuration was successfully merged and applied
     *
     * This method enables partial configuration updates where only specified
     * fields are changed while others remain unchanged.
     *
     * Implementation should:
     * - Merge partial config with current config
     * - Validate merged result
     * - Apply atomically
     *
     * Default implementation delegates to updateConfig() (full replacement).
     * Modules can override for smarter partial merging.
     */
    virtual bool updateConfigPartial(const IDataNode& partialConfigNode) {
        // Default: delegate to full update
        return updateConfig(partialConfigNode);
    }

    /**
     * @brief Get list of module dependencies
     * @return Vector of module names that this module depends on
     *
     * Declares explicit dependencies on other modules. The module system
     * uses this to:
     * - Verify dependencies are loaded before this module
     * - Cascade reload when a dependency is reloaded
     * - Prevent unloading dependencies while this module is active
     * - Detect circular dependencies
     *
     * Dependencies are specified by module name (not file path).
     *
     * Example:
     * - PhysicsModule might return {"MathModule"}
     * - GameplayModule might return {"PhysicsModule", "AudioModule"}
     *
     * Default implementation returns empty vector (no dependencies).
     */
    virtual std::vector<std::string> getDependencies() const {
        return {};  // Default: no dependencies
    }

    /**
     * @brief Get module version number
     * @return Integer version number (increments with each reload)
     *
     * Used for tracking and debugging hot-reload behavior.
     * Helps verify that modules are actually being reloaded and
     * allows tracking version mismatches.
     *
     * Typically incremented manually during development or
     * auto-generated during build process.
     *
     * Default implementation returns 1.
     */
    virtual int getVersion() const {
        return 1;  // Default: version 1
    }

    /**
     * @brief Migrate state from a different version of this module
     * @param fromVersion Version number of the source module
     * @param oldState State data from the previous version
     * @return True if migration was successful, false if incompatible
     *
     * Enables multi-version coexistence by allowing state migration
     * between different versions of the same module. Critical for:
     * - Canary deployments (v1 → v2 progressive migration)
     * - Blue/Green deployments (switch traffic between versions)
     * - Rollback scenarios (v2 → v1 state restoration)
     *
     * Implementation should:
     * - Check if migration from fromVersion is supported
     * - Transform old state format to new format
     * - Handle missing/new fields gracefully
     * - Validate migrated state
     * - Return false if migration is impossible
     *
     * Example:
     * - v2 can migrate from v1 by adding default collision flags
     * - v3 can migrate from v2 by initializing physics parameters
     * - v1 cannot migrate from v2 (missing fields) → return false
     *
     * Default implementation accepts same version only (simple copy).
     */
    virtual bool migrateStateFrom(int fromVersion, const IDataNode& oldState) {
        // Default: only accept same version (simple setState)
        if (fromVersion == getVersion()) {
            setState(oldState);
            return true;
        }
        return false;  // Override for cross-version migration
    }
};

} // namespace grove