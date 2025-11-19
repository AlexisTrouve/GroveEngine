#include "grove/JsonDataTree.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace grove {

namespace fs = std::filesystem;

JsonDataTree::JsonDataTree(const std::string& basePath)
    : m_basePath(basePath) {

    // Create root node
    m_root = std::make_unique<JsonDataNode>("", json::object(), nullptr, false);

    // Load three sub-trees
    loadConfigTree();
    loadDataTree();
    initializeRuntimeTree();

    // Attach to root
    m_root->setChild("config", std::move(m_configRoot));
    m_root->setChild("data", std::move(m_dataRoot));
    m_root->setChild("runtime", std::move(m_runtimeRoot));
}

// ========================================
// TREE ACCESS
// ========================================

std::unique_ptr<IDataNode> JsonDataTree::getRoot() {
    // Return copy of root
    return std::make_unique<JsonDataNode>("", m_root->getJsonData(), nullptr, false);
}

std::unique_ptr<IDataNode> JsonDataTree::getNode(const std::string& path) {
    if (path.empty()) {
        return getRoot();
    }

    // Split path and navigate
    JsonDataNode* current = m_root.get();
    std::string remaining = path;

    while (!remaining.empty()) {
        size_t pos = remaining.find('/');
        std::string part = (pos == std::string::npos) ? remaining : remaining.substr(0, pos);
        remaining = (pos == std::string::npos) ? "" : remaining.substr(pos + 1);

        auto child = current->getFirstChildByName(part);
        if (!child) {
            return nullptr;
        }
        current = static_cast<JsonDataNode*>(child);
    }

    return std::make_unique<JsonDataNode>(current->getName(),
                                          current->getJsonData(),
                                          nullptr,
                                          false);
}

// ========================================
// SEPARATE ROOTS
// ========================================

std::unique_ptr<IDataNode> JsonDataTree::getConfigRoot() {
    auto configNode = m_root->getFirstChildByName("config");
    if (!configNode) {
        return nullptr;
    }
    auto* jsonNode = static_cast<JsonDataNode*>(configNode);
    return std::make_unique<JsonDataNode>(jsonNode->getName(),
                                          jsonNode->getJsonData(),
                                          nullptr,
                                          true); // Read-only
}

std::unique_ptr<IDataNode> JsonDataTree::getDataRoot() {
    auto dataNode = m_root->getFirstChildByName("data");
    if (!dataNode) {
        return nullptr;
    }
    auto* jsonNode = static_cast<JsonDataNode*>(dataNode);
    return std::make_unique<JsonDataNode>(jsonNode->getName(),
                                          jsonNode->getJsonData(),
                                          nullptr,
                                          false);
}

std::unique_ptr<IDataNode> JsonDataTree::getRuntimeRoot() {
    auto runtimeNode = m_root->getFirstChildByName("runtime");
    if (!runtimeNode) {
        return nullptr;
    }
    auto* jsonNode = static_cast<JsonDataNode*>(runtimeNode);
    return std::make_unique<JsonDataNode>(jsonNode->getName(),
                                          jsonNode->getJsonData(),
                                          nullptr,
                                          false);
}

// ========================================
// SAVE OPERATIONS
// ========================================

