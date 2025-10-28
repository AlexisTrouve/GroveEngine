#include "grove/DataTreeFactory.h"
#include "grove/JsonDataTree.h"
#include <stdexcept>

namespace grove {

std::unique_ptr<IDataTree> DataTreeFactory::create(const std::string& type, const std::string& sourcePath) {
    if (type == "json") {
        return std::make_unique<JsonDataTree>(sourcePath);
    }

    throw std::runtime_error("Unknown data tree type: " + type);
}

} // namespace grove
