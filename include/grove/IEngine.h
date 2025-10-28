#pragma once

#include <string>
#include <memory>

// Forward declarations to avoid circular dependencies
namespace warfactory {
    class IModuleSystem;
    class IIO;
}

namespace warfactory {

enum class EngineType {
    DEBUG = 0,
    PRODUCTION = 1,
    HIGH_PERFORMANCE = 2
};

/**
 * @brief Engine orchestration interface - coordinates the entire system
 *
 * The engine is responsible for:
 * - System initialization and lifecycle management
 * - Main game loop coordination with delta time updates
 * - Module system orchestration
 * - IIO health monitoring and backpressure management
 *
 * IMPORTANT: Engine implementations must periodically check IIO health:
 * - Monitor IOHealth.queueSize vs maxQueueSize (warn at 80% full)
 * - Track IOHealth.dropping status (critical - consider module restart)
 * - Log IOHealth.droppedMessageCount for debugging
 * - Monitor IOHealth.averageProcessingRate for performance analysis
 *
 * Evolution path:
 * - DebugEngine: Development/testing with step-by-step execution
 * - HighPerfEngine: Production optimized with threading
 * - DataOrientedEngine: Massive scale with SIMD and clustering
 */
class IEngine {
public:
    virtual ~IEngine() = default;

    /**
     * @brief Initialize engine systems
     *
     * Sets up the engine with basic configuration.
     * Module system and other components are set separately.
     */
    virtual void initialize() = 0;

    /**
     * @brief Start main game loop
     *
     * Blocks until shutdown() called. Engine owns the main loop and handles:
     * - Frame timing and delta time calculation
     * - Module system coordination
     * - Performance management and frame rate control
     */
    virtual void run() = 0;

    /**
     * @brief Process single frame/tick (for debugging)
     * @param deltaTime Time elapsed since last update in seconds
     *
     * For step debugging and testing. Processes one iteration
     * without entering the main loop.
     */
    virtual void step(float deltaTime) = 0;

    /**
     * @brief Shutdown engine and cleanup all resources
     *
     * Ensures proper cleanup of all systems in correct order.
     * Should be safe to call multiple times. Stops run() loop.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Load modules from configuration
     * @param configPath Path to module configuration file
     *
     * Engine automatically:
     * - Loads modules from .so/.dll files
     * - Creates appropriate ModuleSystem for each module (performance strategy)
     * - Configures execution frequency and coordination
     *
     * Config format:
     * {
     *   "modules": [
     *     {"path": "tank.so", "strategy": "threaded", "frequency": "60hz"},
     *     {"path": "economy.so", "strategy": "sequential", "frequency": "0.1hz"}
     *   ]
     * }
     */
    virtual void loadModules(const std::string& configPath) = 0;

    /**
     * @brief Register main coordinator socket
     * @param coordinatorSocket Socket for system coordination communication
     *
     * Engine uses this socket for high-level system coordination,
     * health monitoring, and administrative commands.
     */
    virtual void registerMainSocket(std::unique_ptr<IIO> coordinatorSocket) = 0;

    /**
     * @brief Register new client/player socket
     * @param clientSocket Socket for player communication
     *
     * Engine manages player connections as a priority channel.
     * Players are the most important external connections.
     */
    virtual void registerNewClientSocket(std::unique_ptr<IIO> clientSocket) = 0;

    /**
     * @brief Get engine type identifier
     * @return Engine type enum value for identification
     */
    virtual EngineType getType() const = 0;
};

} // namespace warfactory