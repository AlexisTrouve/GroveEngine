#pragma once

#include "IDataTree.h"
#include "JsonDataNode.h"
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <chrono>
#include <filesystem>

namespace grove {

/**
 * @brief Concrete implementation of IDataTree backed by JSON files
 *
 * Manages three separate trees:
 * - config/  : Read-only configuration loaded from files (hot-reload enabled)
 * - data/    : Persistent player data (read-write, saved to disk)
 * - runtime/ : Temporary runtime state (read-write, never saved)
 *
 * File structure:
 * basePath/
 *   ├─ config/
 *   │   ├─ tanks.json
 *   │   ├─ weapons.json
 *   │   └─ ...
 *   ├─ data/
 *   │   ├─ campaign.json
 *   │   ├─ unlocks.json
 *   │   └─ ...
 *   └─ runtime/  (in-memory only, not on disk)
 */
class JsonDataTree : public IDataTree {
public:
    /**
     * @brief Create a data tree from a base directory
     * @param basePath Base directory containing config/, data/ subdirs
     */
    explicit JsonDataTree(const std::string& basePath);
    virtual ~JsonDataTree() = default;

    // Tree access
    std::unique_ptr<IDataNode> getRoot() override;
    std::unique_ptr<IDataNode> getNode(const std::string& path) override;

    // Separate roots
    std::unique_ptr<IDataNode> getConfigRoot() override;
    std::unique_ptr<IDataNode> getDataRoot() override;
    std::unique_ptr<IDataNode> getRuntimeRoot() override;

    // Save operations
    bool saveData() override;
    bool saveNode(const std::string& path) override;

    // Load operations
    bool loadConfigFile(const std::string& filename) override;
    bool loadDataDirectory() override;

    // Hot-reload
    bool checkForChanges() override;
    bool reloadIfChanged() override;
    void onTreeReloaded(std::function<void()> callback) override;

    // Metadata
    std::string getType() override;

private:
    std::string m_basePath;
    std::unique_ptr<JsonDataNode> m_root;
    std::unique_ptr<JsonDataNode> m_configRoot;
    std::unique_ptr<JsonDataNode> m_dataRoot;
    std::unique_ptr<JsonDataNode> m_runtimeRoot;

    std::map<std::string, std::filesystem::file_time_type> m_configFileTimes;
    std::vector<std::function<void()>> m_reloadCallbacks;

    // Helper methods
    void loadConfigTree();
    void loadDataTree();
    void initializeRuntimeTree();
    void scanDirectory(const std::string& dirPath, JsonDataNode* parentNode, bool readOnly);
    json loadJsonFile(const std::string& filePath);
    bool saveJsonFile(const std::string& filePath, const json& data);
    void buildNodeFromJson(const std::string& name, const json& data, JsonDataNode* parentNode, bool readOnly);
    json nodeToJson(const JsonDataNode* node);
    void updateFileTimestamps(const std::string& dirPath);
};

} // namespace grove
