#include "grove/JsonDataNode.h"
#include <regex>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <functional>

#if GROVE_USE_OPENSSL
#include <openssl/sha.h>
#else
// Simple fallback hash using std::hash - NOT cryptographically secure
// but sufficient for change detection in render graph
#endif

namespace grove {

JsonDataNode::JsonDataNode(const std::string& name, const json& data, JsonDataNode* parent, bool readOnly)
    : m_name(name), m_data(data), m_parent(parent), m_readOnly(readOnly) {
}

// ========================================
// TREE NAVIGATION
// ========================================

std::unique_ptr<IDataNode> JsonDataNode::getChild(const std::string& name) {
    auto it = m_children.find(name);
    if (it == m_children.end()) {
        return nullptr;
    }
    // Return a copy wrapped in unique_ptr
    return std::make_unique<JsonDataNode>(it->second->getName(),
                                          it->second->getJsonData(),
                                          this,
                                          it->second->m_readOnly);
}

IDataNode* JsonDataNode::getChildReadOnly(const std::string& name) {
    auto it = m_children.find(name);
    if (it == m_children.end()) {
        return nullptr;
    }
    // Return raw pointer without copying - valid as long as parent exists
    return it->second.get();
}

std::vector<std::string> JsonDataNode::getChildNames() {
    std::vector<std::string> names;
    names.reserve(m_children.size());
    for (const auto& [name, _] : m_children) {
        names.push_back(name);
    }
    return names;
}

bool JsonDataNode::hasChildren() {
    return !m_children.empty();
}

bool JsonDataNode::hasChild(const std::string& name) const {
    return m_children.find(name) != m_children.end();
}

// ========================================
// EXACT SEARCH IN CHILDREN
// ========================================

std::vector<IDataNode*> JsonDataNode::getChildrenByName(const std::string& name) {
    std::vector<IDataNode*> results;
    auto it = m_children.find(name);
    if (it != m_children.end()) {
        results.push_back(it->second.get());
    }
    return results;
}

bool JsonDataNode::hasChildrenByName(const std::string& name) const {
    return m_children.find(name) != m_children.end();
}

IDataNode* JsonDataNode::getFirstChildByName(const std::string& name) {
    auto it = m_children.find(name);
    if (it != m_children.end()) {
        return it->second.get();
    }
    return nullptr;
}

// ========================================
// PATTERN MATCHING SEARCH
// ========================================

bool JsonDataNode::matchesPattern(const std::string& text, const std::string& pattern) const {
    // Convert wildcard pattern to regex
    std::string regexPattern = pattern;

    // Escape special regex characters except *
    regexPattern = std::regex_replace(regexPattern, std::regex("\\."), "\\.");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\+"), "\\+");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\?"), "\\?");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\["), "\\[");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\]"), "\\]");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\^"), "\\^");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\$"), "\\$");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\("), "\\(");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\)"), "\\)");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\{"), "\\{");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\}"), "\\}");
    regexPattern = std::regex_replace(regexPattern, std::regex("\\|"), "\\|");

    // Convert * to .*
    regexPattern = std::regex_replace(regexPattern, std::regex("\\*"), ".*");

    // Match entire string
    std::regex re("^" + regexPattern + "$");
    return std::regex_match(text, re);
}

void JsonDataNode::collectMatchingNodes(const std::string& pattern, std::vector<IDataNode*>& results) {
    // Check this node
    if (matchesPattern(m_name, pattern)) {
        results.push_back(this);
    }

    // Recursively check children
    for (auto& [name, child] : m_children) {
        child->collectMatchingNodes(pattern, results);
    }
}

std::vector<IDataNode*> JsonDataNode::getChildrenByNameMatch(const std::string& pattern) {
    std::vector<IDataNode*> results;
    collectMatchingNodes(pattern, results);
    return results;
}

bool JsonDataNode::hasChildrenByNameMatch(const std::string& pattern) const {
    // Cast away const for search (doesn't modify state)
    JsonDataNode* mutableThis = const_cast<JsonDataNode*>(this);
    std::vector<IDataNode*> results;
    mutableThis->collectMatchingNodes(pattern, results);
    return !results.empty();
}

IDataNode* JsonDataNode::getFirstChildByNameMatch(const std::string& pattern) {
    std::vector<IDataNode*> results;
    collectMatchingNodes(pattern, results);
    return results.empty() ? nullptr : results[0];
}

// ========================================
// QUERY BY PROPERTIES
// ========================================

void JsonDataNode::collectNodesByProperty(const std::string& propName,
                                         const std::function<bool(const IDataValue&)>& predicate,
                                         std::vector<IDataNode*>& results) {
    // Check this node
    if (hasProperty(propName)) {
        auto value = getData();
        if (value->has(propName)) {
            auto propValue = value->get(propName);
            if (predicate(*propValue)) {
                results.push_back(this);
            }
        }
    }

    // Recursively check children
    for (auto& [name, child] : m_children) {
        child->collectNodesByProperty(propName, predicate, results);
    }
}

std::vector<IDataNode*> JsonDataNode::queryByProperty(const std::string& propName,
                                                      const std::function<bool(const IDataValue&)>& predicate) {
    std::vector<IDataNode*> results;
    collectNodesByProperty(propName, predicate, results);
    return results;
}

// ========================================
// NODE'S OWN DATA
// ========================================

std::unique_ptr<IDataValue> JsonDataNode::getData() const {
    return std::make_unique<JsonDataValue>(m_data);
}

bool JsonDataNode::hasData() const {
    return !m_data.is_null() && !m_data.empty();
}