bool JsonDataTree::saveData() {
    try {
        std::string dataPath = m_basePath + "/data";
        auto dataNode = m_root->getFirstChildByName("data");
        if (!dataNode) {
            return false;
        }

        auto* jsonNode = static_cast<JsonDataNode*>(dataNode);
        json dataJson = nodeToJson(jsonNode);

        // Save each top-level child as separate file
        for (const auto& [name, child] : jsonNode->getChildren()) {
            std::string filePath = dataPath + "/" + name + ".json";
            json childJson = nodeToJson(child.get());
            if (!saveJsonFile(filePath, childJson)) {
                return false;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save data: " << e.what() << std::endl;
        return false;
    }
}

bool JsonDataTree::saveNode(const std::string& path) {
    // Only allow saving data/ paths
    if (path.find("data/") != 0) {
        std::cerr << "Can only save nodes under data/: " << path << std::endl;
        return false;
    }

    try {
        auto node = getNode(path);
        if (!node) {
            return false;
        }

        // Extract filename from path
        size_t lastSlash = path.find_last_of('/');
        std::string filename = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
        std::string filePath = m_basePath + "/" + path + ".json";

        auto* jsonNode = dynamic_cast<JsonDataNode*>(node.get());
        if (!jsonNode) {
            return false;
        }

        json nodeJson = nodeToJson(jsonNode);
        return saveJsonFile(filePath, nodeJson);
    } catch (const std::exception& e) {
        std::cerr << "Failed to save node " << path << ": " << e.what() << std::endl;
        return false;
    }
}

// ========================================
// HOT-RELOAD
// ========================================

bool JsonDataTree::checkForChanges() {
    std::string configPath = m_basePath + "/config";

    try {
        for (const auto& [filePath, lastTime] : m_configFileTimes) {
            if (!fs::exists(filePath)) {
                return true; // File deleted
            }

            auto currentTime = fs::last_write_time(filePath);
            if (currentTime != lastTime) {
                return true; // File modified
            }
        }

        // Check for new files
        if (fs::exists(configPath)) {
            for (const auto& entry : fs::directory_iterator(configPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    if (m_configFileTimes.find(entry.path().string()) == m_configFileTimes.end()) {
                        return true; // New file
                    }
                }
            }
        }

        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error checking for changes: " << e.what() << std::endl;
        return false;
    }
}

bool JsonDataTree::reloadIfChanged() {
    if (!checkForChanges()) {
        return false;
    }

    try {
        loadConfigTree();

        // Re-attach config root to main root
        m_root->setChild("config", std::move(m_configRoot));

        // Recreate m_configRoot for future access
        auto* configNode = static_cast<JsonDataNode*>(m_root->getFirstChildByName("config"));
        if (configNode) {
            m_configRoot = std::make_unique<JsonDataNode>(configNode->getName(),
                                                          configNode->getJsonData(),
                                                          nullptr,
                                                          true);
        }

        // Trigger callbacks
        for (auto& callback : m_reloadCallbacks) {
            callback();
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to reload config: " << e.what() << std::endl;
        return false;
    }
}

void JsonDataTree::onTreeReloaded(std::function<void()> callback) {
    m_reloadCallbacks.push_back(callback);
}

// ========================================
// METADATA
// ========================================

std::string JsonDataTree::getType() {
    return "JsonDataTree";
}

// ========================================
// LOAD OPERATIONS
// ========================================

bool JsonDataTree::loadConfigFile(const std::string& filename) {
    std::string filePath = m_basePath + "/config/" + filename;

    if (!fs::exists(filePath)) {
        std::cerr << "Config file not found: " << filePath << "\n";
        return false;
    }

    try {
        json fileData = loadJsonFile(filePath);
        std::string nodeName = filename.substr(0, filename.find_last_of('.'));

        // Get config root from m_root
        auto* configNode = static_cast<JsonDataNode*>(m_root->getFirstChildByName("config"));
        if (!configNode) {
            std::cerr << "Config root not found\n";
            return false;
        }

        // Build node and add to config tree
        buildNodeFromJson(nodeName, fileData, configNode, true);

        // Track file timestamp for hot-reload
        m_configFileTimes[filePath] = fs::last_write_time(filePath);

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config file " << filePath << ": " << e.what() << "\n";
        return false;
    }
}

bool JsonDataTree::loadDataDirectory() {
    std::string dataPath = m_basePath + "/data";

    if (!fs::exists(dataPath)) {
        std::cerr << "Data directory not found: " << dataPath << "\n";
        return false;
    }

    try {
        // Get data root from m_root
        auto* dataNode = static_cast<JsonDataNode*>(m_root->getFirstChildByName("data"));
        if (!dataNode) {
            std::cerr << "Data root not found\n";
            return false;
        }

        // Scan directory recursively
        scanDirectory(dataPath, dataNode, false);

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load data directory: " << e.what() << "\n";
        return false;
    }
}

// ========================================
// HELPER METHODS
// ========================================

void JsonDataTree::loadConfigTree() {
    std::string configPath = m_basePath + "/config";
    m_configRoot = std::make_unique<JsonDataNode>("config", json::object(), nullptr, false);  // NOT read-only itself

    if (fs::exists(configPath) && fs::is_directory(configPath)) {
        scanDirectory(configPath, m_configRoot.get(), true);
        updateFileTimestamps(configPath);
    }
}

void JsonDataTree::loadDataTree() {
    std::string dataPath = m_basePath + "/data";
    m_dataRoot = std::make_unique<JsonDataNode>("data", json::object(), nullptr, false);

    if (fs::exists(dataPath) && fs::is_directory(dataPath)) {
        scanDirectory(dataPath, m_dataRoot.get(), false);
    } else {
        // Create data directory if it doesn't exist
        fs::create_directories(dataPath);
    }
}

void JsonDataTree::initializeRuntimeTree() {
    m_runtimeRoot = std::make_unique<JsonDataNode>("runtime", json::object(), nullptr, false);
}

void JsonDataTree::scanDirectory(const std::string& dirPath, JsonDataNode* parentNode, bool readOnly) {
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string filename = entry.path().stem().string();
            json data = loadJsonFile(entry.path().string());
            buildNodeFromJson(filename, data, parentNode, readOnly);
        } else if (entry.is_directory()) {
            // Create child node for subdirectory
            auto childNode = std::make_unique<JsonDataNode>(
                entry.path().filename().string(),
                json::object(),
                parentNode,
                readOnly
            );
            scanDirectory(entry.path().string(), childNode.get(), readOnly);
            parentNode->setChild(entry.path().filename().string(), std::move(childNode));
        }
    }
}

json JsonDataTree::loadJsonFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath);
    }

    json data;
    file >> data;
    return data;
}

bool JsonDataTree::saveJsonFile(const std::string& filePath, const json& data) {
    try {
        // Ensure directory exists
        fs::path path(filePath);
        if (path.has_parent_path()) {
            fs::create_directories(path.parent_path());
        }

        std::ofstream file(filePath);
        if (!file.is_open()) {
            return false;
        }

        file << data.dump(2); // Pretty print with 2-space indent
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save JSON file: " << e.what() << std::endl;
        return false;
    }
}

void JsonDataTree::buildNodeFromJson(const std::string& name, const json& data, JsonDataNode* parentNode, bool readOnly) {
    auto node = std::make_unique<JsonDataNode>(name, data, parentNode, readOnly);

    // If data is an object with children, create child nodes
    if (data.is_object()) {
        for (auto& [key, value] : data.items()) {
            if (value.is_object() || value.is_array()) {
                buildNodeFromJson(key, value, node.get(), readOnly);
            }
        }
    }

    parentNode->setChild(name, std::move(node));
}

json JsonDataTree::nodeToJson(const JsonDataNode* node) {
    json result = node->getJsonData();

    // Add children
    for (const auto& [name, child] : node->getChildren()) {
        result[name] = nodeToJson(child.get());
    }

    return result;
}

void JsonDataTree::updateFileTimestamps(const std::string& dirPath) {
    m_configFileTimes.clear();

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            m_configFileTimes[entry.path().string()] = fs::last_write_time(entry.path());
        }
    }
}

} // namespace grove
