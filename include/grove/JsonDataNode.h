#pragma once

#include "IDataNode.h"
#include "JsonDataValue.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>

namespace grove {

using json = nlohmann::json;

/**
 * @brief Concrete implementation of IDataNode backed by JSON
 *
 * Represents a node in the hierarchical data tree. Can have:
 * - Children nodes (map of name -> node)
 * - Own data (JSON value)
 * - Path in the tree for identification
 */
class JsonDataNode : public IDataNode {
public:
    /**
     * @brief Create a node with name and optional data
     * @param name Node name
     * @param data Optional JSON data for this node
     * @param parent Optional parent node (for path tracking)
     * @param readOnly Whether this node is read-only (for config/)
     */
    JsonDataNode(const std::string& name,
                 const json& data = json::object(),
                 JsonDataNode* parent = nullptr,
                 bool readOnly = false);

    virtual ~JsonDataNode() = default;

    // Tree navigation
    std::unique_ptr<IDataNode> getChild(const std::string& name) override;
    IDataNode* getChildReadOnly(const std::string& name) override;
    std::vector<std::string> getChildNames() override;
    bool hasChildren() override;
    bool hasChild(const std::string& name) const override;

    // Exact search in children
    std::vector<IDataNode*> getChildrenByName(const std::string& name) override;
    bool hasChildrenByName(const std::string& name) const override;
    IDataNode* getFirstChildByName(const std::string& name) override;

    // Pattern matching search
    std::vector<IDataNode*> getChildrenByNameMatch(const std::string& pattern) override;
    bool hasChildrenByNameMatch(const std::string& pattern) const override;
    IDataNode* getFirstChildByNameMatch(const std::string& pattern) override;

    // Query by properties
    std::vector<IDataNode*> queryByProperty(const std::string& propName,
                                           const std::function<bool(const IDataValue&)>& predicate) override;

    // Node's own data
    std::unique_ptr<IDataValue> getData() const override;
    bool hasData() const override;
    void setData(std::unique_ptr<IDataValue> data) override;

    // Typed data access
    std::string getString(const std::string& name, const std::string& defaultValue = "") const override;
    int getInt(const std::string& name, int defaultValue = 0) const override;
    double getDouble(const std::string& name, double defaultValue = 0.0) const override;
    bool getBool(const std::string& name, bool defaultValue = false) const override;
    bool hasProperty(const std::string& name) const override;

    // Typed data modification
    void setString(const std::string& name, const std::string& value) override;
    void setInt(const std::string& name, int value) override;
    void setDouble(const std::string& name, double value) override;
    void setBool(const std::string& name, bool value) override;

    // Hash system
    std::string getDataHash() override;
    std::string getTreeHash() override;
    std::string getSubtreeHash(const std::string& childPath) override;

    // Metadata
    std::string getPath() const override;
    std::string getName() const override;
    std::string getNodeType() const override;

    // Tree modification
    void setChild(const std::string& name, std::unique_ptr<IDataNode> node) override;
    bool removeChild(const std::string& name) override;
    void clearChildren() override;

    // Direct JSON access (for internal use by JsonDataTree)
    const json& getJsonData() const { return m_data; }
    json& getJsonData() { return m_data; }
    const std::map<std::string, std::unique_ptr<JsonDataNode>>& getChildren() const { return m_children; }

private:
    std::string m_name;
    json m_data;
    JsonDataNode* m_parent;
    bool m_readOnly;
    std::map<std::string, std::unique_ptr<JsonDataNode>> m_children;

    // Helper methods
    bool matchesPattern(const std::string& text, const std::string& pattern) const;
    void collectMatchingNodes(const std::string& pattern, std::vector<IDataNode*>& results);
    void collectNodesByProperty(const std::string& propName,
                                const std::function<bool(const IDataValue&)>& predicate,
                                std::vector<IDataNode*>& results);
    std::string computeHash(const std::string& input) const;
    void checkReadOnly() const;
};

} // namespace grove
