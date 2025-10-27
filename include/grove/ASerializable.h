#pragma once

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

namespace warfactory {

class SerializationRegistry;

class ASerializable {
private:
    std::string instance_id;

public:
    ASerializable(const std::string& id);
    virtual ~ASerializable();

    const std::string& getInstanceId() const { return instance_id; }

    virtual json serialize() const = 0;
    virtual void deserialize(const json& data) = 0;

protected:
    void registerForSerialization();
    void unregisterFromSerialization();
};

} // namespace warfactory