#pragma once

#include "IDataNode.h"
#include <string>
#include <memory>

namespace grove {

class SerializationRegistry;

class ASerializable {
private:
    std::string instance_id;

public:
    ASerializable(const std::string& id);
    virtual ~ASerializable();

    const std::string& getInstanceId() const { return instance_id; }

    virtual std::unique_ptr<IDataNode> serialize() const = 0;
    virtual void deserialize(const IDataNode& data) = 0;

protected:
    void registerForSerialization();
    void unregisterFromSerialization();
};

} // namespace grove