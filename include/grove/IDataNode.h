#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "IDataValue.h"

namespace warfactory {

/**
 * @brief Interface for a single node in the data tree
 *
 * Each node can have:
 * - Children nodes (tree navigation)
 * - Its own data blob (IDataValue)
 * - Properties accessible by name with type safety
 */
class IDataNode {
public:
    virtual ~IDataNode() = default;

    // ========================================
    // TREE NAVIGATION
    // ========================================

    /**
     * @brief Get direct child by name
     * @param name Exact name of the child
     * @return Child node or nullptr if not found
     */
    virtual std::unique_ptr<IDataNode> getChild(const std::string& name) = 0;

    /**
     * @brief Get names of all direct children
     * @return Vector of child names
     */
    virtual std::vector<std::string> getChildNames() = 0;

    /**
     * @brief Check if this node has any children
     * @return true if children exist
     */
    virtual bool hasChildren() = 0;

    // ========================================
    // EXACT SEARCH IN CHILDREN
    // ========================================

    /**
     * @brief Find all children with exact name (direct children only)
     * @param name Exact name to search for
     * @return Vector of matching child nodes
     */
    virtual std::vector<IDataNode*> getChildrenByName(const std::string& name) = 0;

    /**
     * @brief Check if any children have the exact name
     * @param name Exact name to search for
     * @return true if found
     */
    virtual bool hasChildrenByName(const std::string& name) const = 0;

    /**
     * @brief Get first child with exact name
     * @param name Exact name to search for
     * @return First matching child or nullptr
     */
    virtual IDataNode* getFirstChildByName(const std::string& name) = 0;

    // ========================================
    // PATTERN MATCHING SEARCH (DEEP SEARCH IN WHOLE SUBTREE)
    // ========================================

    /**
     * @brief Find all nodes in subtree matching pattern
     * @param pattern Pattern with wildcards (* supported)
     * @return Vector of matching nodes in entire subtree
     *
     * Examples:
     * - "component*" matches "component_armor", "component_engine"
     * - "*heavy*" matches "tank_heavy_mk1", "artillery_heavy"
     * - "model_*" matches "model_01", "model_02"
     */
    virtual std::vector<IDataNode*> getChildrenByNameMatch(const std::string& pattern) = 0;

    /**
     * @brief Check if any nodes in subtree match pattern
     * @param pattern Pattern with wildcards
     * @return true if any matches found
     */
    virtual bool hasChildrenByNameMatch(const std::string& pattern) const = 0;

    /**
     * @brief Get first node in subtree matching pattern
     * @param pattern Pattern with wildcards
     * @return First matching node or nullptr
     */
    virtual IDataNode* getFirstChildByNameMatch(const std::string& pattern) = 0;

    // ========================================
    // QUERY BY PROPERTIES
    // ========================================

    /**
     * @brief Query nodes in subtree by property value
     * @param propName Property name to check
     * @param predicate Function to test property value
     * @return Vector of nodes where predicate returns true
     *
     * Example:
     * // Find all tanks with armor > 150
     * queryByProperty("armor", [](const IDataValue& val) {
     *     return val.isNumber() && val.asInt() > 150;
     * });
     */
    virtual std::vector<IDataNode*> queryByProperty(const std::string& propName,
                                                   const std::function<bool(const IDataValue&)>& predicate) = 0;

    // ========================================
    // NODE'S OWN DATA
    // ========================================

    /**
     * @brief Get this node's data blob
     * @return Data value or null if no data
     */
    virtual std::unique_ptr<IDataValue> getData() const = 0;

    /**
     * @brief Check if this node has data
     * @return true if data exists
     */
    virtual bool hasData() const = 0;

    /**
     * @brief Set this node's data
     * @param data Data to set
     */
    virtual void setData(std::unique_ptr<IDataValue> data) = 0;

    // ========================================
    // TYPED DATA ACCESS BY PROPERTY NAME
    // ========================================

    /**
     * @brief Get string property from this node's data
     * @param name Property name
     * @param defaultValue Default if property not found or wrong type
     * @return Property value or default
     */
    virtual std::string getString(const std::string& name, const std::string& defaultValue = "") const = 0;

    /**
     * @brief Get integer property from this node's data
     * @param name Property name
     * @param defaultValue Default if property not found or wrong type
     * @return Property value or default
     */
    virtual int getInt(const std::string& name, int defaultValue = 0) const = 0;

    /**
     * @brief Get double property from this node's data
     * @param name Property name
     * @param defaultValue Default if property not found or wrong type
     * @return Property value or default
     */
    virtual double getDouble(const std::string& name, double defaultValue = 0.0) const = 0;

    /**
     * @brief Get boolean property from this node's data
     * @param name Property name
     * @param defaultValue Default if property not found or wrong type
     * @return Property value or default
     */
    virtual bool getBool(const std::string& name, bool defaultValue = false) const = 0;

    /**
     * @brief Check if property exists in this node's data
     * @param name Property name
     * @return true if property exists
     */
    virtual bool hasProperty(const std::string& name) const = 0;

    // ========================================
    // HASH SYSTEM FOR VALIDATION & SYNCHRO
    // ========================================

    /**
     * @brief Get hash of this node's data only
     * @return SHA256 hash of data blob
     */
    virtual std::string getDataHash() = 0;

    /**
     * @brief Get recursive hash of this node and all children
     * @return SHA256 hash of entire subtree
     */
    virtual std::string getTreeHash() = 0;

    /**
     * @brief Get hash of specific child subtree
     * @param childPath Path to child from this node
     * @return SHA256 hash of child subtree
     */
    virtual std::string getSubtreeHash(const std::string& childPath) = 0;

    // ========================================
    // METADATA
    // ========================================

    /**
     * @brief Get full path from root to this node
     * @return Path string (e.g., "vehicles/tanks/heavy/model5")
     */
    virtual std::string getPath() const = 0;

    /**
     * @brief Get this node's name
     * @return Node name
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Get node type (extensible for templates/inheritance later)
     * @return Node type identifier
     */
    virtual std::string getNodeType() const = 0;
};

} // namespace warfactory