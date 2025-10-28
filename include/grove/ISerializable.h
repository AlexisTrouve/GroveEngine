#pragma once

#include "IDataNode.h"
#include <memory>

namespace warfactory {

class ISerializable {
public:
    virtual ~ISerializable() = default;

    virtual std::unique_ptr<IDataNode> serialize() const = 0;
    virtual void deserialize(const IDataNode& data) = 0;
};

} // namespace warfactory