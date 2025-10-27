#pragma once

#include <string>
#include <memory>
#include <functional>
#include "IDataNode.h"

namespace warfactory {

/**
 * @brief Interface for the root data tree container
 *
 * Manages the entire tree structure and provides hot-reload capabilities
 */
class IDataTree {
public:
    virtual ~IDataTree() = default;

    // ========================================
    // TREE ACCESS
    // ========================================

    /**
     * @brief Get root node of the tree
     * @return Root node
     */
    virtual std::unique_ptr<IDataNode> getRoot() = 0;

    /**
     * @brief Get node by path from root
     * @param path Path from root (e.g., "vehicles/tanks/heavy")
     * @return Node at path or nullptr if not found
     */
    virtual std::unique_ptr<IDataNode> getNode(const std::string& path) = 0;

    // ========================================
    // MANUAL HOT-RELOAD (SIMPLE & EFFECTIVE)
    // ========================================

    /**
     * @brief Check if source files have changed
     * @return true if changes detected
     */
    virtual bool checkForChanges() = 0;

    /**
     * @brief Reload entire tree if changes detected
     * @return true if reload was performed
     */
    virtual bool reloadIfChanged() = 0;

    /**
     * @brief Register callback for when tree is reloaded
     * @param callback Function called after successful reload
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

} // namespace warfactory