#pragma once

#include <string>
#include <memory>
#include <functional>
#include "IDataNode.h"

namespace grove {

/**
 * @brief Interface for the root data tree container
 *
 * Unified system for configuration, persistent data, and runtime state.
 * Supports hot-reload for config and persistence for data.
 *
 * Tree Structure:
 * - config/   : Read-only game configuration (hot-reload enabled, moddable)
 * - data/     : Persistent player data (read-write, saved to disk)
 * - runtime/  : Temporary runtime state (read-write, never saved)
 */
class IDataTree {
public:
    virtual ~IDataTree() = default;

    // ========================================
    // TREE ACCESS
    // ========================================

    /**
     * @brief Get root node of the entire tree
     * @return Root node containing config/, data/, runtime/
     *
     * WARNING: This gives access to everything. Use getConfigRoot(),
     * getDataRoot(), or getRuntimeRoot() for isolated access.
     */
    virtual std::unique_ptr<IDataNode> getRoot() = 0;

    /**
     * @brief Get node by path from root
     * @param path Path from root (e.g., "config/vehicles/tanks/heavy")
     * @return Node at path or nullptr if not found
     */
    virtual std::unique_ptr<IDataNode> getNode(const std::string& path) = 0;

    // ========================================
    // SEPARATE ROOTS (Recommended Access Pattern)
    // ========================================

    /**
     * @brief Get config tree root (read-only, hot-reload enabled)
     * @return Config root node (config/)
     *
     * Use for: Game configuration, unit stats, modding
     */
    virtual std::unique_ptr<IDataNode> getConfigRoot() = 0;

    /**
     * @brief Get persistent data root (read-write, saved to disk)
     * @return Data root node (data/)
     *
     * Use for: Campaign progress, unlocks, player statistics
     */
    virtual std::unique_ptr<IDataNode> getDataRoot() = 0;

    /**
     * @brief Get runtime data root (read-write, never saved)
     * @return Runtime root node (runtime/)
     *
     * Use for: Current game state, temporary calculations, caches
     */
    virtual std::unique_ptr<IDataNode> getRuntimeRoot() = 0;

    // ========================================
    // SAVE OPERATIONS
    // ========================================

    /**
     * @brief Save all persistent data to disk
     * @return true if save succeeded
     *
     * Saves the entire data/ subtree to disk. Does not affect config/ or runtime/.
     */
    virtual bool saveData() = 0;

    /**
     * @brief Save specific node and its subtree
     * @param path Path to node to save (e.g., "data/campaign/progress")
     * @return true if save succeeded
     *
     * Allows granular saves for performance. Only works for data/ paths.
     */
    virtual bool saveNode(const std::string& path) = 0;

    // ========================================
    // HOT-RELOAD (Config Only)
    // ========================================

    /**
     * @brief Check if config files have changed
     * @return true if changes detected in config/
     */
    virtual bool checkForChanges() = 0;

    /**
     * @brief Reload config tree if files changed
     * @return true if reload was performed
     *
     * Only reloads config/. Does not affect data/ or runtime/.
     */
    virtual bool reloadIfChanged() = 0;

    /**
     * @brief Register callback for when config is reloaded
     * @param callback Function called after successful config reload
     */
    virtual void onTreeReloaded(std::function<void()> callback) = 0;

    // ========================================
    // METADATA
    // ========================================

    /**
     * @brief Get tree implementation type
     * @return Type identifier (e.g., "JSONDataTree", "DatabaseDataTree")
     */
    virtual std::string getType() = 0;
};

} // namespace grove