void JsonDataNode::setData(std::unique_ptr<IDataValue> data) {
    checkReadOnly();

    // Extract JSON from JsonDataValue
    if (auto* jsonValue = dynamic_cast<JsonDataValue*>(data.get())) {
        m_data = jsonValue->getJson();
    } else {
        throw std::runtime_error("JsonDataNode requires JsonDataValue");
    }
}

// ========================================
// TYPED DATA ACCESS
// ========================================

std::string JsonDataNode::getString(const std::string& name, const std::string& defaultValue) const {
    if (!m_data.is_object() || !m_data.contains(name)) {
        return defaultValue;
    }
    const auto& val = m_data[name];
    return val.is_string() ? val.get<std::string>() : defaultValue;
}

int JsonDataNode::getInt(const std::string& name, int defaultValue) const {
    if (!m_data.is_object() || !m_data.contains(name)) {
        return defaultValue;
    }
    const auto& val = m_data[name];
    return val.is_number() ? val.get<int>() : defaultValue;
}

double JsonDataNode::getDouble(const std::string& name, double defaultValue) const {
    if (!m_data.is_object() || !m_data.contains(name)) {
        return defaultValue;
    }
    const auto& val = m_data[name];
    return val.is_number() ? val.get<double>() : defaultValue;
}

bool JsonDataNode::getBool(const std::string& name, bool defaultValue) const {
    if (!m_data.is_object() || !m_data.contains(name)) {
        return defaultValue;
    }
    const auto& val = m_data[name];
    return val.is_boolean() ? val.get<bool>() : defaultValue;
}

bool JsonDataNode::hasProperty(const std::string& name) const {
    return m_data.is_object() && m_data.contains(name);
}

// ========================================
// TYPED DATA MODIFICATION
// ========================================

void JsonDataNode::setString(const std::string& name, const std::string& value) {
    checkReadOnly();
    if (!m_data.is_object()) {
        m_data = json::object();
    }
    m_data[name] = value;
}

void JsonDataNode::setInt(const std::string& name, int value) {
    checkReadOnly();
    if (!m_data.is_object()) {
        m_data = json::object();
    }
    m_data[name] = value;
}

void JsonDataNode::setDouble(const std::string& name, double value) {
    checkReadOnly();
    if (!m_data.is_object()) {
        m_data = json::object();
    }
    m_data[name] = value;
}

void JsonDataNode::setBool(const std::string& name, bool value) {
    checkReadOnly();
    if (!m_data.is_object()) {
        m_data = json::object();
    }
    m_data[name] = value;
}

// ========================================
// HASH SYSTEM
// ========================================

std::string JsonDataNode::computeHash(const std::string& input) const {
#if GROVE_USE_OPENSSL
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
#else
    // Fallback: use std::hash (not cryptographic, but good enough for change detection)
    std::hash<std::string> hasher;
    size_t hashVal = hasher(input);
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hashVal;
    return ss.str();
#endif
}

std::string JsonDataNode::getDataHash() {
    std::string dataStr = m_data.dump();
    return computeHash(dataStr);
}

std::string JsonDataNode::getTreeHash() {
    // Combine data hash with all children hashes
    std::string combined = getDataHash();
    for (const auto& [name, child] : m_children) {
        combined += name + ":" + child->getTreeHash();
    }
    return computeHash(combined);
}

std::string JsonDataNode::getSubtreeHash(const std::string& childPath) {
    // Parse path and navigate
    size_t pos = childPath.find('/');
    if (pos == std::string::npos) {
        // Direct child
        auto it = m_children.find(childPath);
        if (it != m_children.end()) {
            return it->second->getTreeHash();
        }
        return "";
    } else {
        // Nested path
        std::string firstPart = childPath.substr(0, pos);
        std::string rest = childPath.substr(pos + 1);
        auto it = m_children.find(firstPart);
        if (it != m_children.end()) {
            return it->second->getSubtreeHash(rest);
        }
        return "";
    }
}

// ========================================
// METADATA
// ========================================

std::string JsonDataNode::getPath() const {
    if (m_parent == nullptr) {
        return m_name;
    }
    std::string parentPath = m_parent->getPath();
    return parentPath.empty() ? m_name : parentPath + "/" + m_name;
}

std::string JsonDataNode::getName() const {
    return m_name;
}

std::string JsonDataNode::getNodeType() const {
    return "JsonDataNode";
}

// ========================================
// TREE MODIFICATION
// ========================================

void JsonDataNode::checkReadOnly() const {
    if (m_readOnly) {
        throw std::runtime_error("Cannot modify read-only node: " + getPath());
    }
}

void JsonDataNode::setChild(const std::string& name, std::unique_ptr<IDataNode> node) {
    checkReadOnly();

    // Extract JsonDataNode
    if (auto* jsonNode = dynamic_cast<JsonDataNode*>(node.get())) {
        auto newNode = std::make_unique<JsonDataNode>(
            jsonNode->getName(),
            jsonNode->getJsonData(),
            this,
            m_readOnly // Inherit read-only status
        );

        // Copy children recursively
        for (const auto& [childName, child] : jsonNode->getChildren()) {
            newNode->setChild(childName, std::make_unique<JsonDataNode>(
                child->getName(),
                child->getJsonData(),
                newNode.get(),
                m_readOnly
            ));
        }

        m_children[name] = std::move(newNode);
    } else {
        throw std::runtime_error("JsonDataNode requires JsonDataNode child");
    }
}

bool JsonDataNode::removeChild(const std::string& name) {
    checkReadOnly();
    return m_children.erase(name) > 0;
}

void JsonDataNode::clearChildren() {
    checkReadOnly();
    m_children.clear();
}

} // namespace grove
