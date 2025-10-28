#pragma once

#include <string>
#include <memory>
#include "ITaskScheduler.h"

// Forward declarations to avoid circular dependencies
namespace warfactory {
    class IModule;
    class IIO;
}

namespace warfactory {

enum class ModuleSystemType {
    SEQUENTIAL = 0,
    THREADED = 1,
    THREAD_POOL = 2,
    CLUSTER = 3
};


/**
 * @brief Module execution strategy interface - swappable performance architecture
 *
 * The module system manages module lifecycle and execution strategy.
 * Different implementations provide different performance characteristics:
 *
 * - SequentialModuleSystem: Debug/test mode, processes modules one at a time
 * - ThreadedModuleSystem: Each module in its own thread
 * - MultithreadedModuleSystem: Module tasks distributed across thread pool
 * - ClusterModuleSystem: Modules distributed across multiple machines
 *
 * This enables progressive evolution from debug to production to MMO scale
 * without changing any module business logic code.
 *
 * Inherits from ITaskScheduler to provide task delegation capabilities.
 */
class IModuleSystem : public ITaskScheduler {
public:
    virtual ~IModuleSystem() = default;

    /**
     * @brief Register a module with the system
     * @param name Unique identifier for the module
     * @param module Module implementation (unique ownership)
     *
     * The module system takes ownership of the module and manages its lifecycle.
     * Modules can be registered at any time and will participate in the next
     * processing cycle.
     */
    virtual void registerModule(const std::string& name, std::unique_ptr<IModule> module) = 0;

    /**
     * @brief Process all registered modules
     * @param deltaTime Time elapsed since last processing cycle in seconds
     *
     * This is the core execution method that coordinates all modules according
     * to the implemented strategy. Each module's process() method will be called
     * with appropriate timing and coordination.
     */
    virtual void processModules(float deltaTime) = 0;

    /**
     * @brief Set the IO layer for inter-module communication
     * @param ioLayer Communication transport implementation (unique ownership)
     *
     * The module system takes ownership of the IO layer and uses it to
     * facilitate communication between modules.
     */
    virtual void setIOLayer(std::unique_ptr<IIO> ioLayer) = 0;

    /**
     * @brief Query a specific module directly
     * @param name Name of the module to query
     * @param input Input data to send to the module
     * @return Response data from the module
     *
     * This provides direct access to module functionality for debugging,
     * testing, or administrative purposes. The query bypasses normal
     * execution flow and calls the module's process() method directly.
     */
    virtual std::unique_ptr<IDataNode> queryModule(const std::string& name, const IDataNode& input) = 0;

    /**
     * @brief Get module system type identifier
     * @return Module system type enum value for identification
     */
    virtual ModuleSystemType getType() const = 0;
};

} // namespace warfactory