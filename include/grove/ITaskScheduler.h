#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace warfactory {

/**
 * @brief Task scheduling interface for module delegation to execution system
 *
 * ITaskScheduler allows modules to delegate computationally expensive or
 * time-consuming tasks to the underlying execution system without knowing
 * the implementation details (sequential, threaded, thread pool, cluster).
 *
 * CORE PURPOSE:
 * - Modules stay lightweight (200-300 lines) by delegating heavy work
 * - Execution strategy determined by IModuleSystem implementation
 * - Modules remain thread-agnostic and infrastructure-free
 *
 * USAGE PATTERNS:
 * - ProductionModule: Delegate belt pathfinding calculations
 * - TankModule: Delegate A* pathfinding for unit movement
 * - EconomyModule: Delegate market analysis and price calculations
 * - FactoryModule: Delegate assembly line optimization
 *
 * EXECUTION STRATEGIES:
 * - SequentialModuleSystem: Tasks executed immediately in same thread
 * - ThreadedModuleSystem: Tasks executed in dedicated module thread
 * - MultithreadedModuleSystem: Tasks distributed across thread pool
 * - ClusterModuleSystem: Tasks distributed across remote workers
 *
 * PERFORMANCE BENEFIT:
 * - Modules process() methods stay fast (< 1ms for 60Hz modules)
 * - Heavy computation moved to background without blocking game loop
 * - Automatic scaling based on IModuleSystem implementation
 */
class ITaskScheduler {
public:
    virtual ~ITaskScheduler() = default;

    /**
     * @brief Schedule a task for execution
     * @param taskType Type of task (e.g., "pathfinding", "market_analysis", "belt_optimization")
     * @param taskData JSON data for the task
     *
     * Example usage:
     * ```cpp
     * // TankModule delegates pathfinding
     * scheduler->scheduleTask("pathfinding", {
     *     {"start", {x: 100, y: 200}},
     *     {"target", {x: 500, y: 600}},
     *     {"unit_id", "tank_001"}
     * });
     *
     * // ProductionModule delegates belt calculation
     * scheduler->scheduleTask("belt_optimization", {
     *     {"factory_id", "main_base"},
     *     {"item_type", "iron_plate"},
     *     {"throughput_target", 240}
     * });
     * ```
     */
    virtual void scheduleTask(const std::string& taskType, const json& taskData) = 0;

    /**
     * @brief Check if completed tasks are available
     * @return Number of completed tasks ready to be pulled
     *
     * Modules should check this before calling getCompletedTask()
     * to avoid blocking or polling unnecessarily.
     */
    virtual int hasCompletedTasks() const = 0;

    /**
     * @brief Pull and consume one completed task
     * @return Task result JSON. Task is removed from completed queue.
     *
     * Example results:
     * ```cpp
     * // Pathfinding result
     * {
     *     "task_type": "pathfinding",
     *     "unit_id": "tank_001",
     *     "path": [{"x": 100, "y": 200}, {"x": 150, "y": 250}, ...],
     *     "cost": 42.5
     * }
     *
     * // Belt optimization result
     * {
     *     "task_type": "belt_optimization",
     *     "factory_id": "main_base",
     *     "optimal_layout": [...],
     *     "efficiency_gain": 0.15
     * }
     * ```
     */
    virtual json getCompletedTask() = 0;
};

} // namespace warfactory