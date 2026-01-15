#pragma once

#include <string>
#include <memory>
#include "IDataTree.h"

namespace grove {

/**
 * @brief Factory for creating data tree instances
 */
class DataTreeFactory {
public:
    /**
     * @brief Create data tree from configuration source
     * @param type Tree type ("json", "database", etc.)
     * @param sourcePath Path to configuration source
     * @return Data tree instance
     */
    static std::unique_ptr<IDataTree> create(const std::string& type, const std::string& sourcePath);
};

} // namespace grove